# CnCGZH Engine

CnCGZH Engine is a reusable RTS-oriented game-engine project derived from Electronic Arts' public Command & Conquer Generals / Zero Hour source release.

The goal is **not** to keep building one specific C&C game. The repository preserves EA's released source as an upstream reference and builds a generic engine/editor layer around it so new games can be authored as projects.

## What is here now

- `upstream/ea-generals-zero-hour/` — complete snapshot imported from `electronicarts/CnC_Generals_Zero_Hour`.
- `engine/core/` — engine-neutral API plus an executable visual-script graph model.
- `engine/legacy/` — compatibility boundary for progressively wrapping the original Generals systems.
- `studio/` — native Win32 visual editor MVP with a Scratch-like block palette, canvas and inspector.
- `projects/sample-rts/` — example game project and example block graph.
- `assets/sample/` — original neutral placeholder art for development.
- `assets/ea-source-samples/` — resources copied only from files that are actually present in EA's source-code release.
- `tools/extract_sample_assets.py` — reproducible source-resource collector.

## Studio MVP

The current Studio is deliberately small and dependency-free: drag blocks from the left palette onto the canvas, reposition them, inspect them, delete them and save a block layout. The engine-side block graph already exposes generic commands for spawning, movement, health, resources and UI messages.

The next milestones are JSON load/save parity with `projects/sample-rts`, property editing, map/entity inspectors, an asset browser and Play-in-Editor through the legacy adapter.

## Important asset distinction

EA's source repository is a **source-code release**, not a redistribution of the installed game's commercial art/data. This repository therefore does not pretend to contain all Generals/Zero Hour textures, models, audio or maps. The extractor only collects resources already committed to EA's public source release; the neutral sample assets are original placeholders for engine development.

## Build scope

The top-level CMake project builds the new engine core and native Studio only. The preserved upstream code is intentionally not part of that build yet because EA's release depends on old or missing SDKs/libraries and requires significant modernization. That work is isolated behind adapters rather than contaminating new projects with legacy dependencies.

## License

This project is based on EA's GPLv3 source release and retains its GPLv3 licensing requirements plus EA's additional terms. The original license is preserved at `upstream/ea-generals-zero-hour/LICENSE.md`. This repository is an independent derivative project and is not endorsed by Electronic Arts.
