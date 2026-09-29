# Windows preview 1.2.11

This independent Windows preview targets the editing and project functionality of Robbie Tilton's Compositor v1.2.11 (`0ecbacfff8610b566eda059fb2644fddf337fb65`). The original Windows baseline was 1.0.4 (`a19db9011282399785dc18efcfded904627bdcc2`). Windows build revision 1 retains the existing MSI upgrade identifiers.

The update adds 24 blend modes, folder opacity, six editable layer effects, the full target adjustment set, editable paragraph text, line shapes, RAW development and Camera Raw, supported layered PSD/PSB and SVG import, object/subject selection, feathering, brush smoothing, growing mask edits, rulers/guides/grid/snapping, Trim, keyboard remapping and numeric scrubbing. Layer copy/paste, folder duplication and project transfers preserve supported editing metadata. Resize handles can cross their opposite edges, and distort supports folded quadrilaterals.

Projects save format 9, including guides, editable text/effects and blur/noise adjustments, while earlier supported versions remain readable. Saved image assets own their decoded memory. Captured history revisions prepare the save interface for subsequent background saving without making later edits appear clean.

Windows uses Qt text/SVG rendering, LibRaw development and local ONNX selection models. Those implementations and Windows software blur/glow kernels have not been established as pixel-identical to Mac frameworks. Photoshop supports a documented 8-bit RGB subset and reports conversions. See the [user guide](user-guide.md), [known issues](../KNOWN-ISSUES.md) and [validation](../VALIDATION.md) for actual coverage and limitations.

This is a local independent preview. Signing, production update integration, public release publication, and the complete upstream v1.2.11→v1.3.5 feature range remain for Session 2. Updates are manual. Native Windows title bars remain the default, with the saved optional Mac-style appearance.
