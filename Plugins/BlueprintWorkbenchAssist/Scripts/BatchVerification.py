"""Whole-asset logic verification for multiple explicitly selected K2 graphs."""
import copy
import re
import math
from AssistVerification import canonical_graph,verify_blueprint

LEGACY_FLOAT_FLAG=',bSerializeAsSinglePrecisionFloat=True'
CALL_NODES={'K2Node_CallFunction','K2Node_CommutativeAssociativeBinaryOperator','K2Node_PromotableOperator'}

def connected_numeric_input(node,pin,nodes):
    t=pin['type']
    if not (node['class'].split('.')[-1] in CALL_NODES and pin['direction']=='input' and pin['links'] and t['category']=='real' and t.get('subcategory')=='double' and t.get('container',0)==0 and not t.get('reference')):return False
    pending=[l['node'] for l in pin['links']];seen=set()
    while pending:
        nid=pending.pop()
        if nid in seen or nid not in nodes:continue
        seen.add(nid);source=nodes[nid]
        if source['class']!='/Script/BlueprintGraph.K2Node_Knot':return True
        pending.extend(l['node'] for p in source['pins'] if p['direction']=='input' for l in p['links'])
    return False

def finite_numeric_default(pin):
    try:return math.isfinite(float(pin['default']))
    except (ValueError,TypeError):return False

def normalized(bp):
    value=copy.deepcopy(bp);count=0
    bindings=value.get('input_key_bindings',[])
    # Distinct physical alphanumeric keys dispatch by UE's global EventIndex.
    # Keep duplicate keys, AnyKey and all other binding order strict.
    if len({x['key'] for x in bindings})==len(bindings) and all(re.fullmatch('[A-Z0-9]',x['key']) for x in bindings):
        bindings.sort(key=lambda x:x['key'])
    for graph in value['graphs']:
        nodes={n['id']:n for n in graph['nodes']}
        for node in graph['nodes']:
            for pin in node['pins']:
                # UE's legacy flag controls literal parsing, never a connected
                # numeric call input with a real upstream source. Preserve all
                # actual numeric types and every unconnected/output pin flag.
                if connected_numeric_input(node,pin,nodes):
                    pin['type']['unreal_text']=pin['type']['unreal_text'].replace(LEGACY_FLOAT_FLAG,'')
                    if finite_numeric_default(pin):pin['default']=''
                # Slate enum widgets may initialize an unused default even on a
                # connected enum/enum-array input. Preserve types and terminal links;
                # disconnected defaults remain strict because they drive runtime values.
                if pin['direction']=='input' and pin['links'] and pin['type']['category']=='byte' and pin['type']['object']!='None':
                    count+=bool(pin['default']);pin['default']=''
    return value,count

def verify_batch(before,after,graphs):
    b,bc=normalized(before);a,ac=normalized(after)
    bg={g['path']:g for g in b['graphs']};ag={g['path']:g for g in a['graphs']}
    assert bg.keys()==ag.keys(),'Graph set changed'
    assert set(graphs)<=bg.keys(),'Unknown requested graph'
    for path,g in bg.items():
        assert canonical_graph(g)==canonical_graph(ag[path]),'Logic/metadata changed: '+path
        if path not in graphs:
            def geometry(g):return {n['id']:(n['x'],n['y'],n.get('width'),n.get('height')) for n in g['nodes']}
            assert geometry(g)==geometry(ag[path]),'Unselected graph moved: '+path
    metadata=copy.deepcopy(a);metadata['graphs']=copy.deepcopy(b['graphs'])
    if bg:verify_blueprint(b,metadata,next(iter(bg)))
    else:assert b==a
    changes=[]
    old={(g['path'],n['id'],p['name']):p for g in before['graphs'] for n in g['nodes'] for p in n['pins'] if p['direction']=='input' and p['links'] and p['type']['category']=='byte' and p['type']['object']!='None'}
    for g in after['graphs']:
        for n in g['nodes']:
            for p in n['pins']:
                key=(g['path'],n['id'],p['name'])
                if key in old and p['direction']=='input' and p['links'] and old[key]['default']!=p['default']:
                    changes.append({'graph':g['path'],'node':n['id'],'pin':p['name'],'before':old[key]['default'],'after':p['default']})
    float_changes=[];numeric_changes=[]
    old_pins={(g['path'],n['id'],p['name'],p['direction']):p for g in before['graphs'] for n in g['nodes'] for p in n['pins']}
    for g in after['graphs']:
        nodes={n['id']:n for n in g['nodes']}
        for n in g['nodes']:
            for p in n['pins']:
                prior=old_pins.get((g['path'],n['id'],p['name'],p['direction']))
                if prior and connected_numeric_input(n,p,nodes):
                    was=LEGACY_FLOAT_FLAG in prior['type']['unreal_text'];now=LEGACY_FLOAT_FLAG in p['type']['unreal_text']
                    if was!=now:float_changes.append({'graph':g['path'],'node':n['id'],'pin':p['name'],'before':was,'after':now})
                    if prior['default']!=p['default']:numeric_changes.append({'graph':g['path'],'node':n['id'],'pin':p['name'],'before':prior['default'],'after':p['default']})
    order_changed=[x['key'] for x in before.get('input_key_bindings',[])]!=[x['key'] for x in after.get('input_key_bindings',[])]
    return {'unused_connected_numeric_default_changes':numeric_changes,'legacy_connected_float_flag_changes':float_changes,'distinct_key_binding_order_changed':order_changed,'unused_enum_default_changes':changes,'logic_preserved':True,'unselected_graphs_unchanged':True,'enum_default_normalization_before':bc,'enum_default_normalization_after':ac}
