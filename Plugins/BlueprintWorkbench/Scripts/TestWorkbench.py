"""Isolated UE commandlet integration checks. Only creates a uniquely named test copy."""
import json
import os
from pathlib import Path
import uuid
import sys
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from WorkbenchLifecycle import register_copy

lib = unreal.BlueprintWorkbenchLibrary
root = Path(unreal.Paths.project_dir()).resolve()
report_dir = root / "Saved" / "BlueprintWorkbench"
report_dir.mkdir(parents=True, exist_ok=True)
run_id = uuid.uuid4().hex
package_dir = "/Game/__BlueprintWorkbenchTests/" + run_id
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
source = unreal.AssetToolsHelpers.get_asset_tools().create_asset("BP_Source", package_dir, unreal.Blueprint, factory)
assert source, "Source fixture creation failed"
source_path = source.get_path_name()
seed = json.loads(lib.apply_patch(source_path, json.dumps({'operations':[
    {'op':'add_node','graph':'EventGraph','kind':'custom_event','id':'seed','name':'WorkbenchSeed'}
]}), package_dir+'/BP_Seed'))
assert seed['success'] and seed['saved'], seed
source_path = seed['blueprint']['asset']
baseline = json.loads(lib.inspect_blueprint(source_path))["blueprint"]
checks = []


def check(name, condition):
    if not condition:
        raise AssertionError(name)
    checks.append(name)


def patch(operations, destination=""):
    return json.loads(lib.apply_patch(source_path, json.dumps({"operations": operations}), destination))


def node(kind, alias, **fields):
    return dict(op="add_node", graph="EventGraph", kind=kind, id=alias, **fields)


def link(source_id, source_pin, target_id, target_pin, op="connect"):
    return dict(op=op, graph="EventGraph", **{"from": {"node": source_id, "pin": source_pin}, "to": {"node": target_id, "pin": target_pin}})


ops = [
    {"op": "add_variable", "name": "WorkbenchFlag", "type": "bool"},
    {"op": "add_variable", "name": "RoundIdentity", "type": "struct", "object": "/Script/CoreUObject.Guid"},
    node("custom_event", "start", name="WorkbenchExample"),
    node("branch", "branch"),
    node("get", "get_flag", variable="WorkbenchFlag"),
    node("set", "set_flag", variable="WorkbenchFlag"),
    node("call", "print", owner="/Script/Engine.KismetSystemLibrary", function="PrintString"),
    {"op": "set_default", "graph": "EventGraph", "node": "set_flag", "pin": "WorkbenchFlag", "value": "true"},
    {"op": "set_default", "graph": "EventGraph", "node": "print", "pin": "InString", "value": "Workbench test"},
    link("start", "then", "branch", "execute"),
    link("get_flag", "WorkbenchFlag", "branch", "Condition"),
    link("branch", "else", "set_flag", "execute"),
    link("set_flag", "then", "print", "execute"),
]
preview = patch(ops)
check("preview compiles without saving", preview["success"] and preview["compiled"] and not preview["saved"])
check("preview has no compiler warnings", preview["compile_warnings"] == 0)
check("source graph remains unchanged after preview", json.loads(lib.inspect_blueprint(source_path))["blueprint"] == baseline)
check("member variables created", {v["name"] for v in preview["blueprint"]["variables"]} == {"WorkbenchFlag", "RoundIdentity"})
graph = next(g for g in preview["blueprint"]["graphs"] if g["name"] == "EventGraph")
nodes = {n["id"]: n for n in graph["nodes"]}
setter = nodes[preview["aliases"]["set_flag"]]
check("typed pin default applied", next(p for p in setter["pins"] if p["name"] == "WorkbenchFlag")["default"] == "true")
check("execution link targets intended node", next(p for p in setter["pins"] if p["name"] == "then")["links"][0]["node"] == preview["aliases"]["print"])
bad = patch(ops + [link("get_flag", "WorkbenchFlag", "print", "execute")])
check("incompatible pin types rejected", not bad["success"] and not bad["saved"])
occupied = patch(ops + [link("start", "then", "set_flag", "execute")])
check("implicit link replacement rejected", not occupied["success"])
disconnected = patch(ops + [link("start", "then", "branch", "execute", "disconnect"), link("start", "then", "branch", "execute")])
check("explicit disconnect and reconnect compile", disconnected["success"])
duplicate = patch(ops + [{"op": "add_variable", "name": "WorkbenchFlag", "type": "bool"}])
check("duplicate variable rejected", not duplicate["success"])
check("invalid JSON rejected", not json.loads(lib.apply_patch(source_path, "{bad"))["success"])
check("missing pin rejected", not patch(ops + [{"op": "set_default", "graph": "EventGraph", "node": "set_flag", "pin": "missing", "value": "true"}])["success"])
check("unknown operation rejected", not patch([{"op": "unknown", "graph": "EventGraph"}])["success"])
check("unsupported container rejected", not patch([{"op": "add_variable", "name": "Bad", "type": "bool", "container": "invalid"}])["success"])
check("invalid scalar default rejected", not patch(ops + [{"op": "set_default", "graph": "EventGraph", "node": "set_flag", "pin": "WorkbenchFlag", "value": "not_a_boolean"}])["success"])
types = patch([{"op": "add_variable", "name": "Value_" + t, "type": t} for t in ("int", "float", "double", "string", "name")] + [{"op": "add_variable", "name": "OwnerActor", "type": "object", "object": "/Script/Engine.Actor"}])
check("supported scalar and object variable types compile", types["success"])
check("destination outside Game rejected", not patch([], "/Engine/BP_Test")["success"])
check("source overwrite rejected", not patch([], package_dir + "/BP_Source")["success"])
check("source unchanged after rejected patches", json.loads(lib.inspect_blueprint(source_path))["blueprint"] == baseline)

