"""Find actual K2 formatting roots; purity does not exclude input events."""
PRIMARY={'K2Node_FunctionEntry','K2Node_Event','K2Node_ComponentBoundEvent','K2Node_CustomEvent','K2Node_EnhancedInputAction','K2Node_InputKey','K2Node_InputAction','K2Node_InputAxisEvent','K2Node_InputDebugKey'}
FALLBACK={'K2Node_FunctionResult','K2Node_Tunnel'}

def graph_roots(graph):
    nodes={n['id']:n for n in graph['nodes']};roots=[];covered=set()
    def walk(starts,execution=False):
        seen=set();todo=list(starts)
        while todo:
            nid=todo.pop()
            if nid in seen or nid not in nodes:continue
            seen.add(nid)
            for pin in nodes[nid]['pins']:
                if execution and (pin['direction']!='output' or pin['type']['category']!='exec'):continue
                todo.extend(link['node'] for link in pin['links'])
        return seen
    for classes in (PRIMARY,FALLBACK):
        for nid,node in nodes.items():
            if node['class'].split('.')[-1] not in classes or not any(p['links'] for p in node['pins']):continue
            if classes==FALLBACK and nid in covered:continue
            roots.append(nid);covered.update(walk([nid]))
    executed=walk(roots,True)
    # A timeline may autoplay or be controlled through its component externally.
    # Data-wire sharing with another event does not prove its update chain was formatted.
    for nid,node in nodes.items():
        if node['class'].endswith('.K2Node_Timeline') and nid not in executed and any(p['links'] for p in node['pins']):
            roots.append(nid);executed.update(walk([nid],True))
    return roots
