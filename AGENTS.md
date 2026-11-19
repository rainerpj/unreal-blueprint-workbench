# Unreal Blueprint Workbench

- This original GitHub repository is the authoritative maintenance location: rainerpj/unreal-blueprint-workbench. Do not create a replacement repository for updates.
- Current repository status: docs/CODEX_HANDOFF.md. Plugin architecture and shared entry points: Plugins/BlueprintWorkbench/Docs/ARCHITECTURE.md.
- Keep the Plugins/BlueprintWorkbench installation path. Tools contains forwarding PowerShell entry points only; implementation and tests live inside plugin Scripts.
- Preserve preview, explicit writeback, backup and independent reload contracts. No game-specific classes or assets in the common plugin.
- Report each engine build, load, integration, CLI, PIE and device result separately. Never downgrade assets between hosts.
