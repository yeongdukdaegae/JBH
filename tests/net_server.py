# 네트워크 시험용 서버 — HTTP (다른 곳에서의 요청 허용 · CORS) 와 WebSocket (받은 것을 그대로 돌려줌)
#   python tests/net_server.py <HTTP 포트> <WebSocket 포트>
#   GET /hello → "hello net" · POST /echo → "<X-Tag 값>:<받은 본문>" · GET /big → 200000 바이트 · 그 밖 → 404 "nope"
import base64, hashlib, http.server, socket, struct, sys, threading

HP, WP = int(sys.argv[1]), int(sys.argv[2])

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    def out(self, code, body):
        self.send_response(code)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', '*')
        self.send_header('Access-Control-Allow-Methods', '*')
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_OPTIONS(self): self.out(204, b'')
    def do_GET(self):
        if self.path == '/hello': self.out(200, b'hello net')
        elif self.path == '/big': self.out(200, bytes(i % 251 for i in range(200000)))
        else: self.out(404, b'nope')
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(n)
        self.out(200, (self.headers.get('X-Tag') or '-').encode() + b':' + body)
    def log_message(self, *a): pass

def recvn(c, n):
    b = b''
    while len(b) < n:
        d = c.recv(n - len(b))
        if not d: raise EOFError
        b += d
    return b

def ws_client(c):
    try:
        req = b''
        while b'\r\n\r\n' not in req: req += c.recv(4096)
        key = [l.split(b':', 1)[1].strip() for l in req.split(b'\r\n') if l.lower().startswith(b'sec-websocket-key')][0]
        acc = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
        c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + acc + b'\r\n\r\n')
        def send(op, data):
            n = len(data)
            head = bytes([0x80 | op]) + (bytes([n]) if n < 126 else bytes([126]) + struct.pack('>H', n) if n < 65536 else bytes([127]) + struct.pack('>Q', n))
            c.sendall(head + data)
        send(1, b'welcome')
        while True:
            h = recvn(c, 2); op, n = h[0] & 15, h[1] & 127
            if n == 126: n = struct.unpack('>H', recvn(c, 2))[0]
            elif n == 127: n = struct.unpack('>Q', recvn(c, 8))[0]
            mask = recvn(c, 4) if h[1] & 0x80 else b'\0\0\0\0'
            data = bytes(b ^ mask[i & 3] for i, b in enumerate(recvn(c, n)))
            if op == 8: send(8, data[:2]); break
            if op == 9: send(10, data); continue
            if op in (1, 2):
                if data == b'bye': send(8, struct.pack('>H', 1000)); break   # 서버가 먼저 닫음
                send(op, b'echo:' + data)
    except Exception: pass
    finally: c.close()

def ws_server():
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('127.0.0.1', WP)); s.listen(8)
    while True:
        c, _ = s.accept(); threading.Thread(target=ws_client, args=(c,), daemon=True).start()

threading.Thread(target=ws_server, daemon=True).start()
http.server.ThreadingHTTPServer(('127.0.0.1', HP), H).serve_forever()
