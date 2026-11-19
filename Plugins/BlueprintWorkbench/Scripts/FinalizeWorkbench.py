"""Fresh-process verification and automatic cleanup for the standard writeback entry point."""
import json
import os
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from WorkbenchLifecycle import finalize
request=json.loads(Path(os.environ['BLUEPRINT_WORKBENCH_REQUEST']).read_text(encoding='utf-8-sig'))
result=json.loads(Path(request['report']).read_text(encoding='utf-8'))
finalize([{'source':request['asset'],'patch':request['patch'],'result':result}], request['report']+'.finalize.json')
