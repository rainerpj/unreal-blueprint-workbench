"""Create one explicitly tracked CLI fixture; no project classes or content required."""
import json
import uuid
from pathlib import Path
import unreal

run_id = uuid.uuid4().hex
factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.Actor)
source = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'BP_CliSource', '/Game/__BlueprintWorkbenchTests/' + run_id, unreal.Blueprint, factory)
assert source and unreal.EditorAssetLibrary.save_loaded_asset(source)
directory = Path(unreal.Paths.project_dir()).resolve() / 'Saved/BlueprintWorkbench'
directory.mkdir(parents=True, exist_ok=True)
(directory / 'cli-manifest.json').write_text(json.dumps({
    'source': source.get_path_name(), 'run_id': run_id,
    'temporary': '/Game/Workbench/Cli_' + run_id + '/BP_Review'
}, indent=2), encoding='utf-8')
