import http.server
import socketserver

PORT = 8080

class COOPHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

if __name__ == "__main__":
    with socketserver.TCPServer(("", PORT), COOPHandler) as httpd:
        print(f"Serving at port {PORT} with COOP/COEP headers for SharedArrayBuffer.")
        print("Open http://localhost:8080/benchmark.html or http://localhost:8080/index.html")
        httpd.serve_forever()