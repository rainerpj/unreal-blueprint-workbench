"""Run with UnrealEditor-Cmd -run=pythonscript -script=...; transient fixtures only."""
import json
from pathlib import Path
import unreal as u

w = u.BlueprintWorkbenchLibrary
reports = []

def check(tag, raw, expected=True):
    result = json.loads(raw)
    reports.append({"case": tag, **result})
    assert result["success"] == expected, (tag, result)
    assert not result.get("saved", False), (tag, "preview saved unexpectedly")
    assert not result.get("compile_errors", 0), (tag, result)

def blueprint(tag, operations, expected=True, kind="widget"):
    check(tag, w.create_blueprint(kind, "", json.dumps({"operations": operations}), ""), expected)

check("duplicate enum", w.create_schema_asset("enum", '{"values":["A","A"]}', ""), False)
check("enum preview", w.create_schema_asset("enum", '{"values":["Item","Document"]}', ""))
check("invalid struct type", w.create_schema_asset("struct", '{"fields":[{"name":"X","type":"bogus"}]}', ""), False)
check("struct preview", w.create_schema_asset("struct", '{"fields":[{"name":"Id","type":"name"},{"name":"N","type":"int","default":"1"}]}', ""))
root = {"op": "add_widget", "name": "Root", "class": "/Script/UMG.SizeBox"}
child = {"op": "add_widget", "name": "Text", "class": "/Script/UMG.TextBlock", "parent": "Root"}
blueprint("widget property", [root, child, {"op": "set_widget_property", "name": "Text", "property": "Text", "value": "test"}])
blueprint("missing parent", [child], False)
blueprint("occupied single child", [root, child, {**child, "name": "Second"}], False)
blueprint("duplicate widget", [root, root], False)
blueprint("invalid property", [root, {"op": "set_widget_property", "name": "Root", "property": "NoSuchProperty", "value": "1"}], False)
blueprint("variable metadata", [{"op": "add_variable", "name": "Flag", "type": "bool"}, {"op": "variable_metadata", "name": "Flag", "private": False, "expose_on_spawn": True, "tooltip": "fixture"}])
blueprint("void return", [{"op": "signature", "name": "Probe", "parameters": []}, {"op": "add_node", "graph": "Probe", "id": "early", "kind": "return"}])
blueprint("return event rejected", [{"op": "add_node", "graph": "EventGraph", "id": "bad", "kind": "return"}], False)
blueprint("wrong subsystem class", [{"op": "add_node", "graph": "EventGraph", "id": "bad", "kind": "get_subsystem", "owner": "/Script/Engine.Actor"}], False)
blueprint("wrong widget class", [{"op": "add_node", "graph": "EventGraph", "id": "bad", "kind": "create_widget", "owner": "/Script/Engine.Actor"}], False)
blueprint("format text", [{"op": "add_node", "graph": "EventGraph", "id": "fmt", "kind": "format_text", "text": "Result: {Value}"}])
out = Path(u.Paths.project_saved_dir()) / "BlueprintWorkbench" / "schema-widget-tests.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps({"success": True, "count": len(reports), "tests": reports}, ensure_ascii=False, indent=2), encoding="utf-8")
