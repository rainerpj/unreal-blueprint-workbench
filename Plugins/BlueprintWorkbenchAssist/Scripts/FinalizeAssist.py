"""Fresh-process saved-asset verification after the UI editor has exited."""
import json
import os
import sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from AssistVerification import verify_blueprint
request=json.loads(Path(os.environ['WORKBENCH_ASSIST_REQUEST']).read_text(encoding='utf-8-sig'))
out=Path(request['output']);asset=request['asset'];graph=request['graph']
saved=json.loads((out/'result.json').read_text(encoding='utf-8'))
assert saved['success'] and saved['saved']
before=json.loads((out/'before.json').read_text(encoding='utf-8'))
expected=json.loads((out/'after.json').read_text(encoding='utf-8'))
lib=unreal.BlueprintWorkbenchLibrary
result=json.loads(lib.inspect_blueprint(asset));assert result['success']
current=result['blueprint'];verified=verify_blueprint(before,current,graph)
for eg in expected['graphs']:
    cg=next(g for g in current['graphs'] if g['path']==eg['path'])
    assert {n['id']:(n['x'],n['y'],n['width'],n['height']) for n in eg['nodes']}=={n['id']:(n['x'],n['y'],n['width'],n['height']) for n in cg['nodes']},'Saved layout mismatch'
if hasattr(lib,'preview_saved_asset_patch'):
    compiled=json.loads(lib.preview_saved_asset_patch(asset,'{"operations":[]}'))
else:
    compiled=json.loads(lib.apply_patch(asset,'{"operations":[]}'))
assert compiled['success'] and compiled['compiled'] and compiled['compile_errors']==0,compiled.get('error')
report=dict(verified,success=True,compile_errors=compiled['compile_errors'],compile_warnings=compiled['compile_warnings'],saved_reload=True)
(out/'reload.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
