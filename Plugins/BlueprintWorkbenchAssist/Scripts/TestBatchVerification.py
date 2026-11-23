import copy,unittest
from TestAssistVerification import fixture
from BatchVerification import verify_batch
class BatchTests(unittest.TestCase):
    def test_selected_movement(self):
        b=fixture();a=copy.deepcopy(b);a['graphs'][0]['nodes'][0]['x']=20
        verify_batch(b,a,['Function'])
        with self.assertRaises(AssertionError):verify_batch(b,a,[])
    def test_link_and_defaults_still_checked(self):
        b=fixture();a=copy.deepcopy(b);a['graphs'][0]['nodes'][0]['pins'][0]['default']='changed'
        with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
    def test_only_connected_enum_unused_default_normalized(self):
        b=fixture();n=b['graphs'][0]['nodes'][1];n['class']='/Script/BlueprintGraph.K2Node_GetEnumeratorNameAsString'
        p=n['pins'][0];p.update(name='Enumerator',type={'category':'byte','object':'/Script/Test.Enum'})
        a=copy.deepcopy(b);a['graphs'][0]['nodes'][1]['pins'][0]['default']='None'
        verify_batch(b,a,['Function'])
        a['graphs'][0]['nodes'][1]['pins'][0]['links']=[]
        with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
        b['graphs'][0]['nodes'][1]['pins'][0]['links']=[]
        with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
    def test_connected_legacy_float_flag_only(self):
        b=fixture();b['graphs'][0]['nodes'][1]['class']='/Script/BlueprintGraph.K2Node_CallFunction';p=b['graphs'][0]['nodes'][1]['pins'][0]
        p['type']={'category':'real','subcategory':'double','container':0,'reference':False,'unreal_text':'(PinCategory="real",PinSubCategory="double",bSerializeAsSinglePrecisionFloat=True)'}
        a=copy.deepcopy(b);ap=a['graphs'][0]['nodes'][1]['pins'][0]
        ap['type']['unreal_text']=ap['type']['unreal_text'].replace(',bSerializeAsSinglePrecisionFloat=True','')
        r=verify_batch(b,a,['Function']);self.assertEqual(len(r['legacy_connected_float_flag_changes']),1)
        p['default']='0.0';ap['default']='1.000000'
        self.assertEqual(len(verify_batch(b,a,['Function'])['unused_connected_numeric_default_changes']),1)
        ap['type']['subcategory']='float'
        with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
        ap['type']['subcategory']='double';ap['default']='changed'
        with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
    def test_literal_and_output_precision_flags_remain_strict(self):
        for index in (0,1):
            b=fixture();b['graphs'][0]['nodes'][index]['class']='/Script/BlueprintGraph.K2Node_CallFunction';p=b['graphs'][0]['nodes'][index]['pins'][0]
            p['type']={'category':'real','subcategory':'double','container':0,'reference':False,'unreal_text':'(PinCategory="real",bSerializeAsSinglePrecisionFloat=True)'}
            if index==1:p['links']=[]
            a=copy.deepcopy(b);a['graphs'][0]['nodes'][index]['pins'][0]['type']['unreal_text']='(PinCategory="real")'
            with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
    def test_only_distinct_physical_key_order_is_normalized(self):
        b=fixture();b['input_key_bindings']=[{'key':'I','event':0,'function':'IHandler'},{'key':'O','event':0,'function':'OHandler'}]
        a=copy.deepcopy(b);a['input_key_bindings'].reverse()
        self.assertTrue(verify_batch(b,a,['Function'])['distinct_key_binding_order_changed'])
        a['input_key_bindings'][0]['key']='Q'
        with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
        for key in ('I','AnyKey'):
            b['input_key_bindings'][1]['key']=key;a=copy.deepcopy(b);a['input_key_bindings'].reverse()
            with self.assertRaises(AssertionError):verify_batch(b,a,['Function'])
if __name__=='__main__':unittest.main()
