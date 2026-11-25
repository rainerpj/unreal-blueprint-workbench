"""UE Python entry point; request file supplied by Invoke-BlueprintWorkbench.ps1."""
import json
import os
import sys
from pathlib import Path
import unreal
sys.path.insert(0, str(Path(__file__).resolve().parent))
from WorkbenchLifecycle import register_copy, validate_temporary
from WorkbenchPresentation import plan, verify_logic, verify_presentation
from WorkbenchCrossGroup import compact
from WorkbenchReadability import plan_readability, verify_readability

request = json.loads(Path(os.environ["BLUEPRINT_WORKBENCH_REQUEST"]).read_text(encoding="utf-8-sig"))
library = unreal.BlueprintWorkbenchLibrary
if request.get('presentation'):
    before=json.loads(library.inspect_blueprint(request['asset']))['blueprint']
    spec=request['presentation']
    graph=next(g for g in before['graphs'] if g['path']==spec['graph'])
    if spec.get('mode') == 'readability':
        payload,description=plan_readability(graph,spec.get('threshold',1200),spec.get('max_copies',32))
    elif spec.get('mode') == 'cross_group':
        payload,description=compact(graph,spec.get('groups'),spec.get('movable'),spec.get('threshold',3000))
    else:
        payload,description=plan(graph,spec['groups'],spec.get('candidates',[]))
    request['patch']=payload
    # Persist the concrete plan for independent finalization and review.
    Path(os.environ['BLUEPRINT_WORKBENCH_REQUEST']).write_text(json.dumps(request,ensure_ascii=False,indent=2),encoding='utf-8')
    Path(request['report']+'.plan.json').write_text(json.dumps(description,ensure_ascii=False,indent=2),encoding='utf-8')
    preview=json.loads(library.apply_patch(request['asset'],json.dumps(payload)))
    if description.get('readability'):
        verify_readability(before,preview,payload,description)
    else:
        verify_logic(before,preview,set(description['removed']))
        verify_presentation(before,preview,payload,description)
if request["action"] == "create":
    result = library.create_blueprint(request['kind'], request.get('parent','/Script/Engine.Actor'), json.dumps(request.get('patch',{'operations':[]})), request.get('destination',''))
elif request["action"] == "references":
    result = library.find_member_references(request['asset'], request['member'])
elif request["action"] == "inspect":
    result = library.inspect_blueprint(request["asset"])
elif request["action"] == "patch":
    if request.get('temporary_copy'):
        validate_temporary(request['asset'], request['destination'])
    result = library.apply_patch(request["asset"], json.dumps(request["patch"]), request.get("destination", ""))
elif request["action"] == "writeback":
    result = library.apply_patch_to_saved_asset(request["asset"], json.dumps(request["patch"]))
else:
    raise ValueError("Unsupported action")
Path(request["report"]).write_text(result, encoding="utf-8")
parsed = json.loads(result)
unreal.log("BLUEPRINT_WORKBENCH_RESULT " + json.dumps({k: parsed.get(k) for k in ("success", "compiled", "saved", "error")}))
if not parsed["success"]:
    raise RuntimeError(parsed.get("error", "Blueprint Workbench failed"))
if request.get('presentation'):
    if description.get('readability'):
        metrics=verify_readability(before,parsed,request['patch'],description)
    else:
        verify_logic(before,parsed,set(description['removed']))
        metrics=verify_presentation(before,parsed,request['patch'],description)
    Path(request['report']+'.presentation.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
if request.get('temporary_copy'):
    register_copy(request['asset'], request['patch'], parsed)
