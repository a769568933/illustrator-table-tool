"""Local-only test surface. All table operations run in the real C++ core."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def simulate(executable, payload):
    action = payload.get('action', 'inspect')
    editing = action in {'edit', 'selectText', 'deleteCluster', 'tab'}
    if action not in {'create', 'select', 'dragSelect', 'command', 'inspect', 'sample', 'style', 'format'} and not editing:
        raise ValueError('Unknown simulation action')
    args = [str(executable), action]
    if action == 'create':
        args += [str(int(payload.get('rows', 4))), str(int(payload.get('columns', 4)))]
    elif action in {'select', 'dragSelect', 'command', 'style', 'format'}:
        selection = payload.get('selection', [0, 0, 0, 0])
        if len(selection) != 4:
            raise ValueError('Invalid selection')
        args += [str(int(value)) for value in selection]
        if action == 'command':
            args.append(str(int(payload['command'])))
    standard_input=payload.get('state','')
    if action == 'format':
        fields=('font','fontSize','alignment','verticalAlignment','fillEnabled','borderWidth')
        mask=sum(1<<i for i,key in enumerate(fields) if key in payload)
        args += [str(mask),str(float(payload.get('fontSize',12))),str(int(payload.get('alignment',0))),str(int(payload.get('verticalAlignment',0))),'1' if payload.get('fillEnabled',False) else '0',str(float(payload.get('borderWidth',0.75)))]
        font=payload.get('font','')
        if not isinstance(font,str):
            raise ValueError('Invalid font')
        standard_input=f'{len(standard_input.encode("utf-8"))}\n{standard_input}\n{len(font.encode("utf-8"))}\n{font}\n'
    if editing:
        args += [str(int(payload.get(key,0))) for key in ('row','column','anchor','caret')]
        args.append('1' if payload.get('backward',False) else '0')
        inserted=payload.get('text','')
        if not isinstance(standard_input,str) or not isinstance(inserted,str):
            raise ValueError('Invalid text payload')
        boundaries=payload.get('boundaries',[])
        if not isinstance(boundaries,list) or len(boundaries)>1024*1024+1:
            raise ValueError('Invalid boundary count')
        standard_input=f'{len(standard_input.encode("utf-8"))}\n{standard_input}\n{len(inserted.encode("utf-8"))}\n{inserted}\n'
        standard_input += ' '.join(str(int(value)) for value in boundaries)+'\n'
        if len(standard_input.encode('utf-8'))>20*1024*1024:
            raise ValueError('Simulation input exceeds limit')
    result = subprocess.run(args, input=standard_input, text=True,
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