# Regression: UE duplicates regenerate node GUIDs. Real edits must resolve exported source endpoints.
existing_event = next(n for g in baseline['graphs'] if g['name'] == 'EventGraph' for n in g['nodes'] if n['id'] == seed['aliases']['seed'])
existing_pin = next(p for p in existing_event['pins'] if p['name'] == 'then')
existing_edit = patch([
    node('call', 'existing_print', owner='/Script/Engine.KismetSystemLibrary', function='PrintString'),
    {'op':'connect', 'graph':'EventGraph', 'from':{'node':existing_event['id'], 'pin_id':existing_pin['id']}, 'to':{'node':'existing_print','pin':'execute'}},
])
check('exported existing node GUID and pin GUID resolve in duplicate', existing_edit['success'])
check('existing-source regression leaves source unchanged', json.loads(lib.inspect_blueprint(source_path))['blueprint'] == baseline)
check('invalid break struct rejected', not patch([node('break_struct','bad_struct',struct='/Script/CoreUObject.DoesNotExist')])['success'])
check('native-break struct rejected by generic node', not patch([node('break_struct','native_struct',struct='/Script/CoreUObject.Vector')])['success'])
check('missing external variable owner rejected', not patch([node('get','bad_owner',variable='Hidden',owner='/Script/Engine.DoesNotExist')])['success'])

presented=patch(ops+[
    {'op':'move_node','graph':'EventGraph','node':existing_event['id'],'x':-400,'y':-200},
    node('comment','block',text='Workbench presentation test',details='Preserve execution and data links',width=1000,height=700,members=['start','branch','set_flag']),
])
check('move and grouped comment compile',presented['success'] and presented['compile_warnings']==0)
pnodes={n['id']:n for g in presented['blueprint']['graphs'] for n in g['nodes']}
moved=pnodes[presented['source_nodes']['EventGraph|'+existing_event['id']]]
check('source-guid node moved to requested coordinates',moved['x']==-400 and moved['y']==-200)
comment=pnodes[presented['aliases']['block']]
check('comment text details and bounds exported',comment['comment']=='Workbench presentation test' and comment['details']=='Preserve execution and data links' and comment['width']==1000 and comment['height']==700)
check('live execution node cannot be removed',not patch(ops+[{'op':'remove_unused_nodes','graph':'EventGraph','nodes':['set_flag']}])['success'])
check('entry event cannot be removed',not patch(ops+[{'op':'remove_unused_nodes','graph':'EventGraph','nodes':['start']}])['success'])
check('pure producer with live consumer cannot be removed',not patch(ops+[{'op':'remove_unused_nodes','graph':'EventGraph','nodes':['get_flag']}])['success'])
dead=ops+[node('branch','dead_branch'),node('call','dead_print',owner='/Script/Engine.KismetSystemLibrary',function='PrintString'),link('get_flag','WorkbenchFlag','dead_branch','Condition'),link('dead_branch','then','dead_print','execute')]
pruned=patch(dead+[{'op':'remove_unused_nodes','graph':'EventGraph','nodes':['dead_branch','dead_print']}])
check('disconnected old chain removed while shared data producer survives',pruned['success'] and 'dead_branch' not in pruned['aliases'] and 'get_flag' in pruned['aliases'])
check('invalid comment bounds rejected',not patch([node('comment','bad_comment',text='bad',width=-1,height=100)])['success'])

