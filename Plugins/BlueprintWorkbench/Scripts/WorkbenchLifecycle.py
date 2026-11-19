"""Tracked temporary-copy lifecycle. Never infer cleanup candidates from folder contents."""
import copy
import json
from pathlib import Path
import re
import uuid
import unreal


def root_dir():
    return Path(unreal.Paths.project_dir()).resolve() / 'Saved/BlueprintWorkbench'


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def package(asset):
    return asset.split('.', 1)[0]


def validate_temporary(source, destination):
    if not re.fullmatch(r'/Game/Workbench/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+', destination) or package(source) == destination:
        raise ValueError('Temporary copies must be new assets under /Game/Workbench/, distinct from the original')


def register_copy(source, patch, result):
    if not result.get('success') or not result.get('saved'):
        raise ValueError('Only successfully saved copies can be registered')
    destination = package(result['blueprint']['asset'])
    validate_temporary(source, destination)
    receipt = {'source': package(source), 'destination': destination, 'patch': patch, 'state': 'pending'}
    path = root_dir() / 'TemporaryCopies' / (uuid.uuid4().hex + '.json')
    dump(path, receipt)
    return str(path)


def graph_state(bp):
    value = copy.deepcopy(bp)
    # UE can reconstruct optional pins while loading; compare their meaning, not transient pin GUIDs.
    for graph in value['graphs']:
        for node in graph['nodes']:
            for pin in node['pins']:
                pin.pop('id', None)
                for link in pin['links']:
                    link.pop('pin_id', None)
                pin['links'].sort(key=lambda link:(link['node'],link['pin']))
            node['pins'].sort(key=lambda pin:(pin['direction'],pin['name']))
        graph['nodes'].sort(key=lambda node:node['id'])
    value['graphs'].sort(key=lambda graph:graph['path'])
    return value


def finalize(writes, report_path):
    """Run in a fresh commandlet after successful writeback. All originals verified before deletion."""
    report_path = Path(report_path)
    report = {'success': False, 'verified': [], 'deleted': [], 'already_absent': [], 'retained': []}
    dump(report_path, report)
    try:
        if not writes:
            raise ValueError('No completed writes to verify')
        for item in writes:
            result = item['result']
            source = package(item['source'])
            if not (result.get('success') and result.get('saved') and result.get('compiled') and result.get('compile_errors') == 0):
                raise ValueError('Writeback did not complete: ' + source)
            if package(result['blueprint']['asset']) != source or not Path(result['backup_file']).is_file():
                raise ValueError('Original identity or backup missing: ' + source)
            current = json.loads(unreal.BlueprintWorkbenchLibrary.inspect_blueprint(source))
            if not current['success'] or graph_state(current['blueprint']) != graph_state(result['blueprint']):
                dump(report_path.with_suffix('.mismatch.json'), {'expected':result['blueprint'], 'actual':current.get('blueprint')})
                raise ValueError('Saved original does not match verified writeback: ' + source)
            compiled = json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch(source, '{"operations":[]}'))
            if not compiled['success'] or compiled.get('compile_errors') != 0:
                raise ValueError('Original recompile failed: ' + source)
            report['verified'].append({'source': source, 'compile_warnings': compiled.get('compile_warnings', 0)})
        candidates = []
        for path in sorted((root_dir() / 'TemporaryCopies').glob('*.json')):
            receipt = json.loads(path.read_text(encoding='utf-8'))
            if receipt['state'] == 'deleted':
                continue
            if not any(receipt['source'] == package(w['source']) and receipt['patch'] == w['patch'] for w in writes):
                continue
            validate_temporary(receipt['source'], receipt['destination'])
            if receipt['destination'] in {package(w['source']) for w in writes}:
                raise ValueError('Cleanup candidate is an original asset')
            candidates.append((path, receipt))
        if candidates:
            unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
        # Preflight every candidate before any deletion. Keep referenced copies, including sibling references.
        removable = []
        for path, receipt in candidates:
            asset = receipt['destination']
            if not unreal.EditorAssetLibrary.does_asset_exist(asset):
                report['already_absent'].append(asset)
                receipt['state'] = 'deleted'
                dump(path, receipt)
                continue
            refs = [r for r in unreal.EditorAssetLibrary.find_package_referencers_for_asset(asset, True) if package(r) != asset]
            if refs:
                report['retained'].append({'asset': asset, 'reason': 'referenced', 'referencers': refs})
            else:
                removable.append((path, receipt))
        dump(report_path, report)
        for path, receipt in removable:
            asset = receipt['destination']
            if not unreal.EditorAssetLibrary.delete_asset(asset) or unreal.EditorAssetLibrary.does_asset_exist(asset):
                raise RuntimeError('Could not delete temporary asset: ' + asset)
            receipt['state'] = 'deleted'
            dump(path, receipt)
            report['deleted'].append(asset)
            dump(report_path, report)
        report['success'] = True
        report['cleanup_complete'] = not report['retained']
        dump(report_path, report)
        return report
    except Exception as error:
        report['error'] = str(error)
        dump(report_path, report)
        raise
