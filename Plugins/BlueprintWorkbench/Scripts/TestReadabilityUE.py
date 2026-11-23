"""Isolated-host tests only. Never target a production project."""
import json
import sys
import uuid
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from WorkbenchReadability import plan_readability, verify_readability

root=Path(unreal.Paths.project_dir()).resolve()
assert (root/'WorkbenchHost.uproject').exists(), 'Requires isolated WorkbenchHost'
lib=unreal.BlueprintWorkbenchLibrary
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.Actor)
folder='/Game/__ReadabilityTests/'+uuid.uuid4().hex
source=unreal.AssetToolsHelpers.get_asset_tools().create_asset('Source',folder,unreal.Blueprint,factory)
def add(kind,alias,**kwargs):return dict(op='add_node',graph='EventGraph',kind=kind,id=alias,**kwargs)
def link(a,p,b,q):return {'op':'connect','graph':'EventGraph','from':{'node':a,'pin':p},'to':{'node':b,'pin':q}}
seed=json.loads(lib.apply_patch(source.get_path_name(),json.dumps({'operations':[
    {'op':'add_variable','name':'Flag','type':'bool'},
    add('get','read',variable='Flag',x=0,y=0),
    add('branch','near',x=600,y=0),add('branch','far',x=600,y=4000),
    add('custom_event','event',name='TestRead',x=-600,y=0),
    add('self','self',x=-600,y=-600),
    link('read','Flag','near','Condition'),link('read','Flag','far','Condition'),
    link('event','then','near','execute'),link('near','then','far','execute')
]}),folder+'/Fixture'))
assert seed['success'] and seed['saved'],seed
before=seed['blueprint'];asset=before['asset'];graph=next(g for g in before['graphs'] if g['path']=='EventGraph')
read=next(n for n in graph['nodes'] if n['id']==seed['aliases']['read'])
assert read['localizable_read']
patch,report=plan_readability(graph)
assert len(report['copies'])==1
preview=json.loads(lib.apply_patch(asset,json.dumps(patch)))
assert preview['success'], preview.get('error')
verify_readability(before,preview,patch,report)
# The engine also checks eligibility independently of planner/export claims.
rejected=json.loads(lib.apply_patch(asset,json.dumps({'operations':[add('local_read','bad',source_node=seed['aliases']['near'])]})))
assert not rejected['success'] and not rejected['saved']
self_copy=json.loads(lib.apply_patch(asset,json.dumps({'operations':[add('local_read','self_copy',source_node=seed['aliases']['self'])]})))
assert self_copy['success'] and self_copy['compiled']
saved=json.loads(lib.apply_patch(asset,json.dumps(patch),folder+'/Localized'))
assert saved['success'] and saved['saved']
verify_readability(before,saved,patch,report)
out=root/'Saved/BlueprintWorkbench';out.mkdir(parents=True,exist_ok=True)
(out/'readability-reload-input.json').write_text(json.dumps({'before':before,'saved':saved,'patch':patch,'plan':report}),encoding='utf-8')
(out/'readability-integration.json').write_text(json.dumps({'success':True,'copies':report['copies'],
    'compiled':preview['compiled'],'errors':preview['compile_errors'],'warnings':preview['compile_warnings'],
    'unsafe_clone_rejected':True,'self_clone_compiled':True,'before':report['before'],'after':report['after']},indent=2),encoding='utf-8')
print('READABILITY_INTEGRATION_PASSED')