# Retained generic extensions are checked against engine-owned reflected types.
signature = patch([{'op':'signature','name':'ComputeFlag','parameters':[
    {'name':'Flag','type':'bool','direction':'input'},
    {'name':'Result','type':'bool','direction':'output'},
    {'name':'Temporary','type':'bool','direction':'local'}]}])
check('generic function signature compiles with input output and local', signature['success'])
check('signature graph exports typed entry and return pins', any(
    p['name']=='Result' and p['type']['category']=='bool'
    for g in signature['blueprint']['graphs'] if g['name']=='ComputeFlag' for n in g['nodes'] for p in n['pins']))
component = patch([{'op':'add_component','name':'WorkbenchProbe','class':'/Script/Engine.BoxComponent'},
    node('component_event','overlap',variable='WorkbenchProbe',name='OnComponentBeginOverlap')])
check('generic component and bound overlap event compile', component['success'])
check('invalid component class rejected', not patch([{'op':'add_component','name':'Bad','class':'/Script/Engine.Actor'}])['success'])
check('explicit call refresh compiles', patch(ops+[{'op':'refresh_calls','functions':['PrintString']}])['success'])
check('unscoped call refresh rejected', not patch([{'op':'refresh_calls'}])['success'])
map_result = patch([{'op':'add_variable','name':'Identities','type':'name','container':'map','value_object':'/Script/CoreUObject.Guid'}])
check('map member retains key and struct value reflection', map_result['success'] and map_result['blueprint']['variables'][0]['type']['container']==3)

# Local defaults, editor-callable functions/events and native Make Array.
feature_ops = [
    {'op':'signature','name':'EditorUtility','parameters':[
        {'name':'Enabled','type':'bool','direction':'local','default':'false'},
        {'name':'Amount','type':'float','direction':'local','default':'2.5'},
        {'name':'Label','type':'string','direction':'local','default':'Workbench'},
        {'name':'Count','type':'int','direction':'local','default':'7'},
        {'name':'Offset','type':'struct','object':'/Script/CoreUObject.Vector','direction':'local','default':'(X=1,Y=2,Z=3)'}]},
    {'op':'set_local_default','graph':'EditorUtility','variable':'Enabled','value':'true'},
    {'op':'set_call_in_editor','graph':'EditorUtility','value':True},
    {'op':'set_call_in_editor','graph':'EventGraph','node':'start','value':True},
    node('make_array','bool_array',num_inputs=2),
    link('get_flag','WorkbenchFlag','bool_array','[0]'),
    {'op':'set_default','graph':'EventGraph','node':'bool_array','pin':'[1]','value':'true'},
]
features = patch(ops + feature_ops)
check('local defaults editor flags and Make Array compile', features['success'] and features['compile_warnings']==0)
from TestWorkbenchFeatures import verify_features
verify_features(features['blueprint'])
check('inspection exports local values editor flags and typed array pins', True)
for label, operation in [
    ('missing local', {'op':'set_local_default','graph':'EditorUtility','variable':'Missing','value':'true'}),
    ('invalid local bool', {'op':'set_local_default','graph':'EditorUtility','variable':'Enabled','value':'typo'}),
    ('non-string local default', {'op':'set_local_default','graph':'EditorUtility','variable':'Enabled','value':True}),
    ('invalid editor flag', {'op':'set_call_in_editor','graph':'EditorUtility','value':'true'}),
    ('editor flag on ordinary node', {'op':'set_call_in_editor','graph':'EventGraph','node':'get_flag','value':True}),
    ('negative array count', node('make_array','bad_array',num_inputs=-1)),
    ('string array count', node('make_array','bad_array',num_inputs='2')),
    ('fractional array count', node('make_array','bad_array',num_inputs=1.5)),
    ('oversized array count', node('make_array','bad_array',num_inputs=1025)),
    ('non-string initial default', {'op':'signature','name':'BadDefault','parameters':[{'name':'L','type':'bool','direction':'local','default':True}]}),
    ('output declaration default unsupported', {'op':'signature','name':'BadInput','parameters':[{'name':'L','type':'bool','direction':'output','default':'true'}]}),
]:
    check(label + ' rejected without saving', not patch(ops + feature_ops + [operation])['success'])
disabled = patch(ops + feature_ops + [
    {'op':'set_call_in_editor','graph':'EditorUtility','value':False},
    {'op':'set_call_in_editor','graph':'EventGraph','node':'start','value':False}])
check('editor flags can be unchecked', disabled['success'] and all(not n.get('call_in_editor',False) for g in disabled['blueprint']['graphs'] for n in g['nodes']))
check('feature previews preserve source', json.loads(lib.inspect_blueprint(source_path))['blueprint'] == baseline)
ops.extend(feature_ops)

