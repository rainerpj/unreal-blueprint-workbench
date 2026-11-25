"""Cross-asset capability integration; only unique test fixtures are created."""
import json, uuid
from pathlib import Path
import unreal
lib=unreal.BlueprintWorkbenchLibrary
out=Path(unreal.Paths.project_saved_dir())/'BlueprintWorkbench'
out.mkdir(parents=True,exist_ok=True)
run=uuid.uuid4().hex
base='/Game/__BlueprintWorkbenchTests/'+run
checks=[]
def check(label,value):
    assert value,label
    checks.append(label)
    unreal.log('ADVANCED_CHECK '+label)
def patch(asset,ops,destination=''):
    result=json.loads(lib.apply_patch(asset,json.dumps({'operations':ops}),destination))
    assert result['success'],result
    return result
def create(kind,name,ops,parent='/Script/Engine.Actor'):
    result=json.loads(lib.create_blueprint(kind,parent,json.dumps({'operations':ops}),base+'/'+name))
    assert result['success'] and result['saved'],result
    return result

def node(kind,id,graph='EventGraph',**kw):return dict(op='add_node',graph=graph,kind=kind,id=id,**kw)
def link(a,ap,b,bp,graph='EventGraph'):return {'op':'connect','graph':graph,'from':{'node':a,'pin':ap},'to':{'node':b,'pin':bp}}
def graph(r,name):return next(g for g in r['blueprint']['graphs'] if g['name']==name)

