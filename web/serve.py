# Local dev server for the browser prototype: like `python3 -m http.server`,
# but tells the browser never to cache, so code changes always load.
#   python3 serve.py [port]   (default 8765)
import http.server, sys, os

class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, must-revalidate")
        super().end_headers()

os.chdir(os.path.dirname(os.path.abspath(__file__)))
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
http.server.ThreadingHTTPServer(("", port), NoCache).serve_forever()
