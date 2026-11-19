"""Remove the exact CLI fixture after all CLI checks passed; retain diagnostics and backup."""
import json
import re
from pathlib import Path
import unreal

directory = Path(unreal.Paths.project_dir()).resolve() / 'Saved/BlueprintWorkbench'
manifest = json.loads((directory / 'cli-manifest.json').read_text(encoding='utf-8'))
assert re.fullmatch('[a-f0-9]{32}', manifest['run_id'])
assert manifest['source'].split('.')[0] == '/Game/__BlueprintWorkbenchTests/' + manifest['run_id'] + '/BP_CliSource'
assert not unreal.EditorAssetLibrary.does_asset_exist(manifest['temporary'])
assert unreal.EditorAssetLibrary.delete_asset(manifest['source'])
assert not unreal.EditorAssetLibrary.does_asset_exist(manifest['source'])
(directory / 'cli-cleanup.json').write_text(json.dumps({'success': True, 'source': manifest['source']}), encoding='utf-8')
