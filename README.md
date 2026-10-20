# CnCGZH Engine / Studio v2

This branch restarts the editor from the clean EA Generals / Zero Hour source baseline.

## Goal

CnCGZH Studio is a visual front end over the engine source: scene construction, object inspection, cameras and per-object visual scripting are available without requiring the editor user to write C++.

## Current Studio v2 foundation

- Dockable Dear ImGui interface.
- Direct3D 11 3D viewport.
- Scene hierarchy and object inspector.
- Move / rotate / scale gizmo using ImGuizmo.
- Visual camera entities with editable FOV, clipping planes and viewport frustums.
- Resource browser that scans the imported EA source/content tree and supports drag-and-drop into the viewport.
- Per-object visual scripting graph with an initial set of event, flow, object, camera, math and value nodes.
- Engine API browser that inventories function declarations from the imported Generals / Zero Hour headers so engine subsystems can be exposed through visual adapters instead of requiring users to edit C++.

The API browser deliberately distinguishes a source symbol from an executable visual adapter. Discovering a C++ declaration does not make it safely callable; executable adapters are registered as typed nodes.

## Build on Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target CnCGZHStudio
.\build\Release\CnCGZHStudio.exe
```

The original imported EA source remains under `upstream/ea-generals-zero-hour` and is not modified by the editor.
