"""Read-only verification in a fresh full editor (also supports AnimBlueprint)."""
import json,os,sys,traceback
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from BatchVerification import verify_batch
req=json.loads(Path(os.environ['WORKBENCH_BATCH_VERIFY']).read_text(encoding='utf-8-sig'))
results=[]
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorLoadingSavingSettings'))
previous_autosave=settings.get_editor_property('bAutoSaveEnable')
settings.set_editor_property('bAutoSaveEnable',False)
try:
    for index,name in enumerate(req['directories']):
        d=Path(name);saved=json.loads((d/'result.json').read_text(encoding='utf-8'))
        if not saved.get('success') or not saved.get('saved'):continue
        report={'asset':saved['asset'],'directory':name,'success':False}
        try:
            expected=json.loads((d/'expected-cleaned.json').read_text(encoding='utf-8'))
            after=json.loads((d/'after.json').read_text(encoding='utf-8'))
            r=json.loads(unreal.BlueprintWorkbenchLibrary.inspect_blueprint(saved['asset']));assert r['success']
            current=r['blueprint'];(d/'reloaded.json').write_text(json.dumps(current,ensure_ascii=False),encoding='utf-8');graphs=sorted({x['graph'] for x in saved['formatted_roots']})
            verified=verify_batch(expected,current,graphs)
            def geo(bp):return {g['path']:{n['id']:(n['x'],n['y'],n.get('width'),n.get('height')) for n in g['nodes']} for g in bp['graphs']}
            assert geo(after)==geo(current),'Reloaded geometry differs from saved preview'
            compiled=json.loads(unreal.BlueprintWorkbenchAssistLibrary.compile_checked(saved['asset']))
            assert compiled['success'] and compiled['compile_errors']==0,compiled
            report.update(verified,success=True,compile_errors=compiled['compile_errors'],compile_warnings=compiled['compile_warnings'])
        except Exception as e:report.update(error=str(e),traceback=traceback.format_exc())
        results.append(report)
        (d/'reload.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        Path(req['output']).write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        print('BATCH_RELOAD',index,report['success'],saved['asset'],report.get('error',''))
        if index%10==9:unreal.SystemLibrary.collect_garbage()
finally:
    settings.set_editor_property('bAutoSaveEnable',previous_autosave)
    unreal.SystemLibrary.quit_editor()
