"""Boundary tests for cleanup selection and verification gates; no Unreal process required."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest

class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.assets={'/Game/Original','/Game/Workbench/Review','/Game/Workbench/Unmanaged'}
        self.deleted=[]
        self.refs={}
        self.bp={'asset':'/Game/Original.Original','graphs':[]}
        self.compile_success=True
        def delete(asset):
            self.deleted.append(asset)
            self.assets.remove(asset)
            return True
        fake=types.SimpleNamespace(
            Paths=types.SimpleNamespace(project_dir=lambda:str(self.root)),
            BlueprintWorkbenchLibrary=types.SimpleNamespace(
                inspect_blueprint=lambda _:json.dumps({'success':True,'blueprint':self.bp}),
                apply_patch=lambda *_:json.dumps({'success':self.compile_success,'compile_errors':0 if self.compile_success else 1})),
            AssetRegistryHelpers=types.SimpleNamespace(get_asset_registry=lambda:types.SimpleNamespace(search_all_assets=lambda _:None)),
            EditorAssetLibrary=types.SimpleNamespace(does_asset_exist=lambda a:a in self.assets,find_package_referencers_for_asset=lambda a,_:self.refs.get(a,[]),delete_asset=delete))
        self.previous=sys.modules.get('unreal')
        sys.modules['unreal']=fake
        spec=importlib.util.spec_from_file_location('cleanup_under_test',Path(__file__).with_name('WorkbenchLifecycle.py'))
        self.module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.backup=self.root/'original-backup.uasset'
        self.backup.write_text('backup',encoding='utf-8')
        self.patch={'operations':[]}
        self.result={'success':True,'saved':True,'compiled':True,'compile_errors':0,'backup_file':str(self.backup),'blueprint':copy.deepcopy(self.bp)}
        self.writes=[{'source':'/Game/Original','patch':self.patch,'result':self.result}]
        self.report=self.root/'report.json'
        self.register('/Game/Workbench/Review')
    def register(self,asset,patch=None):
        self.assets.add(asset)
        return self.module.register_copy('/Game/Original',self.patch if patch is None else patch,{'success':True,'saved':True,'blueprint':{'asset':asset+'.'+asset.rsplit('/',1)[1]}})
    def tearDown(self):
        if self.previous is None: sys.modules.pop('unreal',None)
        else: sys.modules['unreal']=self.previous
        self.temp.cleanup()
    def test_only_matching_registered_copies_deleted(self):
        self.register('/Game/Workbench/OtherTask',{'operations':[{'op':'different'}]})
        result=self.module.finalize(self.writes,self.report)
        self.assertEqual(result['deleted'],['/Game/Workbench/Review'])
        self.assertIn('/Game/Workbench/Unmanaged',self.assets)
        self.assertIn('/Game/Workbench/OtherTask',self.assets)
        self.assertIn('/Game/Original',self.assets)
        self.assertTrue(self.backup.is_file() and self.report.is_file())
    def test_original_graph_change_blocks_deletion(self):
        self.bp['graphs']=[{'name':'changed','path':'changed','nodes':[]}]
        with self.assertRaises(ValueError): self.module.finalize(self.writes,self.report)
        self.assertFalse(self.deleted)
    def test_failed_write_blocks_deletion(self):
        self.result['saved']=False
        with self.assertRaises(ValueError): self.module.finalize(self.writes,self.report)
        self.assertFalse(self.deleted)
    def test_failed_recompile_blocks_deletion(self):
        self.compile_success=False
        with self.assertRaises(ValueError): self.module.finalize(self.writes,self.report)
        self.assertFalse(self.deleted)
    def test_missing_backup_blocks_deletion(self):
        self.backup.unlink()
        with self.assertRaises(ValueError): self.module.finalize(self.writes,self.report)
        self.assertFalse(self.deleted)
    def test_referenced_copy_retained_with_reason(self):
        self.refs['/Game/Workbench/Review']=['/Game/Player']
        result=self.module.finalize(self.writes,self.report)
        self.assertFalse(result['cleanup_complete'])
        self.assertEqual(result['retained'][0]['referencers'],['/Game/Player'])
        self.assertFalse(self.deleted)
    def test_absent_copy_and_repeat_are_safe(self):
        self.assets.remove('/Game/Workbench/Review')
        first=self.module.finalize(self.writes,self.report)
        second=self.module.finalize(self.writes,self.root/'again.json')
        self.assertEqual(first['already_absent'],['/Game/Workbench/Review'])
        self.assertFalse(second['deleted'])
    def test_source_or_traversal_cannot_be_registered(self):
        for asset in ['/Game/Original','/Game/Workbench/../Original']:
            with self.assertRaises(ValueError): self.register(asset)
    def test_pin_reordering_is_not_a_graph_change(self):
        pins=[{'id':'a','name':'self','direction':'input','links':[]}, {'id':'b','name':'Output_Get','direction':'output','links':[]}]
        self.bp['graphs']=[{'path':'EventGraph','nodes':[{'id':'node','pins':pins}]}]
        self.result['blueprint']=copy.deepcopy(self.bp)
        self.bp['graphs'][0]['nodes'][0]['pins'].reverse()
        self.assertTrue(self.module.finalize(self.writes,self.report)['success'])

if __name__=='__main__': unittest.main()
