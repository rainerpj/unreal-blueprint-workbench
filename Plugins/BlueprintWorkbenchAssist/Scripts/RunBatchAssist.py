"""Explicit batch request: preview each asset in memory, verify, optionally back up/save.
Runs in a dedicated full editor. Failed assets are never saved; the owned process exits.
"""
import copy,json,os,shutil,sys,time,traceback
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from BatchVerification import verify_batch
req=json.loads(Path(os.environ['WORKBENCH_BATCH_REQUEST']).read_text(encoding='utf-8-sig'))
out=Path(req['output'])
ids=[item['id'] for item in req['items']]
if out.exists() or len(ids)!=len(set(ids)) or any(not isinstance(i,str) or i in ('','.','..') or Path(i).name!=i for i in ids):
    unreal.SystemLibrary.quit_editor()
    raise RuntimeError('A new output directory and unique simple item ids are required')
out.mkdir(parents=True,exist_ok=False)
lib=unreal.BlueprintWorkbenchLibrary;bridge=unreal.BlueprintWorkbenchAssistLibrary
state={'stage':'startup','at':time.monotonic(),'index':-1};settings=[];handle=None;busy=False
reports=[]
def write(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def checked(raw):
    r=json.loads(raw)
    if not r.get('success'):raise RuntimeError(r.get('error',str(r)))
    return r
def inspect():return checked(lib.inspect_blueprint(state['item']['asset']))['blueprint']
def restore(snapshot):
    r=checked(bridge.restore_formatting_defaults(state['item']['asset'],json.dumps(snapshot)))
    state['report'].setdefault('restored_pin_defaults',[]).extend(r['changes'])

def stage(name):
    state.update(stage=name,at=time.monotonic());write(out/'progress.json',{'stage':name,'asset':state.get('item',{}).get('asset'),'index':state['index'],'graph':state.get('graph',{}).get('path'),'root_index':state.get('ri')})
def finish():
    if handle is not None:unreal.unregister_slate_post_tick_callback(handle)
    for obj,key,value in settings:obj.set_editor_property(key,value)
    write(out/'results.json',reports);unreal.SystemLibrary.quit_editor()
def next_asset():
    state['index']+=1
    if state['index']>=len(req['items']):finish();return
    state.pop('graph',None);state.pop('ri',None)
    item=req['items'][state['index']];state['item']=item
    d=out/item['id'];d.mkdir(parents=True,exist_ok=False);state['dir']=d
    state['report']={'asset':item['asset'],'id':item['id'],'success':False,'saved':False,'formatted_roots':[]}
    before=inspect();state['before']=before;write(d/'before.json',before)
    if before.get('parent')=='/Script/CoreUObject.Interface':
        state['report'].update(success=True,skipped='Interface declaration has no editable implementation graph')
        write(d/'result.json',state['report']);reports.append(state['report']);write(out/'results.json',reports)
        stage('next');return
    unreal.load_object(None,item['asset'])
    checked(bridge.compile_checked(item['asset']))
    restore(before)
    verify_batch(before,inspect(),[])
    expected=copy.deepcopy(before)
    for op in item.get('cleanup',[]):
        g=next(g for g in expected['graphs'] if g['path']==op['graph']);ids=set(op['nodes'])
        g['nodes']=[n for n in g['nodes'] if n['id'] not in ids]
        for n in g['nodes']:
            for pin in n['pins']:pin['links']=[l for l in pin['links'] if l['node'] not in ids]
        checked(bridge.remove_unused_nodes(item['asset'],op['graph'],op['nodes']))
    verify_batch(expected,inspect(),[]);state['expected']=expected;write(d/'expected-cleaned.json',expected)
    state['gi']=-1;next_graph()
def next_graph():
    state['gi']+=1
    if state['gi']>=len(state['item']['graphs']):stage('compile');return
    graph=state['item']['graphs'][state['gi']];state['graph']=graph;state['ri']=0
    checked(bridge.open_graph(state['item']['asset'],graph['path'],True));stage('handler')
def tick(delta):
    age=time.monotonic()-state['at'];s=state['stage']
    if (out/'cancel.request').exists():raise RuntimeError('Cancelled by owner')
    if age>120:raise RuntimeError('Timeout at '+s)
    if s=='startup':
        if (out.parent/'stop-batch.request').exists():finish();return
        if age<2:return
        for cls,values in [('/Script/BlueprintAssist.BASettings',{'bGloballyDisableAutoFormatting':True,'bAlwaysFormatAll':False,'bFormatAllAfterSaving':False}),('/Script/UnrealEd.EditorLoadingSavingSettings',{'bAutoSaveEnable':False})]:
            obj=unreal.get_default_object(unreal.load_class(None,cls))
            for key,value in values.items():settings.append((obj,key,obj.get_editor_property(key)));obj.set_editor_property(key,value)
        next_asset();return
    asset=state['item']['asset'];d=state['dir']
    if s=='handler':
        r=json.loads(bridge.graph_state(asset,state['graph']['path']))
        if not r.get('success'):return
        checked(bridge.graph_state(asset,state['graph']['path'],True));stage('sizes');return
    if s=='sizes':
        r=checked(bridge.graph_state(asset,state['graph']['path']))
        if not r['ready']:return
        stage('format');return
    if s=='format':
        graph=state['graph'];roots=graph['roots']
        if state['ri']<len(roots):
            nid=roots[state['ri']]
            result=checked(bridge.format_graph_root(asset,graph['path'],nid))
            state['report']['formatted_roots'].append({'graph':graph['path'],'node':nid,**result});state['ri']+=1
            state['at']=time.monotonic();return
        restore(state['expected'])
        verify_batch(state['expected'],inspect(),[g['path'] for g in state['item']['graphs'][:state['gi']+1]])
        assert bridge.capture_graph(asset,graph['path'],str(d/('graph-'+str(state['gi'])+'.png')),True)
        stage('capture');return
    if s=='capture':
        if age<0.5:return
        assert bridge.capture_graph(asset,state['graph']['path'],str(d/('graph-'+str(state['gi'])+'.png')))
        next_graph();return
    if s=='compile':
        compiled=checked(bridge.compile_checked(asset));restore(state['expected']);after=inspect();write(d/'after.json',after)
        verified=verify_batch(state['expected'],after,[g['path'] for g in state['item']['graphs']])
        state['report'].update(compiled,verification=verified,success=True,saved=False)
        write(d/'preview.json',state['report'])
        if req.get('writeback'):
            assert asset.startswith('/Game/')
            package=state['item'].get('map',asset)
            source=Path(unreal.Paths.project_content_dir())/(package[len('/Game/'):]+('.umap' if 'map' in state['item'] else '.uasset'))
            assert source.is_file(),'Saved source file missing'
            backup=d/source.name;assert not backup.exists();shutil.copy2(source,backup);assert backup.is_file()
            if 'map' in state['item']:
                saved=unreal.EditorLoadingAndSavingUtils.save_map(unreal.load_asset(package),package)
            else:saved=unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(asset),only_if_is_dirty=False)
            assert saved,'Save failed'
            state['report'].update(saved=True,backup=str(backup.resolve()))
            verify_batch(state['expected'],inspect(),[g['path'] for g in state['item']['graphs']])
        write(d/'result.json',state['report']);reports.append(state['report']);write(out/'results.json',reports)
        unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(unreal.load_object(None,asset))
        stage('next');return
    if s=='next':next_asset()
def safe_tick(delta):
    global busy
    if busy:return
    busy=True
    try:tick(delta)
    except Exception as e:
        report=state.get('report',{'success':False,'saved':False});report.update(success=False,error=str(e),traceback=traceback.format_exc())
        if 'dir' in state:
            write(state['dir']/'result.json',report)
            try:write(state['dir']/'failed-after.json',inspect())
            except Exception:pass
        reports.append(report);finish()
    finally:busy=False
handle=unreal.register_slate_post_tick_callback(safe_tick)
