"""Translation-only layout and whole-graph wire audit. No topology edits."""
import copy
import math
from statistics import median

COMMENT = '/Script/UnrealEd.EdGraphNode_Comment'


def extent(n):
    if n['class'] == COMMENT:
        return max(100, n.get('width', 0)), max(100, n.get('height', 0))
    count = max(sum(p['direction'] == d for p in n['pins']) for d in ('input', 'output'))
    return max(260, min(480, 100 + len(n['title']) * 10), n.get('width', 0)), max(100, 70 + count * 28, n.get('height', 0))


def wires(graph):
    ids = {n['id'] for n in graph['nodes']}
    return [(n['id'], l['node'], p['type']['category'] == 'exec')
            for n in graph['nodes'] for p in n['pins'] if p['direction'] == 'output'
            for l in p['links'] if l['node'] in ids]


def segments(graph, offsets=None, owner=None):
    """Estimated pin-to-pin segments; not Slate spline measurements."""
    nodes = {n['id']: n for n in graph['nodes']}
    def point(n, pin):
        dx, dy = offsets[owner[n['id']]] if offsets is not None else (0, 0)
        side = [p for p in n['pins'] if p['direction'] == pin['direction']]
        return (n['x'] + dx + (extent(n)[0] if pin['direction'] == 'output' else 0),
                n['y'] + dy + 55 + side.index(pin) * 28)
    result = []
    for n in graph['nodes']:
        for p in n['pins']:
            if p['direction'] != 'output': continue
            for link in p['links']:
                target = nodes.get(link['node'])
                if target is None: continue
                tp = next((q for q in target['pins'] if q['direction']=='input' and q['name']==link['pin']),None)
                if tp is not None: result.append((n['id'],target['id'],point(n,p),point(target,tp)))
    return result


def crossing(a, b):
    # Shared-node fan-out/fan-in is intentional, not an independent wire crossing.
    if set(a[:2]) & set(b[:2]): return False
    p,q=a[2:]; r,s=b[2:]
    if max(p[0],q[0]) <= min(r[0],s[0]) or max(r[0],s[0]) <= min(p[0],q[0]): return False
    if max(p[1],q[1]) <= min(r[1],s[1]) or max(r[1],s[1]) <= min(p[1],q[1]): return False
    def orient(x,y,z): return (y[0]-x[0])*(z[1]-x[1])-(y[1]-x[1])*(z[0]-x[0])
    return orient(p,q,r)*orient(p,q,s)<0 and orient(r,s,p)*orient(r,s,q)<0


def crossing_count(lines):
    return sum(crossing(a,b) for i,a in enumerate(lines) for b in lines[i+1:])


def audit(graph, groups=(), threshold=3000):
    nodes = {n['id']: n for n in graph['nodes']}
    owner = {nid: i for i, group in enumerate(groups) for nid in group}
    rows = []
    for a, b, execution in wires(graph):
        source, target = nodes[a], nodes[b]
        length = math.hypot(target['x'] - source['x'], target['y'] - source['y'])
        cross = owner.get(a, a) != owner.get(b, b)
        rows.append({'source': a, 'target': b, 'execution': execution, 'cross_group': cross, 'length': round(length, 2)})
    def stats(items):
        lengths = sorted(r['length'] for r in items)
        return {'count': len(items), 'long_count': sum(x > threshold for x in lengths),
                'maximum': max(lengths, default=0), 'total': round(sum(lengths), 2)}
    return {'threshold': threshold, 'estimated_crossings':crossing_count(segments(graph)), 'all': stats(rows), 'cross_group': stats([r for r in rows if r['cross_group']]),
            'execution': stats([r for r in rows if r['execution']]),
            'data': stats([r for r in rows if not r['execution']]),
            'long_edges': sorted([r for r in rows if r['length'] > threshold], key=lambda r: -r['length'])}


