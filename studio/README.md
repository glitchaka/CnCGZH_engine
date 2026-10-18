# CnCGZH Studio

The Studio is the beginner-facing authoring layer for CnCGZH Engine. It is intentionally separate from the Generals game UI.

Current MVP:

- native Win32 desktop application;
- block palette on the left;
- free canvas in the center;
- inspector on the right;
- drag blocks from the palette and reposition them;
- visible execution links;
- delete selected block with `Del`;
- save the current block layout with `Ctrl+S` or the toolbar;
- engine-neutral block schema in `studio/schema/blocks.json`.

The next integration step is to load/save the JSON graph format used by `projects/sample-rts`, then bind Play to `BlockGraph` through a backend adapter.
