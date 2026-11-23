"""Presentation planning: scoped dead-node pruning, entry-local DAG layout and comment blocks."""
from collections import defaultdict, deque
import copy
from WorkbenchCrossGroup import audit, compact

COMMENT='/Script/UnrealEd.EdGraphNode_Comment'
ORDINARY={'K2Node_CallFunction','K2Node_IfThenElse','K2Node_VariableGet','K2Node_VariableSet','K2Node_DynamicCast','K2Node_Knot','K2Node_BreakStruct','K2Node_GetEnumeratorNameAsString'}

def ordinary(n):
    return n['class'].rsplit('.',1)[-1] in ORDINARY and not n.get('latent',False)

def edges(n, direction='output', execution=None):
    return [(p,l) for p in n['pins'] if p['direction']==direction and (execution is None or (p['type']['category']=='exec')==execution) for l in p['links']]

def closure(nodes, starts, direction='output', execution=True):
    seen=set(); pending=list(starts)
    while pending:
        nid=pending.pop()
        if nid in seen or nid not in nodes: continue
        seen.add(nid)
        pending.extend(l['node'] for _,l in edges(nodes[nid],direction,execution))
    return seen

def live_nodes(nodes):
    roots={nid for nid,n in nodes.items() if n['class']!=COMMENT and not ordinary(n)}
    live=closure(nodes,roots)
    # Executed nodes and protected special nodes keep all their data producers alive.
    return closure(nodes,live,'input',False)

def unused_candidates(nodes, candidates):
    remove={nid for nid in candidates if nid in nodes and ordinary(nodes[nid])}-live_nodes(nodes)
    # Do not cut any consumer outside the explicit set or any incoming execution/delegate boundary.
    while True:
        unsafe={nid for nid in remove if any(l['node'] not in remove for _,l in edges(nodes[nid])) or
                any(l['node'] not in remove and p['type']['category'] in ('exec','delegate','mcdelegate') for p,l in edges(nodes[nid],'input'))}
        if not unsafe: return remove
        remove-=unsafe

def size(n):
    if n['class']==COMMENT: return max(n.get('width',0),100),max(n.get('height',0),100)
    inputs=sum(p['direction']=='input' for p in n['pins'])
    outputs=len(n['pins'])-inputs
    width=max(260,min(480,100+len(n['title'])*10),n.get('width',0))
    height=max(100,70+max(inputs,outputs)*28,n.get('height',0))
    return width,height

def intersect(a,b,gap=40):
    return a[0]<b[2]+gap and a[2]+gap>b[0] and a[1]<b[3]+gap and a[3]+gap>b[1]

def layout_group(nodes, members, entry):
    successors={nid:set() for nid in members}; incoming={nid:set() for nid in members}
    for nid in members:
        for _,link in edges(nodes[nid]):
            if link['node'] in members:
                successors[nid].add(link['node']);incoming[link['node']].add(nid)
    count={n:len(p) for n,p in incoming.items()}; ranks={n:0 for n in members}
    queue=deque(sorted(n for n,c in count.items() if c==0));order=[]
    while queue:
        n=queue.popleft();order.append(n)
        for target in sorted(successors[n]):
            ranks[target]=max(ranks[target],ranks[n]+1)
            count[target]-=1
            if count[target]==0:queue.append(target)
    if len(order)!=len(members): raise ValueError('Layout contains a cycle; keep existing positions and review this group')
    # Pure producers can start late: keep them next to the first consumer instead of at the far left.
    for nid in reversed(order):
        if nodes[nid].get('pure',False) and successors[nid]:
            ranks[nid]=max(ranks[nid],min(ranks[t] for t in successors[nid])-1)
    # Align the main execution chain; false/failed alternatives occupy lower lanes.
    lane={entry:0}
    for nid in order:
        for pin,link in edges(nodes[nid],execution=True):
            if link['node'] in members:
                lane[link['node']]=max(lane.get(link['node'],0),lane.get(nid,0)+(pin['name'] in ('else','CastFailed')))
    for nid in reversed(order):
        if nid not in lane: lane[nid]=min((lane[t] for t in successors[nid] if t in lane),default=0)
    widths={rank:max(size(nodes[n])[0] for n in members if ranks[n]==rank) for rank in set(ranks.values())}
    xpos={};x=80
    for rank in sorted(widths):xpos[rank]=x;x+=widths[rank]+110
    placed={};occupied=defaultdict(list)
    for nid in sorted(order,key=lambda n:(ranks[n],n!=entry,nodes[n]['title']=='Print String',nodes[n].get('pure',False),lane[n],n)):
        w,h=size(nodes[nid]); y=100+lane[nid]*620+(300 if nodes[nid].get('pure',False) else 0)
        while any(y<bottom+55 and y+h+55>top for top,bottom in occupied[ranks[nid]]):
            y=max(bottom+55 for top,bottom in occupied[ranks[nid]] if y<bottom+55 and y+h+55>top)
        placed[nid]=(xpos[ranks[nid]],y,w,h);occupied[ranks[nid]].append((y,y+h))
    width=x; height=max(y+h for _,y,_,h in placed.values())+80
    return placed,width,height

