import unittest
from BatchPlanning import graph_roots

def node(n,kind,pure=False):return {'id':n,'class':'/Script/BlueprintGraph.'+kind,'pure':pure,'pins':[]}
def connect(a,b,category='exec'):
    a['pins'].append({'direction':'output','type':{'category':category},'links':[{'node':b['id']}]})
    b['pins'].append({'direction':'input','type':{'category':category},'links':[{'node':a['id']}]})
class PlanningTests(unittest.TestCase):
    def test_input_root_does_not_depend_on_purity_hint(self):
        a=node('key','K2Node_InputKey',True);b=node('call','K2Node_CallFunction');connect(a,b)
        self.assertEqual(graph_roots({'nodes':[a,b]}),['key'])
    def test_pure_return_fallback(self):
        a=node('value','K2Node_VariableGet',True);b=node('return','K2Node_FunctionResult');connect(a,b,'float')
        self.assertEqual(graph_roots({'nodes':[a,b]}),['return'])
    def test_timeline_sharing_data_still_needs_its_own_root(self):
        e=node('event','K2Node_Event');call=node('call','K2Node_CallFunction');v=node('value','K2Node_VariableGet',True);t=node('timeline','K2Node_Timeline');out=node('update','K2Node_CallFunction')
        connect(e,call);connect(v,call,'float');connect(v,t,'float');connect(t,out)
        self.assertEqual(graph_roots({'nodes':[e,call,v,t,out]}),['event','timeline'])
if __name__=='__main__':unittest.main()