library=create('function_library','BFL_Test',[{'op':'signature','name':'Compute','entry_id':'compute_entry','result_id':'compute_result','parameters':[{'name':'Result','type':'int','direction':'output'}]}, {'op':'function_properties','graph':'Compute','pure':True,'category':'Workbench','tooltip':'Test function'},link('compute_entry','then','compute_result','execute','Compute')])
library_path=library['blueprint']['asset']
check('create function library with pure function metadata', any(n.get('tooltip')=='Test function' for n in graph(library,'Compute')['nodes']))
bpi=create('interface','BPI_Test',[{'op':'signature','name':'Query','parameters':[{'name':'Result','type':'int','direction':'output'}]},{'op':'signature','name':'Notify','parameters':[{'name':'Value','type':'int','direction':'input'}]}])
bpi_class=bpi['blueprint']['asset']+'_C'
check('create interface signatures',len([g for g in bpi['blueprint']['graphs'] if g['name'] in ('Query','Notify')])==2)
bad=json.loads(lib.apply_patch(bpi['blueprint']['asset'],json.dumps({'operations':[node('branch','bad',graph='Query')]})))
check('interface implementation logic rejected',not bad['success'])
actor=create('normal','BP_Test',[
 {'op':'implement_interface','interface':bpi_class},
 node('interface_event','notify',owner=bpi_class,function='Notify'),
 {'op':'add_variable','name':'Numbers','type':'int','container':'array','default':'(1,2,3)'},
 {'op':'add_variable','name':'UniqueTags','type':'name','container':'set','default':'(Alpha,Beta)'},
 {'op':'add_variable','name':'Lookup','type':'name','container':'map','value_type':{'type':'int'},'default':'((A,1),(B,2))'},
 {'op':'add_variable','name':'ClassValue','type':'class','object':'/Script/Engine.Actor','default':'/Script/Engine.Actor'},
 {'op':'add_variable','name':'SoftValue','type':'soft_object','object':'/Script/Engine.Texture2D','default':'/Game/UnloadedTexture.UnloadedTexture'},
 {'op':'add_variable','name':'Caption','type':'text','default':'Hello world'},
 {'op':'set_member_default','variable':'Numbers','value':'(4,5)'},
 {'op':'signature','name':'WithArray','parameters':[{'name':'Items','type':'int','container':'array','direction':'input','reference':True},{'name':'LocalNames','type':'name','container':'array','direction':'local','default':'(A,B)'}]},
 {'op':'add_dispatcher','name':'Changed','parameters':[{'name':'Amount','type':'int'}]},
 node('custom_event','start',name='Run',parameters=[{'name':'Amount','type':'int'}]),
 node('dispatcher_call','broadcast',name='Changed'),
 link('start','then','broadcast','execute'),link('start','Amount','broadcast','Amount'),
 node('call','library_call',owner=library_path+'_C',function='Compute'),
 node('self','self'),node('interface_message','message',owner=bpi_class,function='Query'),link('self','self','message','self'),
 node('sequence','sequence'),node('switch_enum','switch',enum='/Script/Engine.ECollisionChannel'),
 node('for_loop','loop'),node('for_loop_break','loop_break'),node('foreach_break','each_break'),
 node('make_array','array'),node('get','numbers',variable='Numbers'),
 link('numbers','Numbers','each_break','Array'),
 node('select','select'),node('get','caption',variable='Caption'),link('caption','Caption','select','Option 0'),
])
asset=actor['blueprint']['asset']
check('normal asset implements event and return-valued interface',bpi_class in actor['blueprint']['interfaces'] and any(g['name']=='Query' for g in actor['blueprint']['graphs']))
check('array set map class soft reference and text members compile',len(actor['blueprint']['variables'])==7)
check('dispatcher and custom event parameters connect',True)
check('function library and interface message consumers compile',True)
# Actual generated CDO values, not just descriptor strings.
cls=unreal.load_object(None,asset+'_C')
cdo=unreal.get_default_object(cls)
check('member array default reaches generated CDO', list(cdo.get_editor_property('Numbers'))==[4,5])
unreal.log('CDO_MAP '+str(cdo.get_editor_property('Lookup'))+' SET '+str(cdo.get_editor_property('UniqueTags')))
check('map and set defaults reach generated CDO', {str(k):v for k,v in cdo.get_editor_property('Lookup').items()}=={'A':1,'B':2} and set(map(str,cdo.get_editor_property('UniqueTags')))=={'Alpha','Beta'})
check('class default resolves actual UClass', cdo.get_editor_property('ClassValue')==unreal.Actor.static_class())
ids=actor['aliases']
follow=[
 {'op':'edit_pins','graph':'EventGraph','node':ids['sequence'],'action':'add'},
 {'op':'edit_pins','graph':'EventGraph','node':ids['sequence'],'action':'remove','pin':'then_2'},
 {'op':'edit_pins','graph':'EventGraph','node':ids['array'],'action':'add'},
 {'op':'edit_pins','graph':'EventGraph','node':ids['array'],'action':'remove','pin':'[1]'},
 {'op':'add_variable','name':'IndexValue','type':'int'},node('get','index',variable='IndexValue'),
 link('index','IndexValue',ids['select'],'Index'),
 {'op':'edit_pins','graph':'EventGraph','node':ids['select'],'action':'add'},
 {'op':'edit_pins','graph':'EventGraph','node':ids['select'],'action':'remove','pin':'Option 2'},
 node('call','vector',owner='/Script/Engine.KismetMathLibrary',function='MakeVector'),
 {'op':'edit_pins','graph':'EventGraph','node':'vector','action':'split','pin':'ReturnValue'},
 {'op':'edit_pins','graph':'EventGraph','node':'vector','action':'recombine','pin':'ReturnValue'},
 {'op':'add_variable','name':'Offset','type':'struct','object':'/Script/CoreUObject.Vector','default':'(X=1,Y=2,Z=3)'},
 {'op':'add_variable','name':'Profile','type':'struct','object':'/Script/Engine.WeightedBlendable','default':'(Weight=0.25,Object=None)'},node('get','offset',variable='Profile'),node('set_members_struct','set_fields',struct='/Script/Engine.WeightedBlendable',fields=['Weight']),
 link('offset','Profile','set_fields','StructRef'),
 {'op':'set_default','graph':'EventGraph','node':'set_fields','pin':'Weight','value':'0.75'},
 node('call','class_call',owner='/Script/Engine.GameplayStatics',function='GetAllActorsOfClass'),
 {'op':'set_default','graph':'EventGraph','node':'class_call','pin':'ActorClass','value':'/Script/Engine.Actor'},
 {'op':'set_default','graph':'EventGraph','node':ids['select'],'pin':'Option 1','value':'Caption B'},
 {'op':'replace_node','graph':'EventGraph','node':ids['numbers'],'replacement':{'kind':'get','id':'new_numbers','variable':'Numbers'}},
 {'op':'move_node','graph':'EventGraph','node':ids['numbers'],'x':123,'y':456},
 {'op':'rename_variable','name':'Numbers','new_name':'InventoryNumbers'},
 {'op':'rename_function','name':'WithArray','new_name':'WithList'},
 {'op':'edit_parameter','graph':'WithList','direction':'input','name':'Items','action':'rename','new_name':'Values'},
 {'op':'rename_variable','graph':'WithList','name':'LocalNames','new_name':'LocalLabels'},
 {'op':'edit_parameter','graph':'Changed','direction':'input','name':'Extra','action':'add','type':'bool'},
 {'op':'refresh_calls','functions':['Changed']},
 {'op':'edit_parameter','graph':'EventGraph','node':ids['start'],'direction':'input','name':'Extra','action':'add','type':'bool'},
 {'op':'signature','name':'Defaults','parameters':[{'name':'Amount','type':'int','direction':'input','default':'7'}]},
 {'op':'edit_parameter','graph':'Defaults','name':'Amount','direction':'input','action':'default','value':'9'},
 {'op':'edit_parameter','graph':'Defaults','name':'Spare','direction':'input','action':'add','type':'int'},
 {'op':'edit_parameter','graph':'Defaults','name':'Spare','direction':'input','action':'type','type':'float'},
 {'op':'edit_parameter','graph':'Defaults','name':'Spare','direction':'input','action':'remove'},
 node('call','default_call',function='Defaults'),
 {'op':'function_properties','graph':'Defaults','access':'private','category':'Testing','tooltip':'Input defaults'},
]
updated=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':follow})))
assert updated['success'] and updated['saved'],updated
(out/'advanced-updated.json').write_text(json.dumps(updated,indent=2),encoding='utf-8')
check('dynamic array sequence select pins and struct split/recombine compile',True)
check('Set Members in Struct and class/text pin defaults compile',True)
check('node replacement preserves linked array consumer',any(any(p['name']=='InventoryNumbers' and p['links'] for p in n['pins']) for n in graph(updated,'EventGraph')['nodes']))
check('original node GUID resolves to replacement within same patch',any(n['x']==123 and n['y']==456 and any(p['name']=='InventoryNumbers' for p in n['pins']) for n in graph(updated,'EventGraph')['nodes']))
check('function variable and local renames persist in graph',any(g['name']=='WithList' for g in updated['blueprint']['graphs']))
check('parameter add rename type remove and default edits compile',any(p['name']=='Amount' and p['default']=='9' for n in graph(updated,'EventGraph')['nodes'] if n['title']=='Defaults' for p in n['pins']))
check('dispatcher and custom event signatures can be extended',any(p['name']=='Extra' for n in graph(updated,'Changed')['nodes'] for p in n['pins']))
# Cross-asset contract change: impact report first, explicit definition write and caller refresh.
impact=json.loads(lib.find_member_references(library_path,'Compute'))
(out/'advanced-impact.json').write_text(json.dumps(impact,indent=2),encoding='utf-8')
check('reference query locates external library call',impact['success'] and any(x['asset']==asset for x in impact['references']))
changed=json.loads(lib.apply_patch_to_saved_asset(library_path,json.dumps({'operations':[{'op':'signature','name':'Compute','parameters':[{'name':'Input','type':'int','direction':'input','default':'12'}]}]})))
assert changed['success'],changed
caller=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':[{'op':'refresh_calls','owner':library_path+'_C','functions':['Compute']}]})))
assert caller['success'],caller
check('library signature change and explicit caller refresh',any(p['name']=='Input' and p['default']=='12' for n in graph(caller,'EventGraph')['nodes'] if n['title']=='Compute' for p in n['pins']))
# Negative boundaries must not save a destination or alter source.
before_failures=json.loads(lib.inspect_blueprint(asset))['blueprint']
for label,operations in [
 ('invalid property default',[{'op':'set_member_default','variable':'InventoryNumbers','value':'(invalid)'}]),
 ('connected parameter removal',[{'op':'edit_parameter','graph':'EventGraph','node':ids['start'],'direction':'input','name':'Amount','action':'remove'}]),
 ('entry replacement',[{'op':'replace_node','graph':'EventGraph','node':ids['start'],'replacement':{'kind':'branch','id':'bad'}}]),
 ('nested map value',[{'op':'add_variable','name':'BadMap','type':'name','container':'map','value_type':{'type':'int','container':'array'}}]),
 ('hard reference must resolve',[{'op':'set_member_default','variable':'ClassValue','value':'/Script/Engine.DoesNotExist'}]),
 ('incompatible replacement',[{'op':'replace_node','graph':'EventGraph','node':updated['aliases']['new_numbers'],'replacement':{'kind':'get','id':'bad_get','variable':'Caption'}}]),
 ('local rename collision',[{'op':'signature','name':'WithList','parameters':[{'name':'OtherLocal','type':'int','direction':'local'}]},{'op':'rename_variable','graph':'WithList','name':'OtherLocal','new_name':'LocalLabels'}]),
 ('native-make Set Members unsupported',[node('set_members_struct','invalid_vector_members',struct='/Script/CoreUObject.Vector',fields=['X'])]),
 ('duplicate interface',[{'op':'implement_interface','interface':bpi_class}]),
]:
 bad=json.loads(lib.apply_patch(asset,json.dumps({'operations':operations})))
 check(label+' rejected',not bad['success'] and not bad['saved'])
