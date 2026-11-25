# Blueprint Workbench – Blueprint Assist bridge

Optional editor plugin, requiring a separately installed and licensed Blueprint Assist. No Blueprint Assist code or binaries are included. Keep the normal BlueprintWorkbench plugin enabled. This adapter is `EditorNoCommandlet` and does not affect the original commandlet APIs.

## Default execution route

Use this bridge in the background for Blueprint wire/node formatting. Do not use Computer Use, simulated F-key presses, or mouse dragging unless the user explicitly requests a different execution method. Single functions use `Invoke-Assist.ps1`; explicit batches use `RunBatchAssist.py` and independent `FinalizeBatchAssist.py`. Launch batch editors hidden (`Start-Process -WindowStyle Hidden`) with `-RenderOffscreen`; formatting requires full-editor Slate, not a commandlet or NullRHI. Review generated graph PNGs directly. A missing/failed bridge calls for dependency/build diagnosis, not an automatic UI-automation or alternate-layout fallback.

## Scope

The single-function runner supports a named K2 **function graph with one entry**, including Widget Blueprint function graphs. The explicit batch runner also supports event roots, pure functions, macros/collapsed K2 graphs, Animation Blueprint K2 event graphs, and embedded level Blueprints. It does not format animation state machines or animation pose graphs. It opens that exact graph, waits for Blueprint Assist's real Slate node-size cache, then invokes `FBAGraphHandler::FormatNodes` on its entry. It does not call the Workbench layout algorithm, duplicate gameplay nodes, or refactor logic. Blueprint Assist may add/remove reroute nodes and reposition/resize comments.

The Python runner proves equivalent terminal pin connections through transparent reroutes, checks original node classes/types/defaults/metadata, and checks other graphs' positions. Any mismatch or compiler error prevents saving. The bridge functions themselves never save. The runner temporarily disables automatic formatting/autosave in memory and restores preferences before exiting.

## Installation and use

Install this folder as a separate project/engine plugin, enable `BlueprintWorkbenchAssist`, and build for the host engine/Blueprint Assist version. Do not copy another engine's binary or replace a project's custom BlueprintWorkbench plugin with a generic DLL.

Run `Scripts/Invoke-Assist.ps1` with explicit `ProjectFile`, `EngineRoot`, `AssetPath`, `GraphPath`, and a **new** `OutputDirectory`. Default is an isolated full-editor preview, launched hidden with Slate rendering offscreen. Existing UE editors must first be saved and closed. Do not use commandlet or NullRHI mode for formatting.

After reviewing the preview JSON and native graph PNGs, rerun with a new output directory, `-WriteBack -PreviewDirectory <successful preview directory>`. It checks the source still matches the preview baseline, makes a backup, verifies formatting/compilation, saves only the target Blueprint, closes the owned editor, and performs independent commandlet reload verification. Evidence includes `before.json`, `after.json`, native `before.png`/`after.png`, `result.json`, backup (writeback only), and `reload.json`.

No forced termination is used on timeout. A failure leaves evidence and stops the pipeline; if saving happened but reload fails, report it as an incomplete verification, never as success. Never reuse an old preview after the source changes. The single-function entry point does not discover or batch assets automatically.

## Verification

`python -m unittest TestAssistVerification` in Scripts tests transparent reroute acceptance and rejection of broken edges, default changes, other-graph moves, and added gameplay nodes. Host build, native editor execution, saved reload and visual acceptance are separate checks. Graph quality and engine/version evidence are recorded in the repository handoff.

Known limits: formatting starts at the function entry; disconnected legacy chains may retain their layout. Blueprint Assist does not guarantee fewer crossings or shorter maximum wires on every graph. Reload can expose engine-normalized metadata (for example an unused default on a connected enum input); the strict verifier intentionally stops on such differences. Inspect the exact difference and backup before proceeding; do not disable semantic checks or treat a failed reload as success.

## Explicit batch requests

