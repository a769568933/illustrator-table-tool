import importlib.util
import json
import pathlib
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PiplTests(unittest.TestCase):
    def test_windows_resource_has_real_entry_and_no_mac_entry(self):
        module_path = ROOT / 'scripts' / 'generate_pipl.py'
        self.assertTrue(module_path.exists(), 'PiPL generator is not implemented')
        spec = importlib.util.spec_from_file_location('pipl', module_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        template = {
            'kind': {'vendor': 'ADBE', 'value': 'SPEA'},
            'ivrs': {'vendor': 'ADBE', 'value': 2},
            'mac': {'mi32': {'vendor': 'ADBE', 'value': ''}},
            'win': {'wx86': {'vendor': 'ADBE', 'value': ''}},
            'pinm': {'vendor': 'ADBE', 'value': 'Sample Plugin'},
            'StsP': {'vendor': 'ADBE'},
        }
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'template.json'
            output = pathlib.Path(tmp) / 'plugin.pipl'
            source.write_text(json.dumps(template), encoding='utf-8')
            module.generate(source, output, 'IllustratorTableTool')
            data = output.read_bytes()
        self.assertEqual(struct.unpack_from('>III', data), (1, 0, 4))
        properties = {}
        offset = 12
        for _ in range(4):
            vendor, key, prop_id, length = struct.unpack_from('>4s4sII', data, offset)
            self.assertEqual(vendor, b'ADBE')
            self.assertEqual(prop_id, 0)
            self.assertEqual(length % 4, 0)
            properties[key] = data[offset + 16:offset + 16 + length]
            offset += 16 + length
        self.assertEqual(offset, len(data))
        self.assertEqual(properties[b'kind'], b'SPEA')
        self.assertEqual(properties[b'ivrs'], b'\x00\x00\x00\x02')
        self.assertEqual(properties[b'wx86'].rstrip(b'\0'), b'PluginMain')
        self.assertEqual(properties[b'pinm'].rstrip(b'\0'), b'IllustratorTableTool')
        self.assertNotIn(b'mi32', properties)


if __name__ == '__main__':
    unittest.main()