def spatial_groups(graph):
    """Keep existing comment regions rigid; merge intersecting regions conservatively.

    Export has no authoritative comment membership. Include nodes whose origin lies
    in a comment. Remaining nearby connected nodes form rigid islands.
    """
    nodes = {n['id']: n for n in graph['nodes']}
    parent = {nid: nid for nid in nodes}
    def root(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    def union(a, b):
        parent[root(b)] = root(a)
    comments = [n for n in nodes.values() if n['class'] == COMMENT]
    covered = set()
    for c in comments:
        w, h = extent(c)
        for n in nodes.values():
            if c['x'] <= n['x'] < c['x'] + w and c['y'] <= n['y'] < c['y'] + h:
                union(c['id'], n['id'])
                covered.add(n['id'])
    for a, b, _ in wires(graph):
        if a not in covered and b not in covered and math.hypot(nodes[a]['x']-nodes[b]['x'], nodes[a]['y']-nodes[b]['y']) < 1200:
            union(a, b)
    groups = {}
    for nid in nodes:
        groups.setdefault(root(nid), []).append(nid)
    return list(groups.values())


def compact(graph, groups=None, movable=None, threshold=3000):
    """Move rigid blocks using cross-boundary wires, including fixed outside nodes.

    Every accepted move reduces weighted squared length; collisions and maximum
    cross-wire regression are rejected. Unconnected blocks remain fixed.
    """
    groups = spatial_groups(graph) if groups is None else [list(g) for g in groups]
    nodes = {n['id']: n for n in graph['nodes']}
    flat = [nid for g in groups for nid in g]
    if len(flat) != len(set(flat)) or not set(flat) <= nodes.keys():
        raise ValueError('Invalid or overlapping groups')
    groups += [[nid] for nid in nodes if nid not in set(flat)]
    owner = {nid: i for i, g in enumerate(groups) for nid in g}
    movable = set(range(len(groups))) if movable is None else set(movable)
    boxes = []
    for g in groups:
        boxes.append([min(nodes[n]['x'] for n in g), min(nodes[n]['y'] for n in g),
                      max(nodes[n]['x'] + extent(nodes[n])[0] for n in g),
                      max(nodes[n]['y'] + extent(nodes[n])[1] for n in g)])
    offsets = [[0, 0] for _ in groups]
    cross = [(a, b, e) for a, b, e in wires(graph) if owner[a] != owner[b]]
    incident = {i: [edge for edge in cross if i in (owner[edge[0]], owner[edge[1]])] for i in movable}
    def point(nid):
        dx, dy = offsets[owner[nid]]
        return nodes[nid]['x'] + dx, nodes[nid]['y'] + dy
    def lengths(edges):
        result = []
        for a, b, e in edges:
            ax, ay = point(a); bx, by = point(b)
            result.append(((bx-ax)**2+(by-ay)**2, 2 if e else 1))
        return result
    original_max = max((v for v, _ in lengths(cross)), default=0)
    def rect(i, offset):
        x, y = offset; a, b, c, d = boxes[i]
        return a+x, b+y, c+x, d+y
    def overlaps(a, b):
        return a[0] < b[2]+60 and b[0] < a[2]+60 and a[1] < b[3]+60 and b[1] < a[3]+60
    for iteration in range(16):
        changed = False
        order = sorted(movable, key=lambda i: -sum(v*w for v,w in lengths(incident[i])))
        for i in order:
            links = incident[i]
            if not links: continue
            old = offsets[i][:]
            lines = segments(graph,offsets,owner)
            affected = {k for k,line in enumerate(lines) if i in (owner[line[0]],owner[line[1]])}
            pairs = [(a,b) for a in range(len(lines)) for b in range(a+1,len(lines)) if a in affected or b in affected]
            def crossings_at(candidate):
                dx,dy=candidate[0]-old[0],candidate[1]-old[1]
                shifted=lines[:]
                for k in affected:
                    a,b,p,q=lines[k]
                    if owner[a]==i:p=(p[0]+dx,p[1]+dy)
                    if owner[b]==i:q=(q[0]+dx,q[1]+dy)
                    shifted[k]=(a,b,p,q)
                return sum(crossing(shifted[a],shifted[b]) for a,b in pairs)
            old_crossings = crossings_at(old)
            old_cost = sum(v*w for v,w in lengths(links)) + old_crossings * 6000000
            targets = []
            for a, b, execution in links:
                local, remote = (a,b) if owner[a] == i else (b,a)
                rx, ry = point(remote)
                targets.append((rx-nodes[local]['x'] + (-550 if local == a else 550), ry-nodes[local]['y']))
            tx = round(median(t[0] for t in targets)); ty = round(median(t[1] for t in targets))
            candidates = {(tx,ty), (tx,old[1]), (old[0],ty), tuple(old)}
            for j in range(len(groups)):
                if j == i: continue
                a,b,c,d = rect(j, offsets[j]); l,t,r,s = boxes[i]
                candidates.update(((round(c+100-l),ty),(round(a-100-r),ty),(tx,round(d+100-t)),(tx,round(b-100-s))))
            best = old; best_cost = old_cost
            for candidate in sorted(candidates):
                if max(abs(v) for v in rect(i,candidate)) > 1000000: continue
                if any(overlaps(rect(i,candidate),rect(j,offsets[j])) for j in range(len(groups)) if j != i): continue
                offsets[i] = candidate
                values = lengths(links)
                count = crossings_at(candidate)
                cost = sum(v*w for v,w in values) + count * 6000000
                if count <= old_crossings and cost < best_cost - 1 and max((v for v,_ in values),default=0) <= original_max:
                    best = list(candidate); best_cost = cost
            offsets[i] = best
            changed |= best != old
        if not changed: break
    after = copy.deepcopy(graph)
    operations = []
    for n in after['nodes']:
        dx, dy = offsets[owner[n['id']]]
        n['x'] += dx; n['y'] += dy
        if dx or dy:
            operations.append({'op':'move_node','graph':graph['path'],'node':n['id'],'x':n['x'],'y':n['y']})
    return {'operations':operations}, {'graph':graph['path'], 'removed':[], 'blocks':[], 'translation_only':True,
        'groups':groups, 'before':audit(graph,groups,threshold), 'after':audit(after,groups,threshold)}
