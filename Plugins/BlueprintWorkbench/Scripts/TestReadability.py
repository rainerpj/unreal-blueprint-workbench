import copy
import unittest
from TestPresentation import node, connect
from WorkbenchReadability import plan_readability, verify_readability
from WorkbenchCrossGroup import crossing_count, segments, compact, audit


class ReadabilityTests(unittest.TestCase):
    def fixture(self):
        a=node('read','K2Node_VariableGet',True);a['localizable_read']=True
        b=node('near',x=600);c=node('far',x=600,y=4000)
        connect(a,b,'bool','Value','Condition');connect(a,c,'bool','Value','Condition')
        return {'path':'EventGraph','nodes':[a,b,c]}

    def test_fanout_localization_preserves_semantics(self):
        graph=self.fixture();before={'asset':'/Game/Test.Test','graphs':[graph]}
        patch,report=plan_readability(graph)
        self.assertEqual(len(report['copies']),1)
        result={'success':True,'compile_errors':0,'blueprint':{'asset':before['asset'],'graphs':[copy.deepcopy(report['expected_graph'])]}}
        verify_readability(before,result,patch,report)
        self.assertEqual(len(graph['nodes']),3)
        self.assertEqual(len(graph['nodes'][0]['pins'][0]['links']),2)
        # Replacing a safe read with an unrelated variable must fail verification.
        clone=result['blueprint']['graphs'][0]['nodes'][-1]
        clone['pins'][0]['name']='OtherVariable'
        with self.assertRaises((AssertionError,StopIteration)):verify_readability(before,result,patch,report)

    def test_pure_call_or_uncertified_getter_is_never_copied(self):
        for kind in ('K2Node_CallFunction','K2Node_VariableGet'):
            graph=self.fixture();graph['nodes'][0]['class']='/Script/BlueprintGraph.'+kind
            graph['nodes'][0].pop('localizable_read')
            patch,report=plan_readability(graph)
            self.assertFalse(report['copies']);self.assertTrue(report['skipped'])
            self.assertTrue(all(o['op']=='move_node' for o in patch['operations']))

    def test_reference_read_rejected_and_copy_limit_respected(self):
        g=self.fixture();g['nodes'][0]['pins'][0]['type']['reference']=True
        with self.assertRaises(ValueError):plan_readability(g)
        _,report=plan_readability(self.fixture(),max_copies=0)
        self.assertFalse(report['copies'])

    def test_crossings_measured_and_reduced_without_topology_change(self):
        a=node('a',x=0,y=0);b=node('b',x=2000,y=1000)
        c=node('c',x=0,y=1000);d=node('d',x=2000,y=0)
        connect(a,b);connect(c,d)
        g={'path':'EventGraph','nodes':[a,b,c,d]}
        self.assertEqual(crossing_count(segments(g)),1)
        patch,report=compact(g,[[n['id']] for n in g['nodes']])
        self.assertLess(report['after']['estimated_crossings'],report['before']['estimated_crossings'])
        self.assertTrue(all(o['op']=='move_node' for o in patch['operations']))

    def test_shared_endpoint_is_not_counted_as_crossing(self):
        self.assertEqual(audit(self.fixture())['estimated_crossings'],0)


if __name__=='__main__':unittest.main()
