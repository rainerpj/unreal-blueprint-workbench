"""Opt-in safe read localization followed by crossing-aware block placement."""
import copy
import math
import re
from WorkbenchCrossGroup import compact, audit, extent, COMMENT, segments, crossing


def refine_reads(graph):
    """Reduce internal crossings by moving pure nodes, within existing comments.

    Execution-node positions are untouched. Existing comment membership and
    non-overlap are constraints, not extra comment boxes or topology edits.
    """
    nodes={n['id']:n for n in graph['nodes']}
    comments=[n for n in graph['nodes'] if n['class']==COMMENT]
    for _ in range(3):
        changed=False
        for n in graph['nodes']:
            if not n.get('pure',False):continue
            lines=segments(graph)
            affected={i for i,line in enumerate(lines) if n['id'] in line[:2]}
            if not affected:continue
            pairs=[(a,b) for a in range(len(lines)) for b in range(a+1,len(lines)) if a in affected or b in affected]
            def evaluate(dx,dy):
                shifted=lines[:]
                for i in affected:
                    a,b,p,q=lines[i]
                    if a==n['id']:p=(p[0]+dx,p[1]+dy)
                    if b==n['id']:q=(q[0]+dx,q[1]+dy)
                    shifted[i]=(a,b,p,q)
                count=sum(crossing(shifted[a],shifted[b]) for a,b in pairs)
                lengths=[(shifted[i][2][0]-shifted[i][3][0])**2+(shifted[i][2][1]-shifted[i][3][1])**2 for i in affected]
                return count,sum(lengths),max(lengths,default=0)
            old=evaluate(0,0);best=(0,0);score=old[0]*6000000+old[1]
            w,h=extent(n)
            def inside(x,y,c):
                cw,ch=extent(c)
                return c['x']<=x and c['y']<=y and x+w<=c['x']+cw and y+h<=c['y']+ch
            membership={c['id']:inside(n['x'],n['y'],c) for c in comments}
            for dx,dy in ((0,-180),(0,180),(-320,0),(320,0),(0,-360),(0,360),(-320,-180),(-320,180)):
                x,y=n['x']+dx,n['y']+dy
                if abs(x)>1000000 or abs(y)>1000000:continue
                if any(inside(x,y,c)!=membership[c['id']] for c in comments):continue
                if any(other['id']!=n['id'] and other['class']!=COMMENT and x<other['x']+extent(other)[0]+30 and
                       other['x']<x+w+30 and y<other['y']+extent(other)[1]+30 and other['y']<y+h+30 for other in nodes.values()):continue
                count,length,maximum=evaluate(dx,dy);candidate=count*6000000+length
                if count<=old[0] and maximum<=max(old[2],1200**2) and candidate<score:
                    best=(dx,dy);score=candidate
            if best!=(0,0):n['x']+=best[0];n['y']+=best[1];changed=True
        if not changed:break


def plan_readability(graph, threshold=1200, max_copies=32):
    if threshold <= 0 or max_copies < 0: raise ValueError('Invalid localization limits')
    draft=copy.deepcopy(graph)
    nodes={n['id']:n for n in draft['nodes']}
    ops=[]; copies=[]; skipped=[]
    def free_position(target, source):
        w,h=extent(source)
        x=target['x']-w-100
        candidates=[(x,target['y']+dy) for dy in (0,180,-180,360,-360,540,-540,720,-720)]
        for x,y in candidates:
            if all(n['class']==COMMENT or x+w+30<=n['x'] or n['x']+extent(n)[0]+30<=x or
                   y+h+30<=n['y'] or n['y']+extent(n)[1]+30<=y for n in nodes.values()):return x,y
        return None
    for original in graph['nodes']:
        n=nodes[original['id']]
        outgoing=[(p,l) for p in n['pins'] if p['direction']=='output' for l in p['links']]
        targets={l['node'] for _,l in outgoing}
        if len(targets)<2:continue
        far=[(p,l) for p,l in outgoing if math.hypot(nodes[l['node']]['x']-n['x'],nodes[l['node']]['y']-n['y'])>threshold]
        if not far:continue
        # The engine certifies direct reads. A pure flag alone is never authority.
        if not n.get('localizable_read',False):
            skipped.append({'node':n['id'],'reason':'not certified as a direct safe read'})
            continue
        if n['class'].rsplit('.',1)[-1] not in ('K2Node_VariableGet','K2Node_Self'):
            raise ValueError('Invalid direct-read certification')
        if any(p['type']['category']=='exec' or p['type'].get('reference',False) or p['type'].get('container',0) or
               (p['direction']=='input' and p['links']) for p in n['pins']):raise ValueError('Unsafe certified read')
        keep=min(targets,key=lambda t:math.hypot(nodes[t]['x']-n['x'],nodes[t]['y']-n['y']))
        for target_id in sorted({l['node'] for _,l in far}-{keep}):
            links=[(p,l) for p,l in outgoing if l['node']==target_id]
            if len(copies)>=max_copies:
                skipped.append({'node':n['id'],'target':target_id,'reason':'copy limit'});continue
            position=free_position(nodes[target_id],n)
            if position is None:
                skipped.append({'node':n['id'],'target':target_id,'reason':'no nearby free position'});continue
            alias='layout_read_'+str(len(copies))
            if alias in nodes:raise ValueError('Generated alias collision')
            clone=copy.deepcopy(n);clone.update(id=alias,x=position[0],y=position[1])
            for p in clone['pins']:p['links']=[]
            ops.append({'op':'add_node','graph':graph['path'],'kind':'local_read','id':alias,'source_node':n['id'],'x':position[0],'y':position[1]})
            for p,link in links:
                tp=next(q for q in nodes[target_id]['pins'] if q['direction']=='input' and q['name']==link['pin'])
                if tp['type'].get('reference',False):raise ValueError('Reference consumer cannot be localized')
                for operation,source in [('disconnect',n['id']),('connect',alias)]:
                    ops.append({'op':operation,'graph':graph['path'],'from':{'node':source,'pin':p['name']},'to':{'node':target_id,'pin':link['pin']}})
                p['links'].remove(link)
                cp=next(q for q in clone['pins'] if q['name']==p['name'] and q['direction']=='output')
                cp['links'].append(copy.deepcopy(link))
                for reverse in tp['links']:
                    if reverse['node']==n['id'] and reverse['pin']==p['name']:reverse['node']=alias
            nodes[alias]=clone;draft['nodes'].append(clone)
            copies.append({'alias':alias,'source':n['id'],'target':target_id})
    placement,description=compact(draft,threshold=threshold)
    positions={op['node']:op for op in placement['operations']}
    for n in draft['nodes']:
        if n['id'] in positions:n.update(x=positions[n['id']]['x'],y=positions[n['id']]['y'])
    refine_reads(draft)
    original_positions={n['id']:(n['x'],n['y']) for n in graph['nodes']}
    positions={n['id']:{'op':'move_node','graph':graph['path'],'node':n['id'],'x':n['x'],'y':n['y']}
               for n in draft['nodes'] if original_positions.get(n['id'])!=(n['x'],n['y'])}
    for op in ops:
        if op['op']=='add_node' and op['id'] in positions:
            move=positions.pop(op['id']);op.update(x=move['x'],y=move['y'])
    ops.extend(positions.values())
    if audit(draft)['estimated_crossings']>audit(graph)['estimated_crossings']:
        # A local clone can create new crossings around its consumer. Keep the
        # safe translation-only plan instead of claiming a regression is cleaner.
        fallback,details=compact(graph,threshold=threshold)
        return fallback,dict(details,localization_skipped='localized placement increased estimated crossings')
    return {'operations':ops},{'graph':graph['path'],'readability':True,'removed':[], 'copies':copies,'skipped':skipped,
        'groups':description['groups'],'before':audit(graph,threshold=threshold),
        'after':audit(draft,description['groups'],threshold),'expected_graph':draft}


