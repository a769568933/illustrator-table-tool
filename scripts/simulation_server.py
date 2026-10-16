"""Local-only test surface. All table operations run in the real C++ core."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def simulate(executable, payload):
    action = payload.get('action', 'inspect')
    if action not in {'create', 'select', 'command', 'inspect', 'sample', 'style'}:
        raise ValueError('Unknown simulation action')
    args = [str(executable), action]
    if action == 'create':
        args += [str(int(payload.get('rows', 4))), str(int(payload.get('columns', 4)))]
    elif action in {'select', 'command', 'style'}:
        selection = payload.get('selection', [0, 0, 0, 0])
        if len(selection) != 4:
            raise ValueError('Invalid selection')
        args += [str(int(value)) for value in selection]
        if action == 'command':
            args.append(str(int(payload['command'])))
    result = subprocess.run(args, input=payload.get('state', ''), text=True,
                            encoding='utf-8', capture_output=True, timeout=10)
    if result.returncode:
        raise ValueError(result.stderr.strip() or 'Simulation failed')
    return json.loads(result.stdout)


def serve(executable, port):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path not in {'/', '/index.html'}:
                self.send_error(404)
                return
            content = (ROOT / 'simulation/index.html').read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(content)

        def do_POST(self):
            try:
                if self.path != '/simulate':
                    raise ValueError('Unknown endpoint')
                length = int(self.headers.get('Content-Length', 0))
                if length < 1 or length > 20*1024*1024:
                    raise ValueError('Invalid request size')
                result = simulate(executable, json.loads(self.rfile.read(length)))
                content = json.dumps(result, ensure_ascii=True).encode('ascii')
                self.send_response(200)
            except (ValueError, subprocess.TimeoutExpired) as error:
                content = json.dumps({'error': str(error)}, ensure_ascii=True).encode('ascii')
                self.send_response(400)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(content)

        def log_message(self, *_):
            pass

    print(f'Simulation ready: http://127.0.0.1:{port}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8776)
    args = parser.parse_args()
    serve(args.executable.resolve(strict=True), args.port)