`Scripts/RunBatchAssist.py` is a full-editor Python entry point. Set `WORKBENCH_BATCH_REQUEST` to a JSON file containing `output` (new evidence directory), `writeback` (explicit authorization), and `items`. Each item contains an `id`, exact `asset` path, `graphs` (`path`, `roots` node GUID array), and optional `cleanup` (`graph`, `nodes` GUID array). Embedded level Blueprints additionally specify their containing `map` package. Discovery and approval of the exact asset/node list belong to the caller; this script does not sweep or delete assets by itself.

For each item, it inspects the original, compiles, removes only specified verified-unused nodes, formats every requested root through BA, verifies the whole asset and compiles again. A successful in-memory preview precedes any writeback. Writeback first backs up the original package, then saves only that asset/map. An error stops the owned editor; failed previews are not saved. Earlier successful items remain saved and are reported individually. Run only after existing interactive editors have been saved and closed; do not load the same packages in another audit process during writeback.

`FormatGraphRoot` uses the same BA `FormatNodes` operation used by the F-key formatting path. `OpenGraph` temporarily requests the full Blueprint editor for data-only assets without persisting that transient preference. `RemoveUnusedNodes` rejects event/function entries, unknown nodes, latent/unresolved calls and live execution/data boundaries. Protected entries are never inferred unused from a lack of wires.

BA/Slate can initialize pin default strings while creating widgets or reconnecting reroutes, including unused Key output defaults. `RestoreFormattingDefaults` restores the inspected original `DefaultValue` string on surviving pins before verification, so a formatting-side value edit is not retained. It does not alter connections, pin types, default objects or localized text. Each restoration is logged; every surviving value and terminal connection must then match the baseline. Batch comparison records and normalizes unused connected enum inputs, and finite numeric fallback strings / legacy float-literal serialization flags on connected numeric call inputs with a real upstream source. Numeric categories, precision, links, all disconnected defaults and output precision flags stay strict. Distinct physical A–Z / 0–9 key bindings may be compared independent of array order because UE dispatches them by global input EventIndex; duplicate keys and AnyKey remain order-sensitive. The original single-function verifier remains unchanged.

After the batch editor exits, launch a fresh full editor with `Scripts/FinalizeBatchAssist.py` and `WORKBENCH_BATCH_VERIFY` pointing to `{ "directories": ["asset evidence directory"], "output": "reload-summary.json" }`. It independently reloads saved assets, checks logic and exact saved geometry, and compiles (including Animation Blueprints). Treat any failed reload as incomplete verification. No PIE, cook, device or play-feel claim follows from these checks.

Batch tests: `python -m unittest TestBatchPlanning TestBatchVerification TestAssistVerification`. Native build and real editor integration are documented separately in the handoff.

Blueprint interfaces contain declarations, not editable implementations. Batch inspection records them as skipped without saving; their implementations in ordinary Blueprints are formatted normally.

Source basis for batch-only normalization (UE 5.7.4): `FNodeHandlingFunctor::RegisterNets` resolves connected inputs to their upstream net and registers literals only for unconnected inputs (`KismetCompilerMisc.cpp`); `KismetCompilerVMBackend.cpp` uses `bSerializeAsSinglePrecisionFloat` only while emitting a literal; `EdGraphSchema_K2.cpp` clears that legacy flag on default initialization. `PlayerInput.cpp` assigns a global incrementing EventCount and sorts non-axis delegates by EventIndex (then FoundIndex for ties). Do not extend normalization to changed numeric types, unconnected literals, output flags, duplicate physical keys, or arbitrary generated binding names.

`BatchPlanning.graph_roots` discovers connected function/event roots without treating an input node's purity hint as an exclusion. It also covers pure return/tunnel graphs and timeline chains that are not execution-reachable from another root. Run coverage discovery against the baseline after explicitly authorized deletions, so an entry whose only old data consumers were removed is correctly treated as an empty retained entry.

The conservative deletion whitelist includes unused CreateDelegate, SpawnActorFromClass and CreateWidget chains, but still rejects live consumers or incoming execution/delegate boundaries. Component-template construction, macros, timelines, latent calls and unknown special nodes are not inferred dead merely from their appearance. Callers must check the owned process exit code as well as JSON results; a shutdown failure can occur after a report was written and needs a clean independent verification.
