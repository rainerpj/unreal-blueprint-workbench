"""New-process checks of advanced capability fixtures; no gameplay assets touched."""
import json,sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from WorkbenchPresentation import verify_logic
out=Path(unreal.Paths.project_saved_dir())/'BlueprintWorkbench'
m=json.loads((out/'advanced-manifest.json').read_text(encoding='utf-8'))
lib=unreal.BlueprintWorkbenchLibrary
for key,result_key in [('actor','actor_result'),('library','library_result'),('interface','interface_result')]:
    r=json.loads(lib.inspect_blueprint(m[key]));assert r['success'],r
    (out/('advanced-reloaded-'+key+'.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
    verify_logic(m[result_key]['blueprint'],dict(success=True,compile_errors=0,blueprint=r['blueprint']),set())
    preview=json.loads(lib.apply_patch(m[key],'{"operations":[]}'))
    (out/('advanced-preview-'+key+'.json')).write_text(json.dumps(preview,indent=2),encoding='utf-8')
    verify_logic(r['blueprint'],preview,set())
    m['checks'].append(key+' independently reloads and recompiles with types defaults metadata and links preserved')
a=unreal.get_default_object(unreal.load_object(None,m['actor']+'_C'))
assert list(a.get_editor_property('InventoryNumbers'))==[4,5]
assert a.get_editor_property('initial_life_span')==12.5
assert a.call_method('ReadLocal')==21
assert a.call_method('TestMessage')==17
value=a.call_method('MutateProfile')
assert value.weight==0.75 and value.object is None
m['checks'].append('supported Set Members mutation survives independent reload')
b=unreal.get_default_object(unreal.load_object(None,m['library']+'_C'))
assert b.call_method('Compute',kwargs={'input':12,'world_context':None})==42
m['checks'].append('generated CDO and function behavior survive independent reload')
# Delete only the three uniquely named fixtures recorded by this run after verification.
for key,name in [('actor','BP_Test'),('library','BFL_Test'),('interface','BPI_Test')]:
    package=m[key].split('.')[0]
    assert package==m['base']+'/'+name and m['base']=='/Game/__BlueprintWorkbenchTests/'+m['run_id']
    assert unreal.EditorAssetLibrary.delete_asset(package)
m['checks'].append('only this run generated fixture assets removed')
m['success']=True
(out/'advanced-results.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
unreal.log('ADVANCED_TESTS_PASSED '+str(len(m['checks'])))
