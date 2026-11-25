"""Create a unique disposable function for precompiled bridge smoke tests."""
import json, uuid
from pathlib import Path
import unreal

lib = unreal.BlueprintWorkbenchLibrary
asset = '/Game/__WorkbenchAssistTests/' + uuid.uuid4().hex + '/BP_Formatting'
def connect(a, ap, b, bp):
    return dict(op='connect', graph='Compute', **{'from': {'node': a, 'pin': ap}, 'to': {'node': b, 'pin': bp}})
ops = [
    dict(op='signature', name='Compute', entry_id='entry', result_id='result', parameters=[
        dict(name='Value', type='int', direction='input'), dict(name='Result', type='int', direction='output')]),
    dict(op='add_node', graph='Compute', kind='call', id='multiply', owner='/Script/Engine.KismetMathLibrary', function='Multiply_IntInt'),
    dict(op='set_default', graph='Compute', node='multiply', pin='B', value='2'),
    connect('entry', 'Value', 'multiply', 'A'), connect('multiply', 'ReturnValue', 'result', 'Result'),
    connect('entry', 'then', 'result', 'execute'),
    dict(op='move_node', graph='Compute', node='entry', x=800, y=0),
    dict(op='move_node', graph='Compute', node='multiply', x=-500, y=700),
    dict(op='move_node', graph='Compute', node='result', x=0, y=-400),
]
r = json.loads(lib.create_blueprint('normal', '/Script/Engine.Actor', json.dumps({'operations': ops}), asset))
assert r['success'] and r['saved'], r
graph = next(g for g in r['blueprint']['graphs'] if g['name'] == 'Compute')
out = Path(unreal.Paths.project_saved_dir()) / 'BlueprintWorkbench'
out.mkdir(exist_ok=True)
(out/'assist-fixture.json').write_text(json.dumps({'asset': r['blueprint']['asset'], 'graph': graph['path']}))
assert unreal.get_default_object(unreal.load_object(None, asset + '.BP_Formatting_C')).call_method('Compute', kwargs={'value': 7}) == 14
unreal.log('ASSIST_FIXTURE_CREATED')
