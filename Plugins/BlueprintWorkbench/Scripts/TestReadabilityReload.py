"""Independent-process verification of the isolated readability fixture."""
import json
import sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from WorkbenchReadability import verify_readability
root=Path(unreal.Paths.project_dir()).resolve()
assert (root/'WorkbenchHost.uproject').exists()
out=root/'Saved/BlueprintWorkbench'
bundle=json.loads((out/'readability-reload-input.json').read_text(encoding='utf-8'))
saved=bundle['saved'];asset=saved['blueprint']['asset']
current=json.loads(unreal.BlueprintWorkbenchLibrary.inspect_blueprint(asset))
assert current['success']
verify_readability(bundle['before'],dict(saved,blueprint=current['blueprint']),bundle['patch'],bundle['plan'])
compiled=json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch(asset,'{"operations":[]}'))
assert compiled['success'] and compiled['compiled'] and compiled['compile_errors']==0
(out/'readability-reload.json').write_text(json.dumps({'success':True,'saved_graph_semantics_verified':True,
    'fresh_compile_preview':True,'errors':compiled['compile_errors'],'warnings':compiled['compile_warnings']}),encoding='utf-8')
