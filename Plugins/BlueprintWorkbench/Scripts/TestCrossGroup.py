import copy
import unittest
from TestPresentation import node, connect
from WorkbenchCrossGroup import compact, audit, spatial_groups, COMMENT
from WorkbenchPresentation import plan


class CrossGroupTests(unittest.TestCase):
    def fixture(self):
        a=node('a',x=0,y=0); b=node('b',x=0,y=14000)
        connect(a,b)
        return {'path':'EventGraph','nodes':[a,b]}

    def test_vertical_and_external_data_edges_are_audited(self):
        g=self.fixture(); connect(g['nodes'][0],g['nodes'][1],'bool','value','condition')
        report=audit(g,[['a']])
        self.assertEqual(report['cross_group']['long_count'],2)
        self.assertEqual(report['data']['maximum'],14000)

    def test_fixed_endpoint_is_considered_but_not_moved(self):
        g=self.fixture(); before=copy.deepcopy(g)
        patch,report=compact(g,[['a']],movable=[0])
        self.assertEqual(g,before)
        self.assertEqual({o['node'] for o in patch['operations']},{'a'})
        self.assertLess(report['after']['cross_group']['maximum'],3000)

    def test_comment_and_members_translate_together(self):
        g=self.fixture(); c=node('comment',x=-50,y=-50)
        c.update({'class':COMMENT,'width':600,'height':400})
        g['nodes'].append(c)
        groups=spatial_groups(g)
        self.assertTrue(any(set(group)=={'comment','a'} for group in groups))
        patch,_=compact(g)
        positions={o['node']:(o['x'],o['y']) for o in patch['operations']}
        if 'a' in positions:
            self.assertEqual(positions['a'][0]-positions['comment'][0],50)
            self.assertEqual(positions['a'][1]-positions['comment'][1],50)

    def test_cycles_do_not_require_logic_rewrite(self):
        g=self.fixture(); connect(g['nodes'][1],g['nodes'][0])
        patch,report=compact(g,[['a'],['b']])
        self.assertTrue(all(o['op']=='move_node' for o in patch['operations']))
        self.assertLess(report['after']['all']['maximum'],report['before']['all']['maximum'])

    def test_unconnected_nodes_stay_put_and_groups_are_validated(self):
        g=self.fixture(); g['nodes'].append(node('unrelated',x=20000,y=30000))
        patch,_=compact(g)
        self.assertNotIn('unrelated',{o['node'] for o in patch['operations']})
        with self.assertRaises(ValueError):compact(g,[['a'],['a']])

    def test_reproducible(self):
        self.assertEqual(compact(self.fixture()),compact(self.fixture()))

    def test_standard_planner_optimizes_connection_to_outside_scope(self):
        g=self.fixture()
        patch,_=plan(g,[{'entry':'a','nodes':['a'],'title':'A','details':''}])
        after=copy.deepcopy(g)
        positions={o['node']:o for o in patch['operations'] if o['op']=='move_node'}
        for n in after['nodes']:
            if n['id'] in positions:n.update(x=positions[n['id']]['x'],y=positions[n['id']]['y'])
        self.assertNotIn('b',positions)
        self.assertLess(audit(after)['all']['maximum'],audit(g)['all']['maximum'])


if __name__=='__main__':unittest.main()
