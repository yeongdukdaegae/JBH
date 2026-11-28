# https · wss 서버 시험용 클라이언트 (CI) — 인증서는 확인하지 않음 (스스로 서명한 시험용)
#   python tests/tls_client.py 포트  →  "py https=…" · "py wss=…" 줄
import socket, ssl, base64, os, sys, time
port = int(sys.argv[1])
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
def conn():
    for _ in range(40):
        try: return ctx.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=10), server_hostname='localhost')
        except OSError: time.sleep(0.5)
    raise SystemExit('py: 접속하지 못함')
s = conn(); s.sendall(b'GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n'); r = b''
while True:
    d = s.recv(4096)
    if not d: break
    r += d
print('py https=' + r.split(b'\r\n')[0].decode() + ' body=' + r.split(b'\r\n\r\n', 1)[1].decode() + ' tls=' + s.version())
s = conn(); k = base64.b64encode(os.urandom(16)).decode()
s.sendall(('GET /py HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % k).encode())
h = b''
while b'\r\n\r\n' not in h: h += s.recv(1)
def frame():
    b = s.recv(2); n = b[1] & 127
    if n == 126: n = int.from_bytes(s.recv(2), 'big')
    d = b''
    while len(d) < n: d += s.recv(n - len(d))
    return d.decode()
def send(t):
    d = t.encode(); m = os.urandom(4); n = len(d)
    hd = bytes([0x81, 0x80 | n]) if n < 126 else bytes([0x81, 0x80 | 126]) + n.to_bytes(2, 'big')
    s.sendall(hd + m + bytes(c ^ m[i % 4] for i, c in enumerate(d)))
w = frame(); send('x' * 3000); e = frame()
print('py wss=' + h.split(b'\r\n')[0].decode() + ' first=' + w + ' echo=' + str(len(e)))
