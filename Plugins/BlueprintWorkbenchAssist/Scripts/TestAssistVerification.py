import copy
import unittest
from AssistVerification import verify_blueprint, KNOT


def fixture():
    def pin(name,direction,node,target):return {'name':name,'direction':direction,'type':{'category':'exec'},'default':'','links':[{'node':node,'pin':target}]}
    return {'asset':'/Game/Test.Test','graphs':[{'path':'Function','nodes':[
        {'id':'a','class':'Event','title':'A','x':0,'y':0,'pins':[pin('then','output','b','execute')]},
        {'id':'b','class':'Call','title':'B','x':300,'y':0,'pins':[pin('execute','input','a','then')]}
    ]}]}


class AssistVerificationTests(unittest.TestCase):
    def test_transparent_reroute_and_movement_allowed(self):
        before=fixture();after=copy.deepcopy(before);nodes=after['graphs'][0]['nodes']
        nodes[0]['pins'][0]['links']=[{'node':'k','pin':'In'}]
        nodes[1]['pins'][0]['links']=[{'node':'k','pin':'Out'}]
        nodes.append({'id':'k','class':KNOT,'x':100,'y':0,'pins':[
            {'name':'In','direction':'input','links':[{'node':'a','pin':'then'}]},
            {'name':'Out','direction':'output','links':[{'node':'b','pin':'execute'}]}]})
        nodes[1]['y']=120
        self.assertTrue(verify_blueprint(before,after,'Function')['logic_preserved'])
    def test_broken_edge_rejected(self):
        before=fixture();after=copy.deepcopy(before);after['graphs'][0]['nodes'][0]['pins'][0]['links']=[]
        with self.assertRaises(AssertionError):verify_blueprint(before,after,'Function')
    def test_default_change_rejected(self):
        before=fixture();after=copy.deepcopy(before);after['graphs'][0]['nodes'][0]['pins'][0]['default']='changed'
        with self.assertRaises(AssertionError):verify_blueprint(before,after,'Function')
    def test_other_graph_layout_rejected(self):
        before=fixture();other=copy.deepcopy(before['graphs'][0]);other['path']='Other';before['graphs'].append(other)
        after=copy.deepcopy(before);after['graphs'][1]['nodes'][0]['x']=50
        with self.assertRaises(AssertionError):verify_blueprint(before,after,'Function')
    def test_gameplay_node_added_rejected(self):
        before=fixture();after=copy.deepcopy(before);n=copy.deepcopy(after['graphs'][0]['nodes'][0]);n['id']='new';after['graphs'][0]['nodes'].append(n)
        with self.assertRaises(AssertionError):verify_blueprint(before,after,'Function')
    def test_generated_binding_suffix_only_is_ignored(self):
        before=fixture();before['input_key_bindings']=[{'key':'P','event':0,'function':'InpActEvt_P_K2Node_InputKeyEvent_3'}]
        after=copy.deepcopy(before);after['input_key_bindings'][0]['function']='InpActEvt_P_K2Node_InputKeyEvent_12'
        verify_blueprint(before,after,'Function')
        after['input_key_bindings'][0]['key']='O'
        with self.assertRaises(AssertionError):verify_blueprint(before,after,'Function')
        after['input_key_bindings'][0]=dict(before['input_key_bindings'][0],function='CustomHandler')
        with self.assertRaises(AssertionError):verify_blueprint(before,after,'Function')


if __name__=='__main__':unittest.main()
