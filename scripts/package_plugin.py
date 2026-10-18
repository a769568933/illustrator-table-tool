"""Validate the native Illustrator binary and package only public deliverables."""
import argparse
import hashlib
from pathlib import Path
import struct
import zipfile


def validate_plugin(path):
    path = Path(path)
    if not path.is_file():
        raise ValueError('Plugin does not exist')
    data = path.read_bytes()

    def read(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('Truncated native plugin')
        return struct.unpack_from(fmt, data, offset)

    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Plugin must be a Windows x64 PE binary')
    pe, = read('<I', 0x3c)
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, count = read('<HH', pe + 4)
    optional_size, flags = read('<HH', pe + 20)
    if machine != 0x8664 or read('<H', pe + 24)[0] != 0x20b:
        raise ValueError('Plugin must use x64 architecture')
    if not flags & 0x2000 or optional_size < 136 or count > 96:
        raise ValueError('Plugin must be a native DLL with PE directories')
    sections = []
    for index in range(count):
        size, address, raw_size, raw = read('<IIII', pe + 24 + optional_size + 40*index + 8)
        if raw + raw_size > len(data):
            raise ValueError('Truncated PE section')
        sections.append((address, size, raw, raw_size))

    def offset(rva, size=1):
        for address, _, raw, raw_size in sections:
            if address <= rva and rva + size <= address + raw_size:
                return raw + rva - address
        raise ValueError('Invalid PE directory address')

    export_rva, export_size = read('<II', pe + 24 + 112)
    export = offset(export_rva, min(export_size, 40))
    names_count, = read('<I', export + 24)
    names_rva, = read('<I', export + 32)
    if names_count > 65536:
        raise ValueError('Invalid export count')
    names = offset(names_rva, names_count*4)
    exported = set()
    for index in range(names_count):
        name = offset(read('<I', names + index*4)[0])
        end = data.find(b'\0', name, min(name+512, len(data)))
        if end < 0:
            raise ValueError('Invalid export name')
        exported.add(data[name:end])
    if b'PluginMain' not in exported:
        raise ValueError('PluginMain export is missing')

    resource_rva, resource_size = read('<II', pe + 24 + 112 + 16)
    resource = offset(resource_rva, resource_size)
    named, ids = read('<HH', resource + 12)
    found = False
    for index in range(named + ids):
        entry, _ = read('<II', resource + 16 + 8*index)
        if entry & 0x80000000:
            location = resource + (entry & 0x7fffffff)
            length, = read('<H', location)
            if location + 2 + length*2 > resource + resource_size:
                raise ValueError('Invalid resource name')
            name = data[location+2:location+2+length*2].decode('utf-16-le')
            found = found or name.upper() == 'PIPL'
    if not found:
        raise ValueError('Adobe PiPL resource is missing')
    return hashlib.sha256(data).hexdigest()


def build_package(plugin, output, readme):
    plugin, output, readme = Path(plugin), Path(output), Path(readme)
    digest = validate_plugin(plugin)
    documentation = readme.read_bytes()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / 'IllustratorTableTool-0.2.4-win64.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zipped:
        zipped.writestr('IllustratorTableTool.aip', plugin.read_bytes())
        zipped.writestr('README.md', documentation)
        readme_hash = hashlib.sha256(documentation).hexdigest()
        zipped.writestr('SHA256SUMS.txt', f'{digest}  IllustratorTableTool.aip\n{readme_hash}  README.md\n')
    archive.with_suffix('.zip.sha256').write_text(
        f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n', encoding='ascii')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--plugin', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(build_package(args.plugin, args.output, Path(__file__).resolve().parents[1] / 'README.md'))
