"""Independent reload and compile verification for CLI-created Blueprint assets."""
import json, os, sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from WorkbenchPresentation import verify_logic
request=json.loads(Path(os.environ['BLUEPRINT_WORKBENCH_REQUEST']).read_text(encoding='utf-8-sig'))
result=json.loads(Path(request['report']).read_text(encoding='utf-8'))
assert result['success'] and result['saved']
asset=result['blueprint']['asset']
loaded=json.loads(unreal.BlueprintWorkbenchLibrary.inspect_blueprint(asset))
assert loaded['success']
verify_logic(result['blueprint'],dict(success=True,compile_errors=0,blueprint=loaded['blueprint']),set())
compiled=json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch(asset,'{"operations":[]}'))
verify_logic(loaded['blueprint'],compiled,set())
Path(request['report']+'.finalize.json').write_text(json.dumps({'success':True,'asset':asset,'independent_reload':True,'compile_errors':compiled['compile_errors'],'compile_warnings':compiled['compile_warnings']},indent=2),encoding='utf-8')
