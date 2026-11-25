# Blueprint Workbench

- Standalone source plugin. Shared entry points and ownership: Docs/ARCHITECTURE.md. Current status: docs/CODEX_HANDOFF.md.
- Preserve preview, explicit writeback, backup and independent reload contracts. No project asset paths or game-module dependencies in core.
- Record engine versions separately: build, load, integration and unverified. Never downgrade assets between hosts.
- Source delivery and engine-build-specific Win64 packages are supported. Validate precompiled packages in a fresh Blueprint-only host; never claim cross-engine binary compatibility. No commits, pushes or public release without authorization.

- Blueprint wire/node formatting uses the background BlueprintWorkbenchAssist bridge by default, not Computer Use. See README.md; missing dependencies are a setup issue, not permission to fall back to UI automation.
