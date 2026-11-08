"""Exercise the real socket transport and example CLI against a local FSAPI radio."""
import http.server
import subprocess
import sys
import threading
import urllib.parse

requests = []
mode = "normal"

class RadioHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        requests.append(parsed.path)
        if query.get("pin") != ["1234"]:
            self.send_error(403)
            return
        if mode == "large":
            body = b"x" * 40000
        elif parsed.path == "/fsapi/CREATE_SESSION":
            body = b"<fsapiResponse><status>FS_OK</status><sessionId>42</sessionId></fsapiResponse>"
        elif query.get("sid") != ["42"]:
            self.send_error(404)
            return
        elif parsed.path == "/fsapi/GET/netRemote.sys.power":
            body = b"<fsapiResponse><status>FS_OK</status><value><u8>1</u8></value></fsapiResponse>"
        elif parsed.path == "/fsapi/GET/netRemote.sys.audio.volume":
            body = b"<fsapiResponse><status>FS_OK</status><value><u8>7</u8></value></fsapiResponse>"
        elif parsed.path == "/fsapi/DELETE_SESSION":
            body = b"<fsapiResponse><status>FS_OK</status></fsapiResponse>"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_args):
        pass

server = http.server.HTTPServer(("127.0.0.1", 0), RadioHandler)
worker = threading.Thread(target=server.serve_forever, daemon=True)
worker.start()
endpoint = f"http://127.0.0.1:{server.server_port}/fsapi"
try:
    result = subprocess.run([sys.argv[1], endpoint], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert "Power: on; volume: 7" in result.stdout, result.stdout
    assert "[debug]" not in result.stdout, result.stdout
    assert requests == ["/fsapi/CREATE_SESSION", "/fsapi/GET/netRemote.sys.power",
                        "/fsapi/GET/netRemote.sys.audio.volume", "/fsapi/DELETE_SESSION"], requests
    for arguments in (["--debug", endpoint], [endpoint, "1234", "--debug"]):
        result = subprocess.run([sys.argv[1], *arguments], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert "[debug] HTTP GET " in result.stdout, result.stdout
        assert "[debug] TCP connected" in result.stdout, result.stdout
        assert "[debug] HTTP response: HTTP/1.0 200 OK" in result.stdout, result.stdout
        assert "[debug] FSAPI CREATE_SESSION status=FS_OK" in result.stdout, result.stdout
        assert "[debug] Local session cleared" in result.stdout, result.stdout
        assert "[debug]" not in result.stderr, result.stderr
        assert "pin=1234" not in result.stdout and "sid=42" not in result.stdout, result.stdout
        assert "<sessionId>" not in result.stdout, result.stdout
    result = subprocess.run([sys.argv[1], endpoint, "wrong"], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "HTTP 403" in result.stderr, result
    result = subprocess.run([sys.argv[1], "--debug", endpoint, "private-pin"], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "[debug] HTTP failed: HTTP 403" in result.stdout, result
    assert "private-pin" not in result.stdout + result.stderr, result
    mode = "large"
    result = subprocess.run([sys.argv[1], endpoint], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "32 KiB" in result.stderr, result
    print("Socket transport integration tests passed")
finally:
    server.shutdown()
    server.server_close()
    worker.join()
