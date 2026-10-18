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
    def test_edit_chinese_selection_and_roundtrip(self):
        executable=Path(os.environ['TABLE_SIMULATOR'])
        state=server.simulate(executable,{'action':'create','rows':1,'columns':1})
        state=server.simulate(executable,{'action':'edit','state':state['state'],'row':0,'column':0,'anchor':0,'caret':0,'text':'中文\n123'})
        self.assertEqual(state['cells'][0]['text'],'中文\n123')
        state=server.simulate(executable,{'action':'edit','state':state['state'],'row':0,'column':0,'anchor':2,'caret':0,'text':'表格'})
        self.assertEqual(state['cells'][0]['text'],'表格\n123')
        self.assertEqual(state['editor']['caret'],2)
        restored=server.simulate(executable,{'action':'inspect','state':state['state']})
        self.assertEqual(restored['cells'][0]['text'],'表格\n123')
        with self.assertRaises(ValueError):
            server.simulate(executable,{'action':'edit','state':state['state'],'row':0,'column':0,'anchor':0,'caret':0,'text':'\x00'})
        self.assertEqual(server.simulate(executable,{'action':'inspect','state':state['state']})['state'],state['state'])

    def test_delete_unicode_clusters(self):
        executable=Path(os.environ['TABLE_SIMULATOR'])
        state=server.simulate(executable,{'action':'create','rows':1,'columns':1})
        state=server.simulate(executable,{'action':'edit','state':state['state'],'row':0,'column':0,'anchor':0,'caret':0,'text':'A😀é'})
        state=server.simulate(executable,{'action':'deleteCluster','state':state['state'],'row':0,'column':0,'anchor':5,'caret':5,'backward':True,'boundaries':[0,1,3,5]})
        self.assertEqual(state['cells'][0]['text'],'A😀')
        state=server.simulate(executable,{'action':'deleteCluster','state':state['state'],'row':0,'column':0,'anchor':3,'caret':3,'backward':True,'boundaries':[0,1,3]})
        self.assertEqual(state['cells'][0]['text'],'A')

    def test_tab_new_row_and_limits(self):
        executable=Path(os.environ['TABLE_SIMULATOR'])
        state=server.simulate(executable,{'action':'create','rows':2,'columns':2})
        state=server.simulate(executable,{'action':'command','state':state['state'],'selection':[0,0,0,1],'command':0})
        next_cell=server.simulate(executable,{'action':'tab','state':state['state'],'row':0,'column':0,'anchor':0,'caret':0,'backward':False})
        expected=next(c['id'] for c in next_cell['cells'] if c['row']==1 and c['column']==0)
        self.assertEqual(next_cell['editor']['cellId'],expected)
        grown=server.simulate(executable,{'action':'tab','state':state['state'],'row':1,'column':1,'anchor':0,'caret':0,'backward':False})
        self.assertEqual(grown['rows'],3)
        maximum=server.simulate(executable,{'action':'create','rows':100,'columns':1})
        with self.assertRaises(ValueError):
            server.simulate(executable,{'action':'tab','state':maximum['state'],'row':99,'column':0,'anchor':0,'caret':0})

    def test_edit_merge_split_formats(self):
        executable=Path(os.environ['TABLE_SIMULATOR'])
        state=server.simulate(executable,{'action':'create','rows':1,'columns':2})
        for column,text in [(0,'甲'),(1,'乙')]:
            state=server.simulate(executable,{'action':'edit','state':state['state'],'row':0,'column':column,'anchor':0,'caret':0,'text':text})
        state=server.simulate(executable,{'action':'style','state':state['state'],'selection':[0,1,0,1]})
        state=server.simulate(executable,{'action':'command','state':state['state'],'selection':[0,0,0,1],'command':0})
        self.assertEqual(state['cells'][0]['text'],'甲\n乙')
        restored=server.simulate(executable,{'action':'inspect','state':state['state']})
        state=server.simulate(executable,{'action':'command','state':restored['state'],'selection':[0,0,0,1],'command':1})
        self.assertEqual([(c['text'],c['size']) for c in state['cells']],[('甲',12),('乙',18)])

    def test_drag_select_cells_without_modifying_model(self):
        executable=Path(os.environ['TABLE_SIMULATOR'])
        state=server.simulate(executable,{'action':'create','rows':4,'columns':4})
        state=server.simulate(executable,{'action':'command','state':state['state'],'selection':[1,1,2,2],'command':0})
        selected=server.simulate(executable,{'action':'dragSelect','state':state['state'],'selection':[0,0,1,1]})
        self.assertEqual(selected['selection'],[0,0,2,2])
        self.assertEqual(selected['state'],state['state'])
        selected=server.simulate(executable,{'action':'dragSelect','state':state['state'],'selection':[3,3,0,0]})
        self.assertEqual(selected['selection'],[0,0,3,3])
        self.assertEqual(selected['state'],state['state'])

    def test_distribute_commands_preserve_merged_table(self):
        executable=Path(os.environ['TABLE_SIMULATOR'])
        state=server.simulate(executable,{'action':'create','rows':3,'columns':3})
        state=server.simulate(executable,{'action':'sample','state':state['state']})
        state=server.simulate(executable,{'action':'command','state':state['state'],'selection':[0,0,1,1],'command':0})
        before=[(c['id'],c['text'],c['rowSpan'],c['columnSpan']) for c in state['cells']]
        for command in (8,9):
            next_state=server.simulate(executable,{'action':'command','state':state['state'],'selection':[0,0,2,2],'command':command})
            self.assertAlmostEqual(next_state['width'],state['width'],places=6)
            self.assertAlmostEqual(next_state['height'],state['height'],places=6)
            self.assertEqual([(c['id'],c['text'],c['rowSpan'],c['columnSpan']) for c in next_state['cells']],before)
            state=next_state
        self.assertEqual(server.simulate(executable,{'action':'inspect','state':state['state']})['state'],state['state'])

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