# Engine-owned parent contracts: no game module or fixture native class required.
factory.set_editor_property("parent_class", unreal.UserWidget)
widget = unreal.AssetToolsHelpers.get_asset_tools().create_asset("WBP_Source", package_dir, unreal.WidgetBlueprint, unreal.WidgetBlueprintFactory())
check("Widget Blueprint fixture created", widget is not None)
widget_path = widget.get_path_name()
widget_baseline = json.loads(lib.inspect_blueprint(widget_path))["blueprint"]
widget_ops = [{"op":"add_variable","name":"WidgetFlag","type":"bool"},
              {"op":"add_override","name":"IsInteractable"},
              {"op":"add_override","name":"Destruct","graph":"EventGraph"}]
overrides = json.loads(lib.apply_patch(widget_path, json.dumps({"operations":widget_ops})))
check("Widget return-valued and event overrides compile: " + json.dumps(overrides.get("error")), overrides["success"])
check("override function exposes entry and return", len(next(g for g in overrides["blueprint"]["graphs"] if g["name"] == "IsInteractable")["nodes"]) >= 2)
check("Widget preview preserves source including Designer", json.loads(lib.inspect_blueprint(widget_path))["blueprint"] == widget_baseline)
widget_destination = package_dir + "/WBP_Patched"
widget_saved = json.loads(lib.apply_patch(widget_path, json.dumps({"operations":widget_ops}), widget_destination))
check("Widget logic copy compiles and saves without warnings", widget_saved["success"] and widget_saved["saved"] and widget_saved["compile_warnings"] == 0)
check("project-only HUD operation rejected", not patch([{"op":"hud_buttons"}])["success"])

ops.append(node('comment','saved_comment',text='Persistent comment',details='Saved details: 中文说明',width=700,height=300))
destination = package_dir + "/BP_Patched"
rejected_save = patch(ops + [link("start", "then", "set_flag", "execute")], destination)
check("failed patch does not save destination", not rejected_save["success"] and not rejected_save["saved"])
saved = patch(ops, destination)
check("new copy saved after compilation, including retry after rejected patch", saved["success"] and saved["saved"])
check("saved destination cannot be overwritten", not patch([], destination)["success"])
fixture_file = root / "Content" / "__BlueprintWorkbenchTests" / run_id / "BP_Patched.uasset"
check("saved package exists", fixture_file.is_file())
bad_write = json.loads(lib.apply_patch_to_saved_asset(destination, '{"operations":[{"op":"unsupported"}]}'))
check('writeback preflight failure does not save or create backup', not bad_write['success'] and not bad_write['saved'] and 'backup_file' not in bad_write)
write_patch = {'operations':[{'op':'set_default','graph':'EventGraph','node':saved['aliases']['set_flag'],'pin':'WorkbenchFlag','value':'false'},
    {'op':'set_local_default','graph':'EditorUtility','variable':'Count','value':'9'}]}
temporary = '/Game/Workbench/TemporaryTest_'+run_id+'/BP_Review'
temporary_result = json.loads(lib.apply_patch(destination,json.dumps(write_patch),temporary))
check('temporary review fixture saved', temporary_result['success'] and temporary_result['saved'])
receipt = register_copy(destination,write_patch,temporary_result)
write_result = json.loads(lib.apply_patch_to_saved_asset(destination,json.dumps(write_patch)))
check('commandlet writeback compiles and saves an existing fixture', write_result['success'] and write_result['saved'] and write_result['compile_errors']==0)
check('writeback retains original package identity', write_result['blueprint']['asset']==saved['blueprint']['asset'])
check('writeback creates an existing original-asset backup', Path(write_result['backup_file']).is_file())
write_node = next(n for g in write_result['blueprint']['graphs'] for n in g['nodes'] if n['id']==saved['aliases']['set_flag'])
check('writeback updates the requested existing pin', next(p['default'] for p in write_node['pins'] if p['name']=='WorkbenchFlag')=='false')
manifest = {"comment_node":saved["aliases"]["saved_comment"], "widget_destination":widget_destination, "checks": checks, "destination": destination, "fixture_file": str(fixture_file), "source": source_path, "run_id": run_id, 'writeback_node':saved['aliases']['set_flag'], 'writeback_backup':write_result['backup_file'], 'temporary':temporary, 'receipt':receipt, 'write_patch':write_patch, 'write_result':write_result}
(report_dir / "test-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
unreal.log("BLUEPRINT_WORKBENCH_TESTS_PASSED " + str(len(checks)))
