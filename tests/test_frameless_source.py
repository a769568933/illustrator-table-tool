"""Architecture guard; the native artwork audit separately proves object types."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FramelessSourceTests(unittest.TestCase):
    def test_shipping_modules_have_no_native_text_construction_or_editing_route(self):
        forbidden = re.compile(r'AITextFrame|ATESuites|ATE::|SetTextFocus|LoseTextFocus|kTypeTool|NewInPathText|CreateOutline')
        violations = []
        for directory in ('src/plugin', 'src/text'):
            for path in (ROOT / directory).glob('*'):
                if path.suffix not in {'.cpp', '.hpp'}:
                    continue
                for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
                    if forbidden.search(line):
                        violations.append(f'{path.relative_to(ROOT)}:{number}')
        self.assertFalse(violations, 'Forbidden native text routes: ' + ', '.join(violations))


if __name__ == '__main__':
    unittest.main()