def verify_readability(before,result,patch,description):
    from WorkbenchPresentation import verify_logic
    if not result.get('success') or result.get('compile_errors')!=0:raise ValueError('Readability patch did not compile')
    actual=copy.deepcopy(result['blueprint']);path=description['graph']
    mapping=result.get('source_nodes',{})
    reverse={mapping.get(g['path']+'|'+n['id'],n['id']):n['id'] for g in before['graphs'] for n in g['nodes']}
    reverse.update({result.get('aliases',{}).get(c['alias'],c['alias']):c['alias'] for c in description['copies']})
    for g in actual['graphs']:
        for n in g['nodes']:
            n['id']=reverse.get(n['id'],n['id'])
            for p in n['pins']:
                if 'unreal_text' in p['type']:
                    p['type']['unreal_text']=re.sub(r'MemberGuid=([0-9A-F]{32})',lambda m:'MemberGuid='+reverse.get(m[1],m[1]),p['type']['unreal_text'])
                for l in p['links']:l['node']=reverse.get(l['node'],l['node'])
    graph=next(g for g in actual['graphs'] if g['path']==path)
    expected=description['expected_graph']
    assert {n['id']:(n['x'],n['y']) for n in graph['nodes']}=={n['id']:(n['x'],n['y']) for n in expected['nodes']},'Unexpected node/position'
    measured=audit(graph,description['groups'],description['after']['threshold'])
    assert measured==description['after'],'Layout differs from plan'
    # Collapse only declared direct-read aliases back to their source. This proves
    # every original consumer, execution edge and data dependency remains intact.
    original={n['id']:n for g in before['graphs'] for n in g['nodes']}
    nodes={n['id']:n for n in graph['nodes']}
    def shape(n):
        return (n['class'],[(p['name'],p['direction'],p['type'],p.get('default'),p.get('default_object'),p.get('default_text')) for p in n['pins']])
    for item in description['copies']:
        alias,source=item['alias'],item['source']
        assert original[source].get('localizable_read'), 'Source not certified'
        # Preview copies rebase self-class pin types, so compare within result.
        assert shape(nodes[alias])==shape(nodes[source]),'Clone differs from original read'
        for p in nodes[alias]['pins']:
            sp=next(q for q in nodes[source]['pins'] if q['name']==p['name'] and q['direction']==p['direction'])
            sp['links'].extend(p['links'])
        for n in graph['nodes']:
            for p in n['pins']:
                for link in p['links']:
                    if link['node']==alias:link['node']=source
        graph['nodes']=[n for n in graph['nodes'] if n['id']!=alias]
    normalized=dict(result,blueprint=actual,source_nodes={})
    verify_logic(before,normalized,set())
    for bg in before['graphs']:
        if bg['path']==path:continue
        ag=next(g for g in actual['graphs'] if g['path']==bg['path'])
        assert {n['id']:(n['x'],n['y']) for n in bg['nodes']}=={n['id']:(n['x'],n['y']) for n in ag['nodes']}
    return measured
