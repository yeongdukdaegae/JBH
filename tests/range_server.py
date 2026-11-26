# 업데이트 시험용 서버: 폴더의 파일을 HTTP 로 내줌 (Range 지원) — CI 의 "업데이트 시험" 이 씀
#   python tests/range_server.py <폴더> <포트>
import http.server, os, re, sys

ROOT, PORT = sys.argv[1], int(sys.argv[2])

class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        p = os.path.join(ROOT, os.path.basename(self.path))
        if not os.path.isfile(p):
            self.send_error(404); return
        size = os.path.getsize(p)
        m = re.match(r'bytes=(\d+)-(\d+)$', self.headers.get('Range', ''))
        a, b = (int(m[1]), min(int(m[2]), size - 1)) if m else (0, size - 1)
        self.send_response(206 if m else 200)
        if m: self.send_header('Content-Range', 'bytes %d-%d/%d' % (a, b, size))
        self.send_header('Content-Length', str(b - a + 1))
        self.end_headers()
        with open(p, 'rb') as f:
            f.seek(a); left = b - a + 1
            while left > 0:
                d = f.read(min(65536, left))
                if not d: break
                self.wfile.write(d); left -= len(d)
    def log_message(self, *a): pass

http.server.ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
