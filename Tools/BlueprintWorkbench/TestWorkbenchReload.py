"""Separate-process round-trip check. The PowerShell runner cleans up after UE releases its file handles."""
import json
import sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from WorkbenchLifecycle import finalize

root = Path(unreal.Paths.project_dir()).resolve()
report_dir = root / "Saved" / "BlueprintWorkbench"
manifest = json.loads((report_dir / "test-manifest.json").read_text(encoding="utf-8"))
result = json.loads(unreal.BlueprintWorkbenchLibrary.inspect_blueprint(manifest["destination"]))
assert result["success"], result
assert {v["name"] for v in result["blueprint"]["variables"]} == {"WorkbenchFlag", "RoundIdentity"}
graph = next(g for g in result["blueprint"]["graphs"] if g["name"] == "EventGraph")
assert any(p["links"] for n in graph["nodes"] for p in n["pins"]), "Saved connections lost"
write_node=next(n for n in graph['nodes'] if n['id']==manifest['writeback_node'])
assert next(p['default'] for p in write_node['pins'] if p['name']=='WorkbenchFlag')=='false'
manifest['checks'].append('writeback pin change survives independent-process reload')
compiled = json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch(manifest["destination"], '{"operations": []}'))
assert compiled["success"] and compiled["compile_errors"] == 0, compiled
manifest["checks"].append("saved copy reloads with variables and links and recompiles in a fresh process")
fixture_file = Path(manifest["fixture_file"]).resolve()
expected = (root / "Content" / "__BlueprintWorkbenchTests" / manifest["run_id"] / "BP_Patched.uasset").resolve()
assert fixture_file == expected and expected.is_relative_to(root / "Content" / "__BlueprintWorkbenchTests")
assert fixture_file.is_file()
cleanup_report=report_dir/'test-cleanup-results.json'
cleanup=finalize([{'source':manifest['destination'],'patch':manifest['write_patch'],'result':manifest['write_result']}],cleanup_report)
assert cleanup['success'] and cleanup['deleted']==[manifest['temporary']]
assert not unreal.EditorAssetLibrary.does_asset_exist(manifest['temporary'])
assert unreal.EditorAssetLibrary.does_asset_exist(manifest['destination'])
assert Path(manifest['writeback_backup']).is_file() and cleanup_report.is_file()
manifest['checks'].append('verified writeback automatically deletes registered temporary copy and preserves original, backup and report')
again=finalize([{'source':manifest['destination'],'patch':manifest['write_patch'],'result':manifest['write_result']}],report_dir/'test-cleanup-repeat.json')
assert again['success'] and not again['deleted']
manifest['checks'].append('repeated finalization does not delete anything again')
(report_dir / "test-results.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
unreal.log("BLUEPRINT_WORKBENCH_TESTS_PASSED " + str(len(manifest["checks"])))
