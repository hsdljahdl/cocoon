from http.server import HTTPServer, BaseHTTPRequestHandler


class EchoHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(f"Echo GET: {self.path}".encode())

    def do_POST(self):
        length = int(self.headers.get('content-length', 0))
        data = self.rfile.read(length)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"Echo POST: " + data)


if __name__ == '__main__':
    server_address = ('', 8118)
    httpd = HTTPServer(server_address, EchoHandler)
    print('Echo server running on port 8118...')
    httpd.serve_forever()
