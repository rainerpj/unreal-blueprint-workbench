# BlueprintWorkbenchAssist

- Default to the hidden background bridge for Blueprint wire/node formatting. Do not use Computer Use, simulated keys, or mouse dragging.
- Read README.md. Use Invoke-Assist.ps1 for single-function preview/writeback, RunBatchAssist.py and FinalizeBatchAssist.py for explicit batches.
- Formatting requires full-editor Slate with RenderOffscreen, not commandlet/NullRHI. Preserve semantic verification, backups and independent reload.
- Third-party Blueprint Assist is separately installed and licensed; do not redistribute its files. Check exact engine BuildId and tested dependency version before loading a precompiled bridge.
