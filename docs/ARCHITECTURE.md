# CnCGZH Engine architecture

## Goal

Turn the released Generals/Zero Hour codebase into a reusable RTS-oriented game engine with a beginner-facing visual scripting workflow. The preserved EA source is an implementation backend, not the product identity.

## Layers

- `upstream/ea-generals-zero-hour/` — untouched source-release snapshot.
- `engine/core/` — engine-neutral API and visual-script graph runtime.
- `engine/legacy/` — adapters that translate generic engine calls into legacy Generals systems.
- `studio/` — visual authoring application. Blocks are data; the UI must never generate C++.
- `projects/` — user games. A project owns maps, assets, archetypes and block graphs.
- `assets/sample/` — new neutral sample content.
- `assets/ea-source-samples/` — only resources already present in EA's source release, copied by the extractor with provenance.

## Migration sequence

1. Preserve upstream exactly.
2. Build a clean API over entities, world, terrain, camera, input, audio and UI.
3. Move game rules out of hard-coded Generals globals into project data.
4. Replace unavailable/proprietary SDK integrations.
5. Make Studio load/save projects and visual block graphs.
6. Add map/entity inspectors, asset browser and play-in-editor.
7. Make the legacy backend optional once equivalent modern subsystems exist.

## Visual scripting contract

A block graph is serialized as project data. Blocks call only `EngineAPI`. This keeps projects portable if the rendering/audio/network backend changes later.

Initial block families: Events, Entities, Movement, Combat, Economy, Flow and UI. Later families should expose AI, teams, diplomacy, triggers, camera, terrain queries, audio and multiplayer-safe commands.

## Licensing

The engine is a modified work based on EA's GPLv3 source release and must retain the GPLv3 license plus EA's additional terms. Modified files must be identified as modified. The repository must not imply that EA endorses this derivative.
