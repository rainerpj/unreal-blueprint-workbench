import unittest
from AssistSettings import override_setting

class Settings:
    def __init__(self, values): self.values = values
    def get_editor_property(self, key):
        if key not in self.values: raise Exception("Failed to find property '"+key+"'")
        return self.values[key]
    def set_editor_property(self, key, value): self.values[key] = value

class TestSettings(unittest.TestCase):
    def test_supported_setting_is_restorable(self):
        obj=Settings({'bFormatAllAfterSaving':True});saved=[]
        override_setting(saved,obj,'bFormatAllAfterSaving',False)
        self.assertFalse(obj.values['bFormatAllAfterSaving'])
        for target,key,value in saved:target.set_editor_property(key,value)
        self.assertTrue(obj.values['bFormatAllAfterSaving'])
    def test_old_version_missing_optional_setting(self):
        saved=[];override_setting(saved,Settings({}),'bFormatAllAfterSaving',False)
        self.assertEqual(saved,[])
    def test_required_setting_missing_fails(self):
        with self.assertRaises(Exception):override_setting([],Settings({}),'bAlwaysFormatAll',False)
    def test_unexpected_failure_is_not_hidden(self):
        class Broken(Settings):
            def get_editor_property(self,key):raise RuntimeError('invalid object')
        with self.assertRaises(RuntimeError):override_setting([],Broken({}),'bFormatAllAfterSaving',False)