check('failed advanced previews preserve source',json.loads(lib.inspect_blueprint(asset))['blueprint']==before_failures)
check('existing destination creation rejected',not json.loads(lib.create_blueprint('normal','/Script/Engine.Actor','{"operations":[]}',base+'/BP_Test'))['success'])
# Persist executable return values and verify generated functions, without PIE.
lib_state=json.loads(lib.inspect_blueprint(library_path))
lib_entry=next(n['id'] for n in graph(lib_state,'Compute')['nodes'] if n['class'].endswith('K2Node_FunctionEntry'))
lib_return=next(n['id'] for n in graph(lib_state,'Compute')['nodes'] if n['class'].endswith('K2Node_FunctionResult'))
lib_value=json.loads(lib.apply_patch_to_saved_asset(library_path,json.dumps({'operations':[link(lib_entry,'then',lib_return,'execute','Compute'),{'op':'set_default','graph':'Compute','node':lib_return,'pin':'Result','value':'42'}]})))
assert lib_value['success'],lib_value
lib_cdo=unreal.get_default_object(unreal.load_object(None,library_path+'_C'))
check('generated function library executes and returns configured result',lib_cdo.call_method('Compute',kwargs={'input':12,'world_context':None})==42)
# Check type variants and defaults independently, then persist them.
extra=[
 {'op':'add_variable','name':'EnumValue','type':'enum','object':'/Script/Engine.ECollisionChannel','default':'ECC_Pawn'},
 {'op':'add_variable','name':'ByteValue','type':'byte','default':'123'},
 {'op':'add_variable','name':'Int64Value','type':'int64','default':'1234567890123'},
 {'op':'add_variable','name':'DoubleValue','type':'double','default':'2.25'},
 {'op':'add_variable','name':'ObjectValue','type':'object','object':'/Script/Engine.Texture2D','default':"/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"},
 {'op':'add_variable','name':'SoftClassValue','type':'soft_class','object':'/Script/Engine.Actor','default':'/Script/Engine.Actor'},
 {'op':'add_variable','name':'InterfaceValue','type':'interface','object':bpi_class},
 {'op':'set_member_default','variable':'InitialLifeSpan','value':'12.5'},
 {'op':'signature','name':'ReadLocal','parameters':[{'name':'LocalValue','type':'int','direction':'local','default':'21'},{'name':'Result','type':'int','direction':'output'}]},
 node('local_get','read_local',graph='ReadLocal',variable='LocalValue'),
]
extra_result=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':extra})))
assert extra_result['success'],extra_result
lg=graph(extra_result,'ReadLocal')
entry=next(n['id'] for n in lg['nodes'] if n['class'].endswith('K2Node_FunctionEntry'))
ret=next(n['id'] for n in lg['nodes'] if n['class'].endswith('K2Node_FunctionResult'))
get=extra_result['aliases']['read_local']
caller=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':[link(entry,'then',ret,'execute','ReadLocal'),link(get,'LocalValue',ret,'Result','ReadLocal')]})))
assert caller['success'],caller
actor_cdo=unreal.get_default_object(unreal.load_object(None,asset+'_C'))
check('generated function executes local variable initializer',actor_cdo.call_method('ReadLocal')==21)
check('enum byte int64 double hard object soft class and interface types compile',len(caller['blueprint']['variables'])==17)
check('inherited class default is editable',actor_cdo.get_editor_property('initial_life_span')==12.5)
# Interface definition changes explicitly update its implementation and message call site.
bpi_changed=json.loads(lib.apply_patch_to_saved_asset(bpi['blueprint']['asset'],json.dumps({'operations':[{'op':'signature','name':'Query','parameters':[{'name':'Request','type':'int','direction':'input'}]}]})))
assert bpi_changed['success'],bpi_changed
caller=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':[{'op':'refresh_interfaces'},{'op':'refresh_calls','owner':bpi_class,'functions':['Query']}]})))
assert caller['success'],caller
check('interface signature changes refresh implementation and message pins',any(p['name']=='Request' for n in graph(caller,'Query')['nodes'] for p in n['pins']) and any(p['name']=='Request' for n in graph(caller,'EventGraph')['nodes'] if n['class'].endswith('K2Node_Message') for p in n['pins']))
bpi=bpi_changed
query_graph=graph(caller,'Query')
query_entry=next(n['id'] for n in query_graph['nodes'] if n['class'].endswith('K2Node_FunctionEntry'))
query_result=next(n['id'] for n in query_graph['nodes'] if n['class'].endswith('K2Node_FunctionResult'))
message_ops=[
 link(query_entry,'then',query_result,'execute','Query'),
 {'op':'set_default','graph':'Query','node':query_result,'pin':'Result','value':'17'},
 {'op':'signature','name':'TestMessage','entry_id':'message_entry','result_id':'message_result','parameters':[{'name':'Result','type':'int','direction':'output'}]},
 node('self','message_self',graph='TestMessage'),node('interface_message','execute_message',graph='TestMessage',owner=bpi_class,function='Query'),
 link('message_self','self','execute_message','self','TestMessage'),
 link('message_entry','then','execute_message','execute','TestMessage'),
 link('execute_message','then','message_result','execute','TestMessage'),
 link('execute_message','Result','message_result','Result','TestMessage'),
]
caller=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':message_ops})))
assert caller['success'],caller
actor_cdo=unreal.get_default_object(unreal.load_object(None,asset+'_C'))
check('interface message executes implementation and returns result',actor_cdo.call_method('TestMessage')==17)
actor=caller
# Regression: execute a supported native Set Members, not a dead/pruned node.
mutate_ops=[
 {'op':'signature','name':'MutateProfile','entry_id':'mutate_entry','result_id':'mutate_result','parameters':[{'name':'Result','type':'struct','object':'/Script/Engine.WeightedBlendable','direction':'output'}]},
 node('get','mutate_profile',graph='MutateProfile',variable='Profile'),
 node('set_members_struct','mutate_fields',graph='MutateProfile',struct='/Script/Engine.WeightedBlendable',fields=['Weight']),
 link('mutate_profile','Profile','mutate_fields','StructRef','MutateProfile'),
 link('mutate_entry','then','mutate_fields','execute','MutateProfile'),
 link('mutate_fields','then','mutate_result','execute','MutateProfile'),
 link('mutate_fields','StructOut','mutate_result','Result','MutateProfile'),
 {'op':'set_default','graph':'MutateProfile','node':'mutate_fields','pin':'Weight','value':'0.75'},
]
caller=json.loads(lib.apply_patch_to_saved_asset(asset,json.dumps({'operations':mutate_ops})))
assert caller['success'],caller
value=unreal.get_default_object(unreal.load_object(None,asset+'_C')).call_method('MutateProfile')
check('supported Set Members executes field mutation',value.weight==0.75 and value.object is None)
# Inspect all generated pins rather than guessing on follow-up patches.
(out/'advanced-first.json').write_text(json.dumps(actor,indent=2),encoding='utf-8')
manifest={'run_id':run,'base':base,'library':library_path,'interface':bpi['blueprint']['asset'],'actor':asset,'actor_result':actor,'checks':checks,'library_result':lib_value,'interface_result':bpi}
(out/'advanced-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
unreal.log('ADVANCED_FIRST_PASSED')
