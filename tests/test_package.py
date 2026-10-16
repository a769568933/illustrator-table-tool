import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_plugin', ROOT / 'scripts/package_plugin.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def test_missing_or_disguised_plugin_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'fake.aip'
            with self.assertRaises(ValueError):
                package.validate_plugin(target)
            target.write_text('This is not a native plugin')
            with self.assertRaises(ValueError):
                package.validate_plugin(target)

    @unittest.skipUnless(os.environ.get('TABLE_TEST_PLUGIN'), 'Requires a compiled native plugin')
    def test_real_plugin_package_contains_only_deliverables(self):
        with tempfile.TemporaryDirectory() as folder:
            archive = package.build_package(Path(os.environ['TABLE_TEST_PLUGIN']), Path(folder), ROOT / 'README.md')
            with zipfile.ZipFile(archive) as zipped:
                self.assertEqual(set(zipped.namelist()), {'IllustratorTableTool.aip', 'README.md', 'SHA256SUMS.txt'})
                self.assertEqual(zipped.read('IllustratorTableTool.aip'), Path(os.environ['TABLE_TEST_PLUGIN']).read_bytes())
                self.assertIn(b'IllustratorTableTool.aip', zipped.read('SHA256SUMS.txt'))

    @unittest.skipUnless(os.environ.get('TABLE_TEST_PLUGIN'), 'Requires a compiled native plugin')
    def test_wrong_architecture_is_rejected(self):
        import struct
        data = bytearray(Path(os.environ['TABLE_TEST_PLUGIN']).read_bytes())
        pe_offset = struct.unpack_from('<I', data, 0x3c)[0]
        struct.pack_into('<H', data, pe_offset + 4, 0x14c)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'x86.aip'
            target.write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'x64'):
                package.validate_plugin(target)


if __name__ == '__main__':
    unittest.main()
