"""Assertions shared by the creation and independent-process reload checks."""
def verify_features(blueprint, count="7"):
    function = next(g for g in blueprint['graphs'] if g['name']=='EditorUtility')
    entry = next(n for n in function['nodes'] if 'local_variables' in n)
    assert entry['call_in_editor'] is True, entry
    locals_ = {v['name']: v['default'] for v in entry['local_variables']}
    assert locals_['Enabled']=='true', locals_
    assert float(locals_['Amount'])==2.5 and locals_['Label']=='Workbench' and locals_['Count']==count, locals_
    events = next(g for g in blueprint['graphs'] if g['name']=='EventGraph')
    assert any(n.get('call_in_editor') is True for n in events['nodes'])
    array = next(n for n in events['nodes'] if n['class'].endswith('K2Node_MakeArray'))
    pins = {p['name']:p for p in array['pins']}
    assert len(pins)==3 and pins['Array']['type']['container']==1, pins
    assert all(p['type']['category']=='bool' for p in pins.values()), pins
    assert pins['[0]']['links'] and pins['[1]']['default']=='true', pins
