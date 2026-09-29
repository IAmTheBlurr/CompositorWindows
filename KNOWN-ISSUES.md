# Known issues

The independent Windows preview targets Compositor **1.2.11** at `0ecbacfff8610b566eda059fb2644fddf337fb65` and saves project format 9. Actual Mac runtime equivalence and reciprocal project exchange remain unverified.

| Area | Limitation |
| --- | --- |
| Platform rendering | Qt text/SVG, Windows software blur/glow/effects, LibRaw development and local ONNX selection differ from Apple's frameworks. Numerical/source and Windows workflow checks do not establish identical Mac pixels. Missing fonts retain the saved PNG until text is edited, when a Windows fallback may change its appearance. |
| Photoshop | Import supports 8-bit RGB PSD/PSB. Conversion reports identify unsupported/rasterized structures, advanced fill/blending approximations and oversized-layer cropping. An actual advanced fill-opacity fixture differs in RGB from Photoshop's composite while retaining alpha; that unsupported blend flag is reported. Keep the original Photoshop file. |
| RAW | As-shot camera white balance uses LibRaw's stored multipliers; its starting Kelvin estimate and tone/demosaic output can differ from Apple. Optional lossy-JPEG DNG support is not included. A real Canon EOS 5DIII CR2 and generated lossless DNG were tested. |
| Selection models | Object selection uses click-prompted SlimSAM; Select Subject/Remove Background use the bundled foreground model. Hair, transparent objects and difficult boundaries can require refinement. These are Windows model implementations, not Apple Vision equivalence. |
| Large soft brushes | The earlier 1.0.4-based preview measured 38.6–50.4 ms p95 event-to-DwmFlush latency on 4000 px documents with 800 px soft brushes; worst stroke start/release was 427/522 ms under background load. That benchmark was not repeated for 1.2.11. Current large-document checks verify bounded viewport rendering, not a universal 60 Hz guarantee. |
| Retained historical comparison | `cancellation.adjustment_contracts` still fails against a frozen reference containing the previously repaired Levels partial-alpha defect. Its independent corrected oracle passes. This retained failure is not counted as a pass. The former cursor, M/L and Gaussian expectation discrepancies were reassessed against actual v1.2.11 source and now pass. |
| Sanitizer coverage | 44 distinct current-feature tests passed ASAN, including new parsers, text/clipboard and masks. The full ASAN suite was not repeated. The earlier intermittent full-suite CRT shutdown stall remains unexplained. |
| Signing and updates | This preview is unsigned and updates are manual. Production signing/updating and public release publication belong to Session 2. |
| Hardware/interaction coverage | Installation and native 100%/150% workflows are checked on the development machine. Fresh Windows environments, physical pens, multiple monitors, Narrator, high contrast, IME and wider GPU coverage remain unverified. |

No ordinary-use crash or project corruption is being accepted as an intentional limitation. See [validation](VALIDATION.md) for actual checks. Include the preview version and reproduction steps when [reporting an issue](https://github.com/IAmTheBlurr/CompositorWindows/issues).
