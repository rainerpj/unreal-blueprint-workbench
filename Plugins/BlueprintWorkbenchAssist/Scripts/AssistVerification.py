"""Compare Blueprint logic while allowing only transparent reroute-node changes."""
import copy
import re

KNOT='/Script/BlueprintGraph.K2Node_Knot'
COMMENT='/Script/UnrealEd.EdGraphNode_Comment'


def canonical_graph(graph):
    nodes={n['id']:n for n in graph['nodes']}
    def endpoints(link,direction,visited=()):
        nid=link['node']
        if nid not in nodes:raise AssertionError('Dangling node link')
        n=nodes[nid]
        if n['class']!=KNOT:return {(nid,link['pin'])}
        if nid in visited:raise AssertionError('Reroute cycle')
        assert len(n['pins'])==2 and {p['direction'] for p in n['pins']}=={'input','output'},'Unexpected reroute shape'
        follow=next(p for p in n['pins'] if p['direction']==direction)
        result=set()
        for next_link in follow['links']:result.update(endpoints(next_link,direction,visited+(nid,)))
        return result
    result={}
    for n in graph['nodes']:
        if n['class']==KNOT:continue
        item=copy.deepcopy(n)
        for field in ('x','y','width','height'):item.pop(field,None)
        pins=[]
        for pin in item['pins']:
            pin.pop('id',None)
            connections=set()
            for link in pin['links']:connections.update(endpoints(link,pin['direction']))
            pin['links']=sorted(connections)
            pins.append(pin)
        item['pins']=sorted(pins,key=lambda p:(p['direction'],p['name']))
        result[n['id']]=item
    return result


def verify_blueprint(before,after,target_graph):
    bg={g['path']:g for g in before['graphs']};ag={g['path']:g for g in after['graphs']}
    assert bg.keys()==ag.keys(),'Graphs added or removed'
    for path,g in bg.items():
        assert canonical_graph(g)==canonical_graph(ag[path]),'Changed logic/metadata: '+path
        if path!=target_graph:
            def layout(graph):return {n['id']:(n['x'],n['y'],n.get('width'),n.get('height')) for n in graph['nodes']}
            assert layout(g)==layout(ag[path]),'Changed another graph layout: '+path
    for field in before:
        if field=='graphs':continue
        if field=='input_key_bindings':
            # UE recompilation allocates fresh generated input-event function
            # suffixes. Key/event/order and every EventGraph node/pin remain checked.
            def bindings(items):
                result=copy.deepcopy(items)
                for item in result:
                    item['function']=re.sub(r'(_K2Node_InputKeyEvent_)\d+$',r'\1<generated>',item['function'])
                return result
            assert bindings(before[field])==bindings(after.get(field,[])),'Changed input key bindings'
        else:assert before[field]==after.get(field),'Changed Blueprint metadata: '+field
    g=bg[target_graph];a=ag[target_graph]
    original={n['id']:n for n in g['nodes']}
    return {'logic_preserved':True,'other_graphs_unchanged':True,
            'nodes_before':len(g['nodes']),'nodes_after':len(a['nodes']),
            'reroutes_before':sum(n['class']==KNOT for n in g['nodes']),
            'reroutes_after':sum(n['class']==KNOT for n in a['nodes']),
            'moved_existing':sum(n['id'] in original and (n['x'],n['y'])!=(original[n['id']]['x'],original[n['id']]['y']) for n in a['nodes'])}
