import copy
import unittest
from WorkbenchPresentation import closure,unused_candidates,plan,layout_group,verify_logic,intersect,size

def node(n,kind='K2Node_CallFunction',pure=False,x=0,y=0):
    return {'id':n,'class':'/Script/BlueprintGraph.'+kind,'title':n,'pure':pure,'x':x,'y':y,'pins':[]}
def connect(a,b,category='exec',out='then',inp='execute'):
    def pin(n,name,direction):
        found=next((p for p in n['pins'] if p['name']==name and p['direction']==direction),None)
        if found:return found
        p={'name':name,'direction':direction,'type':{'category':category,'unreal_text':category},'default':'','links':[]};n['pins'].append(p);return p
    pin(a,out,'output')['links'].append({'node':b['id'],'pin':inp})
    pin(b,inp,'input')['links'].append({'node':a['id'],'pin':out})

class PresentationTests(unittest.TestCase):
    def setUp(self):
        self.e=node('entry','K2Node_CustomEvent');self.b=node('branch','K2Node_IfThenElse');self.c=node('call');self.v=node('value','K2Node_VariableGet',True)
        self.d=node('dead','K2Node_IfThenElse');self.f=node('old_call')
        connect(self.e,self.b);connect(self.b,self.c);connect(self.v,self.b,'bool','value','Condition');connect(self.v,self.d,'bool','value','Condition');connect(self.d,self.f)
        self.nodes={n['id']:n for n in [self.e,self.b,self.c,self.v,self.d,self.f]}
    def test_inherited_math_calls_follow_same_liveness_rules(self):
        for kind in ('K2Node_PromotableOperator','K2Node_CommutativeAssociativeBinaryOperator'):
            math=node('math',kind,True);ns={'math':math}
            self.assertEqual(unused_candidates(ns,ns),{'math'})
            event=node('event','K2Node_CustomEvent');call=node('consumer')
            connect(event,call);connect(math,call,'int','result','value')
            ns.update(event=event,consumer=call)
            self.assertFalse(unused_candidates(ns,ns))
    def test_unused_value_chain_keeps_shared_producer(self):
        selfref=node('self','K2Node_Self',True);struct=node('struct','K2Node_MakeStruct',True)
        select=node('select','K2Node_Select',True);call=node('consumer');event=node('event','K2Node_CustomEvent')
        connect(selfref,struct,'object','self','owner');connect(struct,select,'struct','result','option')
        ns={n['id']:n for n in [selfref,struct,select,call,event]}
        connect(event,call);connect(selfref,call,'object','self','target')
        self.assertEqual(unused_candidates(ns,ns),{'struct','select'})
    def test_dispatcher_boundary_prevents_pruning(self):
        owner=node('owner','K2Node_CustomEvent');call=node('broadcast','K2Node_CallDelegate')
        ns={'owner':owner,'broadcast':call}
        self.assertEqual(unused_candidates(ns,ns),{'broadcast'})
        connect(owner,call,'mcdelegate','delegate','target')
        self.assertFalse(unused_candidates(ns,ns))
    def test_unused_delegate_and_constructors_require_no_live_consumer(self):
        for kind in ('K2Node_CreateDelegate','K2Node_SpawnActorFromClass','K2Node_CreateWidget'):
            source=node('source',kind);consumer=node('consumer');event=node('event','K2Node_CustomEvent')
            connect(source,consumer,'delegate' if kind=='K2Node_CreateDelegate' else 'object','value','value')
            ns={n['id']:n for n in (source,consumer,event)}
            self.assertEqual(unused_candidates(ns,ns),{'source','consumer'})
            connect(event,consumer)
            self.assertFalse(unused_candidates(ns,ns))
    def test_only_dead_scoped_chain_removed(self):
        self.assertEqual(unused_candidates(self.nodes,set(self.nodes)),{'dead','old_call'})
    def test_live_data_and_events_retained(self):
        self.assertFalse(unused_candidates(self.nodes,{'entry','value','branch'}))
    def test_incomplete_scope_does_not_cut_exec(self):
        self.assertFalse(unused_candidates(self.nodes,{'old_call'}))
    def test_latent_call_retained(self):
        self.f['latent']=True
        self.assertFalse(unused_candidates(self.nodes,{'dead','old_call'}))
    def test_dag_flows_left_to_right_without_overlap(self):
        members={'entry','branch','call','value'}
        pos,_,_=layout_group(self.nodes,members,'entry')
        for n in members:
            for p in self.nodes[n]['pins']:
                if p['direction']=='output':
                    for link in p['links']:
                        if link['node'] in members:self.assertLess(pos[n][0],pos[link['node']][0])
        boxes=[(x,y,x+w,y+h) for x,y,w,h in pos.values()]
        for i,a in enumerate(boxes):
            for b in boxes[i+1:]:self.assertFalse(intersect(a,b,0))
    def test_comment_avoids_unrelated_node(self):
        obstacle=node('unrelated','K2Node_CustomEvent',x=300,y=0);self.nodes['unrelated']=obstacle
        graph={'path':'EventGraph','nodes':list(self.nodes.values())}
        _,report=plan(graph,[{'entry':'entry','nodes':{'entry','branch','call','value'},'title':'Request','details':'Description'}],{'dead','old_call'})
        x,y=obstacle['x'],obstacle['y'];w,h=size(obstacle)
        self.assertFalse(intersect(report['blocks'][0]['bounds'],(x,y,x+w,y+h),0))
    def test_cycle_requires_review(self):
        connect(self.c,self.b)
        with self.assertRaises(ValueError):layout_group(self.nodes,{'entry','branch','call','value'},'entry')
    def test_logic_verifier_rejects_default_change(self):
        before={'asset':'/Game/Test.Test','graphs':[{'path':'EventGraph','nodes':list(self.nodes.values())}]}
        after=copy.deepcopy(before);after['graphs'][0]['nodes'][1]['pins'][0]['default']='different'
        with self.assertRaises(AssertionError):verify_logic(before,{'success':True,'compile_errors':0,'blueprint':after},set())

if __name__=='__main__':unittest.main()
