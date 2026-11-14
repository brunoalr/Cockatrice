import http.server
import sys

port, root = int(sys.argv[1]), sys.argv[2]
isolation_headers = "--no-isolation-headers" not in sys.argv[3:]


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=root, **kwargs)

    def end_headers(self):
        if isolation_headers:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_request(self, code="-", size="-"):
        print(self.requestline, flush=True)

    def log_message(self, format, *args):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