def nearest_free(origin,width,height,obstacles):
    ox,oy=origin
    xs={ox};ys={oy}
    for left,top,right,bottom in obstacles:
        xs.update((right+100,left-width-100));ys.update((bottom+100,top-height-100))
    xs=sorted(xs,key=lambda x:abs(x-ox))[:60];ys=sorted(ys,key=lambda y:abs(y-oy))[:100]
    for x,y in sorted(((x,y) for x in xs for y in ys),key=lambda p:abs(p[0]-ox)*1.5+abs(p[1]-oy)):
        if not any(intersect((x,y,x+width,y+height),b) for b in obstacles): return x,y
    return ox,max((b[3] for b in obstacles),default=oy)+100

def plan(graph,groups,candidates=()):
    nodes={n['id']:n for n in graph['nodes']}
    removed=unused_candidates(nodes,set(candidates))
    members_all=set().union(*(set(g['nodes']) for g in groups))
    if removed & members_all: raise ValueError('A layout member was selected for deletion')
    if sum(len(set(g['nodes'])) for g in groups)!=len(members_all): raise ValueError('Shared nodes must not be assigned to multiple groups')
    obstacles=[(n['x'],n['y'],n['x']+size(n)[0],n['y']+size(n)[1]) for nid,n in nodes.items() if nid not in members_all|removed]
    ops=[];blocks=[]
    if removed:ops.append({'op':'remove_unused_nodes','graph':graph['path'],'nodes':sorted(removed)})
    for i,group in enumerate(groups):
        entry=nodes[group['entry']]
        positions,width,height=layout_group(nodes,set(group['nodes']),group['entry'])
        anchor=(entry['x']-positions[group['entry']][0],entry['y']-positions[group['entry']][1])
        x,y=nearest_free(anchor,width,height,obstacles)
        for nid,(px,py,_,_) in positions.items():
            ops.append({'op':'move_node','graph':graph['path'],'node':nid,'x':x+px,'y':y+py})
        ops.append({'op':'add_node','graph':graph['path'],'kind':'comment','id':'layout_comment_'+str(i),'x':x,'y':y,'width':width,'height':height,'text':group['title'],'details':group['details'],'members':sorted(group['nodes'])})
        obstacles.append((x,y,x+width,y+height))
        blocks.append({'title':group['title'],'entry':group['entry'],'nodes':sorted(group['nodes']),'bounds':[x,y,x+width,y+height]})
    # Also optimize block-to-block and block-to-fixed-node wires after local layout.
    draft=copy.deepcopy(graph)
    draft['nodes']=[n for n in draft['nodes'] if n['id'] not in removed]
    by_id={n['id']:n for n in draft['nodes']}
    rigid=[]
    for op in ops:
        if op['op']=='move_node':by_id[op['node']].update(x=op['x'],y=op['y'])
        elif op['op']=='add_node':
            draft['nodes'].append({'id':op['id'],'class':COMMENT,'title':'Comment','pins':[],
                'x':op['x'],'y':op['y'],'width':op['width'],'height':op['height']})
            rigid.append(op['members']+[op['id']])
    adjustment,_=compact(draft,rigid,movable=range(len(rigid)))
    final={op['node']:op for op in adjustment['operations']}
    for op in ops:
        nid=op.get('node',op.get('id'))
        if nid in final:op.update(x=final[nid]['x'],y=final[nid]['y'])
    for block,op in zip(blocks,[o for o in ops if o['op']=='add_node']):
        block['bounds']=[op['x'],op['y'],op['x']+op['width'],op['y']+op['height']]
    return {'operations':ops},{'graph':graph['path'],'removed':sorted(removed),'preserved_candidates':sorted(set(candidates)-removed),'blocks':blocks}

def verify_logic(before,result,removed):
    if not result['success'] or result.get('compile_errors')!=0:raise ValueError(result.get('error','Compile failed'))
    after=result['blueprint']; mapping=result.get('source_nodes',{})
    # Layout and comment edits must not change any surviving pin type, value or link.
    for bg in before['graphs']:
        ag=next(g for g in after['graphs'] if g['path']==bg['path'])
        actual={n['id']:n for n in ag['nodes']}
        def mapped(n):return mapping.get(bg['path']+'|'+n,n)
        for n in bg['nodes']:
            if n['id'] in removed:continue
            target=actual[mapped(n['id'])]
            assert n['class']==target['class'],n['title']
            def pins(node,translate):
                values=[]
                for p in node['pins']:
                    item=copy.deepcopy(p);item.pop('id',None)
                    item['links']=[{'node':mapped(l['node']) if translate else l['node'],'pin':l['pin']} for l in p['links'] if not translate or l['node'] not in removed]
                    item['links'].sort(key=lambda l:(l['node'],l['pin']))
                    if translate:
                        # The preview is a copied Blueprint, so self-class references legitimately rebase.
                        import json
                        item=json.loads(json.dumps(item).replace(before['asset'],after['asset']))
                        # Custom-event delegate type metadata carries the event node's GUID.
                        import re
                        event_guids={n['id']:mapping.get(g['path']+'|'+n['id'],n['id']) for g in before['graphs'] for n in g['nodes'] if n['class'].rsplit('.',1)[-1] in ('K2Node_CustomEvent','K2Node_Event','K2Node_ComponentBoundEvent')}
                        item['type']['unreal_text']=re.sub(r'MemberGuid=([0-9A-F]{32})',lambda m:'MemberGuid='+event_guids.get(m[1],m[1]),item['type']['unreal_text'])
                    values.append(item)
                return sorted(values,key=lambda p:(p['name'],p['direction']))
            assert pins(n,True)==pins(target,False),('changed logic',bg['path'],n['title'])
        assert all(mapped(nid) not in actual for nid in removed if any(n['id']==nid for n in bg['nodes']))
    for field in ('variables','widgets','widget_bindings','widget_animations','parent'):
        if field in before:
            import json
            expected=json.loads(json.dumps(before[field]).replace(before['asset'],after['asset']))
            assert expected==after[field],field

