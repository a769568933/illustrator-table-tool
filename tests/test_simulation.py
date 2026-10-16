"""Exercise the real simulator executable through its local-server boundary."""
import importlib.util
import os
from pathlib import Path
import random
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('simulation_server', ROOT / 'scripts/simulation_server.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


@unittest.skipUnless(os.environ.get('TABLE_SIMULATOR'), 'Requires compiled table_simulator')
class SimulationTests(unittest.TestCase):
    def test_repeated_sidebar_actions_preserve_valid_saved_tables(self):
        executable = Path(os.environ['TABLE_SIMULATOR'])
        rng = random.Random(20261016)
        operations = 0
        for scenario in range(8):
            model = server.simulate(executable, {'action': 'create', 'rows': 4, 'columns': 4})
            model = server.simulate(executable, {'action': 'sample', 'state': model['state']})
            for step in range(60):
                r, c = rng.randrange(model['rows']), rng.randrange(model['columns'])
                end_r, end_c = rng.randrange(model['rows']), rng.randrange(model['columns'])
                selected = server.simulate(executable, {'action': 'select', 'state': model['state'],
                    'selection': [r, c, end_r, end_c]})['selection']
                command = rng.randrange(8)
                if command in (2, 3) and model['rows'] >= 12:
                    command = 1
                if command in (4, 5) and model['columns'] >= 12:
                    command = 1
                if command == 6 and selected[2]-selected[0]+1 == model['rows']:
                    command = 1
                if command == 7 and selected[3]-selected[1]+1 == model['columns']:
                    command = 1
                with self.subTest(scenario=scenario, step=step, command=command):
                    model = server.simulate(executable, {'action': 'command', 'state': model['state'],
                        'selection': selected, 'command': command})
                    restored = server.simulate(executable, {'action': 'inspect', 'state': model['state']})
                    self.assertEqual(restored['state'], model['state'])
                    self.assertLessEqual(len(model['cells']), model['rows']*model['columns'])
                    operations += 1
        self.assertEqual(operations, 480)


if __name__ == '__main__':
    unittest.main()
