"""Generate the Windows PiPL resource using the user's Adobe SDK template.

The 2024 SDK's original generator writes Python 2 strings to binary streams.
This encoder preserves its documented resource layout under Python 3.
"""
import argparse
import json
import pathlib
import struct


def generate(template_path, output_path, plugin_name):
    template = json.loads(pathlib.Path(template_path).read_text(encoding='utf-8'))
    properties = []
    for key, entry in template.items():
        if key == 'mac':
            continue
        if key == 'win':
            entries = entry.items()
        else:
            entries = [(key, entry)]
        for prop_key, prop in entries:
            if 'value' not in prop:
                continue
            value = prop['value']
            if prop_key == 'wx86':
                value = 'PluginMain\0\0\0\0'
            elif prop_key == 'pinm':
                value = plugin_name
            if isinstance(value, int):
                payload = struct.pack('>I', value)
            else:
                payload = value.encode('ascii')
                payload += b'\0' * (-len(payload) % 4)
            properties.append(struct.pack('>4s4sII', prop['vendor'].encode('ascii'),
                                          prop_key.encode('ascii'), prop.get('id', 0),
                                          len(payload)) + payload)
    output_path = pathlib.Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(struct.pack('>III', 1, 0, len(properties)) + b''.join(properties))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--template', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--name', default='IllustratorTableTool')
    args = parser.parse_args()
    generate(args.template, args.output, args.name)