def verify_presentation(before,result,patch,description):
    after=result['blueprint'];mapping=result.get('source_nodes',{})
    positions={(op['graph'],op['node']):(op['x'],op['y']) for op in patch['operations'] if op['op']=='move_node'}
    removed=set(description['removed'])
    metrics={'execution_edges':0,'backward_before':0,'backward_after':0,'max_exec_span_before':0,'max_exec_span_after':0}
    for graph in before['graphs']:
        actual={n['id']:n for g in after['graphs'] if g['path']==graph['path'] for n in g['nodes']}
        def mapped(n):return mapping.get(graph['path']+'|'+n,n)
        for n in graph['nodes']:
            if n['id'] in removed:continue
            target=actual[mapped(n['id'])]
            expected=positions.get((graph['path'],n['id']),(n['x'],n['y']))
            assert (target['x'],target['y'])==expected,('unexpected position',n['title'])
            assert n.get('comment','')==target.get('comment',''),('changed existing comment',n['title'])
        old={n['id']:n for n in graph['nodes']}
        for block in description['blocks'] if graph['path']==description.get('graph','EventGraph') else []:
            members=set(block['nodes'])
            rectangles=[]
            for nid in members:
                n=actual[mapped(nid)];w,h=size(n);rectangles.append((n['x'],n['y'],n['x']+w,n['y']+h))
                for _,link in edges(old[nid],execution=True):
                    if link['node'] not in members:continue
                    target=actual[mapped(link['node'])]
                    metrics['execution_edges']+=1
                    metrics['backward_before']+=old[link['node']]['x']<=old[nid]['x']
                    metrics['backward_after']+=target['x']<=n['x']
                    metrics['max_exec_span_before']=max(metrics['max_exec_span_before'],abs(old[link['node']]['x']-old[nid]['x']))
                    metrics['max_exec_span_after']=max(metrics['max_exec_span_after'],abs(target['x']-n['x']))
            for i,a in enumerate(rectangles):
                for b in rectangles[i+1:]:assert not intersect(a,b,0),'estimated node overlap'
        survivors={mapped(n['id']) for n in graph['nodes'] if n['id'] not in removed}
        assert all(n['class']==COMMENT for nid,n in actual.items() if nid not in survivors),'unexpected added gameplay node'
    for op in patch['operations']:
        if op['op']=='add_node' and op.get('kind')=='comment':
            n=next(n for g in after['graphs'] for n in g['nodes'] if n['id']==result['aliases'][op['id']])
            assert (n['comment'],n.get('details'),n['width'],n['height'])==(op['text'],op['details'],op['width'],op['height'])
    if not description.get('translation_only'):
        assert metrics['backward_after']==0
    for bg in before['graphs']:
        if bg['path'] != description['graph']: continue
        ag=copy.deepcopy(next(g for g in after['graphs'] if g['path']==bg['path']))
        reverse={mapping.get(bg['path']+'|'+n['id'],n['id']):n['id'] for n in bg['nodes']}
        for n in ag['nodes']:
            n['id']=reverse.get(n['id'],n['id'])
            for p in n['pins']:
                for link in p['links']:link['node']=reverse.get(link['node'],link['node'])
        groups=description.get('groups',[b['nodes'] for b in description['blocks']])
        metrics['whole_graph_before']=audit(bg,groups,description.get('after',{}).get('threshold',3000))
        metrics['whole_graph_after']=audit(ag,groups,description.get('after',{}).get('threshold',3000))
        if description.get('translation_only'):
            assert metrics['whole_graph_after']==description['after'], 'Cross-group result differs from plan'
            original={n['id']:n for n in bg['nodes']}
            actual={n['id']:n for n in ag['nodes']}
            for group in groups:
                translations={(actual[n]['x']-original[n]['x'],actual[n]['y']-original[n]['y']) for n in group}
                assert len(translations)==1, 'Rigid group distorted'
            assert metrics['whole_graph_after']['cross_group']['maximum'] <= metrics['whole_graph_before']['cross_group']['maximum']
    return metrics
