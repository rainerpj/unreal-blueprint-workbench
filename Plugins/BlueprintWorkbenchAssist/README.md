# Blueprint Workbench – Blueprint Assist bridge

Optional editor plugin, requiring a separately installed and licensed Blueprint Assist. No Blueprint Assist code or binaries are included. Keep the normal BlueprintWorkbench plugin enabled. This adapter is `EditorNoCommandlet` and does not affect the original commandlet APIs.

## Scope

Version 0.1 supports a named K2 **function graph with one entry**, including Widget Blueprint function graphs. It opens that exact graph, waits for Blueprint Assist's real Slate node-size cache, then invokes `FBAGraphHandler::FormatNodes` on its entry. It does not call the Workbench layout algorithm, duplicate gameplay nodes, or refactor logic. Blueprint Assist may add/remove reroute nodes and reposition/resize comments.

The Python runner proves equivalent terminal pin connections through transparent reroutes, checks original node classes/types/defaults/metadata, and checks other graphs' positions. Any mismatch or compiler error prevents saving. The bridge functions themselves never save. The runner temporarily disables automatic formatting/autosave in memory and restores preferences before exiting.

## Installation and use

Install this folder as a separate project/engine plugin, enable `BlueprintWorkbenchAssist`, and build for the host engine/Blueprint Assist version. Do not copy another engine's binary or replace a project's custom BlueprintWorkbench plugin with a generic DLL.

Run `Scripts/Invoke-Assist.ps1` with explicit `ProjectFile`, `EngineRoot`, `AssetPath`, `GraphPath`, and a **new** `OutputDirectory`. Default is an isolated full-editor preview, launched hidden with Slate rendering offscreen. Existing UE editors must first be saved and closed. Do not use commandlet or NullRHI mode for formatting.

After reviewing the preview JSON and native graph PNGs, rerun with a new output directory, `-WriteBack -PreviewDirectory <successful preview directory>`. It checks the source still matches the preview baseline, makes a backup, verifies formatting/compilation, saves only the target Blueprint, closes the owned editor, and performs independent commandlet reload verification. Evidence includes `before.json`, `after.json`, native `before.png`/`after.png`, `result.json`, backup (writeback only), and `reload.json`.

No forced termination is used on timeout. A failure leaves evidence and stops the pipeline; if saving happened but reload fails, report it as an incomplete verification, never as success. Never reuse an old preview after the source changes. No automatic asset-wide or project-wide batch formatting is performed.

## Verification

`python -m unittest TestAssistVerification` in Scripts tests transparent reroute acceptance and rejection of broken edges, default changes, other-graph moves, and added gameplay nodes. Host build, native editor execution, saved reload and visual acceptance are separate checks. Graph quality and engine/version evidence are recorded in the repository handoff.

Known limits: formatting starts at the function entry; disconnected legacy chains may retain their layout. Blueprint Assist does not guarantee fewer crossings or shorter maximum wires on every graph. Reload can expose engine-normalized metadata (for example an unused default on a connected enum input); the strict verifier intentionally stops on such differences. Inspect the exact difference and backup before proceeding; do not disable semantic checks or treat a failed reload as success.
