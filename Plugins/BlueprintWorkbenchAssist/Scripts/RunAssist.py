"""Full-editor, tick-driven BA formatter. Never used as a commandlet."""
import json
import os
import shutil
import sys
import time
import traceback
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from AssistSettings import override_setting
from AssistVerification import verify_blueprint

request=json.loads(Path(os.environ['WORKBENCH_ASSIST_REQUEST']).read_text(encoding='utf-8-sig'))
out=Path(request['output']);out.mkdir(parents=True,exist_ok=True)
asset=request['asset'];graph=request['graph']
lib=getattr(unreal,'BlueprintWorkbenchLibrary',None)
bridge=getattr(unreal,'BlueprintWorkbenchAssistLibrary',None)
state={'stage':'startup','at':time.monotonic(),'deadline':time.monotonic()+180}
report={'success':False,'saved':False,'stage':'startup','asset':asset,'graph':graph}
handle=None;busy=False;settings=[]

def write(name,value):
    (out/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')

def update(stage):
    state.update(stage=stage,at=time.monotonic());report['stage']=stage;write('result.json',report)

def checked(raw):
    result=json.loads(raw)
    if not result.get('success'):raise RuntimeError(result.get('error',str(result)))
    return result

def inspect():return checked(lib.inspect_blueprint(asset))['blueprint']

def finish():
    if handle is not None:unreal.unregister_slate_post_tick_callback(handle)
    # Release the Blueprint toolkit while the editor world is still alive.
    # Leaving it open until engine shutdown can assert in UE 5.5 Kismet teardown.
    loaded=unreal.find_object(None,asset)
    if loaded is not None:unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(loaded)
    for obj,key,value in settings:obj.set_editor_property(key,value)
    write('result.json',report)
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    if (out/'cancel.request').exists():raise RuntimeError('Bridge cancelled by its owner')
    if time.monotonic()>state['deadline']:raise RuntimeError('Editor bridge timed out at '+state['stage'])
    age=time.monotonic()-state['at'];stage=state['stage']
    if stage=='startup':
        if age<3:return
        if lib is None or bridge is None:raise RuntimeError('Build and enable BlueprintWorkbench and BlueprintWorkbenchAssist before launching')
        # Session-only overrides. Never SaveConfig or change the user's stored preferences.
        for cls,values in [(unreal.load_class(None,'/Script/BlueprintAssist.BASettings'),{'bGloballyDisableAutoFormatting':True,'bAlwaysFormatAll':False,'bFormatAllAfterSaving':False}),
                           (unreal.load_class(None,'/Script/UnrealEd.EditorLoadingSavingSettings'),{'bAutoSaveEnable':False})]:
            obj=unreal.get_default_object(cls)
            for key,value in values.items():
                override_setting(settings,obj,key,value)
        before=inspect();state['before']=before;write('before.json',before)
        if request.get('writeback'):
            preview=Path(request['preview'])
            approval=json.loads((preview/'result.json').read_text(encoding='utf-8'))
            assert approval['success'],'Successful preview required'
            assert approval['asset']==asset and approval['graph']==graph,'Preview targets a different asset/graph'
            state['preview_after']=json.loads((preview/'after.json').read_text(encoding='utf-8'))
            previous=json.loads((preview/'before.json').read_text(encoding='utf-8'))
            verify_blueprint(previous,before,graph)
            pg=next(g for g in previous['graphs'] if g['path']==graph)
            bg=next(g for g in before['graphs'] if g['path']==graph)
            assert {n['id']:(n['x'],n['y'],n['width'],n['height']) for n in pg['nodes']}=={n['id']:(n['x'],n['y'],n['width'],n['height']) for n in bg['nodes']},'Graph changed since preview'
            assert asset.startswith('/Game/')
            filename=Path(unreal.Paths.project_content_dir())/(asset.split('.')[0][len('/Game/'):]+'.uasset')
            backup=out/filename.name
            assert not backup.exists(),'Backup already exists'
            shutil.copy2(filename,backup);assert backup.is_file()
            report['backup']=str(backup.resolve())
        checked(bridge.open_graph(asset,graph));update('handler');return
    if stage=='handler':
        status=json.loads(bridge.graph_state(asset,graph))
        if not status.get('success'):return
        checked(bridge.graph_state(asset,graph,True));update('sizes');return
    if stage=='sizes':
        status=checked(bridge.graph_state(asset,graph));report['cache']=status
        if not status['ready']:return
        assert bridge.capture_graph(asset,graph,str(out/'before.png'),True)
        update('before_capture');return
    if stage=='before_capture':
        if age<2:return
        assert bridge.capture_graph(asset,graph,str(out/'before.png')),'Before screenshot failed'
        report['formatter']=checked(bridge.format_function_graph(asset,graph))
        write('formatted.json',inspect())
        update('settle');return
    if stage=='settle':
        if age<2:return
        status=checked(bridge.graph_state(asset,graph));report['cache']=status
        # FormatNodes is synchronous; newly inserted reroutes need no cache gate.
        assert bridge.capture_graph(asset,graph,str(out/'after.png'),True)
        update('after_capture');return
    if stage=='after_capture':
        if age<2:return
        assert bridge.capture_graph(asset,graph,str(out/'after.png')),'After screenshot failed'
        report.update(checked(bridge.compile_checked(asset)))
        after=inspect();write('after.json',after)
        report['verification']=verify_blueprint(state['before'],after,graph)
        if request.get('writeback'):
            def geometry(bp):
                g=next(g for g in bp['graphs'] if g['path']==graph)
                return sorted((n['id'],n['x'],n['y'],n['width'],n['height']) for n in g['nodes'] if not n['class'].endswith('K2Node_Knot'))
            assert geometry(after)==geometry(state['preview_after']),'Formatter differs from reviewed preview; saving refused'
            assert unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(asset),only_if_is_dirty=False),'Save failed'
            report['saved']=True
            verify_blueprint(state['before'],inspect(),graph)
        report.update(success=True,stage='complete');finish()

def safe_tick(delta):
    global busy
    if busy:return
    busy=True
    try:tick(delta)
    except Exception as exc:
        report.update(success=False,error=str(exc),traceback=traceback.format_exc());finish()
    finally:busy=False

write('result.json',report)
handle=unreal.register_slate_post_tick_callback(safe_tick)
