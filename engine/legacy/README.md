# Legacy adapter

This directory is the compatibility boundary with the preserved EA code under `upstream/ea-generals-zero-hour`.

Rules:

1. Generic project files and block graphs must not include Generals/Zero Hour headers.
2. New gameplay concepts go through `EngineAPI`, not directly into legacy globals.
3. Legacy rendering, terrain, object simulation, pathfinding, audio and UI are wrapped incrementally.
4. Proprietary/missing SDK integrations (Miles, Bink, GameSpy, SafeDisk, old DirectX SDK pieces) are replacement targets, not dependencies of the editor.
5. The upstream tree remains untouched so diffs against EA's release stay possible.
