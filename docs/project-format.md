# Compositor project format, versions 1–9

The Windows implementation is [ProjectStore.cpp](../src/persistence/ProjectStore.cpp). Its source contract is upstream v1.2.11 at `0ecbacfff8610b566eda059fb2644fddf337fb65`. Windows creates version **9** and reads versions **1–9**. Actual Mac runtime exchange remains unverified.

A `.comp` project is a directory containing `manifest.json` and owned PNG assets under `images/`. Copy the whole directory. Saved files retain sRGB source pixels; layer transforms are separate. Opening restores the active layer and guides, starts clean history, fits the viewport, and leaves the document selection empty.

The manifest identifies `com.compositor.project`, the integer version, `sRGB`, the document UUID, width/height, optional resolution (default 72 ppi), optional active layer UUID, and layers in bottom-to-top sibling order. Geometry uses two-number arrays for `origin` and `size`, clockwise rotation about the center, flips, and `Nearest`, `Smooth` or `High quality` sampling. Asset filenames match the normalized uppercase UUID: `<UUID>.png` and `<UUID>.mask.png`.

| Version | Added semantics |
| --- | --- |
| 1 | Pixel/blank layers and transforms |
| 2 | Nested folders through `parentID` and `isGroup` |
| 3 | Opacity and blend modes on layers |
| 4 | Layer masks, with enabled state |
| 5 | Clipping/live-alpha links through `maskSourceID` |
| 6 | Folder masks, multiplied into each descendant |
| 7 | Adjustment layers |
| 8 | Folder opacity and saved document guides |
| 9 | Gaussian Blur, Motion Blur and Add Noise adjustment layers |

Folders remain pass-through, with Normal blend mode. Folder opacity and enabled masks multiply the coverage of each contained layer. The 24 blend strings follow upstream's darkening, lightening, contrast, comparison and component groups; see `blendNames` and `blendMenuModes` in [Document.h](../src/core/Document.h).

Masks use 8-bit gray PNGs. Linked masks follow layer transforms; unlinked masks retain `maskPlacement`. Clipping sources contribute alpha, transform, opacity and masks regardless of visibility. Missing links, cycles, adjustment/folder sources and excessive nesting are rejected.

Pixel layers may also carry these additive fields:

- `text`: uniform content, PostScript font name, size, RGB color, Left/Center/Right alignment, tracking, leading and optional `boxSize`. Content is limited to 100,000 UTF-16 units. The PNG is authoritative until edited, preserving appearance if a font is missing. Ordinary transforms, masks and duplication retain text; operations that replace its source pixels rasterize it. Format 10 color runs and format 11 font runs are rejected.
- `shape`: Rectangle, Ellipse or Line, RGB color, corner radius and optional line width/start/end. Line endpoints use fractions of the layer box. The saved PNG remains a fallback.
- `effects`: independent stroke, shadow, colorOverlay, innerShadow, outerGlow and innerGlow records. Each retains optional `enabled` state (absent means enabled), its color/opacity and relevant size, blur, distance, angle or inside setting.

Adjustment objects retain every parameter family, including inactive settings. Kind strings are Hue/Saturation, Levels, Curves, Exposure, Gradient Map, Grain, Invert, Black & White, Color Balance, Gaussian Blur, Motion Blur and Add Noise. Blur/motion/noise require version 9. Optional later parameter families use upstream defaults; required legacy hue/levels/curves fields still must be present. Finite ranges, Boolean types, enum values, curve points and UInt32 seeds are validated. Enum-keyed HSV dictionaries use alternating key/value arrays.

Guides have unique UUIDs, horizontal/vertical axes and finite positions within ±1,000,000 document pixels; at most 1,000 are stored. Canvas resize/crop offsets them, Image Size scales them, and canvas flips mirror them. Ruler/grid visibility and snapping preferences are application settings rather than project content.

Limits are 30,000 pixels per side,200 million pixels per individual working surface,10,000 layers, 4 MiB manifest and 512 MiB per encoded asset. Aggregate raster budgets scale with physical memory, up to 800 million pixels, separately accounting for image and mask assets. Working operations may impose smaller temporary-memory limits. See [DocumentLimits.h](../src/core/DocumentLimits.h).

Saving stages and validates an owned package, then replaces the destination with a recoverable sibling journal and backup. Fault recovery preserves the valid committed package. Readers decode independent pixel storage; later replacement of on-disk PNGs cannot change history snapshots. Failed load or save must leave the editor document intact. Export creates a flattened derivative and does not mark the project saved.
