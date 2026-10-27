# Local dev server for the browser prototype: like `python3 -m http.server`,
# but tells the browser never to cache, so code changes always load.
#   python3 serve.py [port]   (default 8765; Windows: py -3 serve.py [port])
import http.server, sys, os

class NoCache(http.server.SimpleHTTPRequestHandler):
    # some Windows machines map .js to text/plain in the registry; browsers then won't run the scripts
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".js": "text/javascript", ".mjs": "text/javascript", ".json": "application/json", ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, must-revalidate")
        super().end_headers()

os.chdir(os.path.dirname(os.path.abspath(__file__)))
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
http.server.ThreadingHTTPServer(("", port), NoCache).serve_forever()
