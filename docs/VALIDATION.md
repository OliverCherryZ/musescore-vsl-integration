# Validation scope

This publication was prepared on 2026-11-18 and supplemented with Windows binaries. It separates packaging checks from earlier native application tests and listening acceptance. The release excludes private logs, screenshots, scores with embedded commercial player state, commercial VSL binaries and samples, and private audio recordings.

## Windows binary distribution checks

The packaged host retains the final installed executable SHA-256 `a877222e478aecc517901279e8335501911f0c212125e26dddede69ce009ac9f`. The three companion plugins are the MSVC builds from the published v0.1.0 sources. Their class IDs, x64 PE format, factory loading and release passed; the smoother also passed 17 component/controller/state checks. Static companion-plugin imports require only Windows KERNEL32; bridges dynamically require the user's Synchron Player.

From the relocated application directory, with PATH restricted to its own bin and Windows system directories, version reporting, MusicXML-to-PDF and MuseScore Basic WAV export succeeded. The generated WAV was 48 kHz stereo IEEE float32, 7 seconds, finite and non-silent, with peak below full scale. Native GUI startup, display of the generated two-measure test score, and normal exit were observed. These were tests on the development computer with its existing user profile, not a clean-machine deployment or a subjective listening test.

GUI startup displayed a MuseSounds unavailable notice. Dismissing it allowed normal score work; MuseSounds compatibility is not established by this release. The three newly compiled companion binaries were not newly auditioned with every VSL route. Runtime libraries and fonts are bundled with notices; VSL player, commercial sample libraries and player preset state are supplied by the user.

## Source and companion-plugin publication checks

The complete MuseScore patch contains 46 modified tracked files and 123 added files, including the Zstandard source dependency. Patch reconstruction checks compare all 169 touched files with the maintained source bytes. See `musescore/source-manifest.json` and the build document for the exact base revision and scope.

The restore script was also run against a complete upstream checkout: all 169 source files and 18 support files matched. Seven existing host regression executables passed: baseline mapper (88 assertions), baseline compatibility (88), extended mapper (777), harmonic-tremolo mapper (1,143), loading observation (28 checks), engraving (106 tests), and RPC packer (26 tests). These are executions of the existing built test programs, not a new full host build. Public evidence is in [source-reconstruction.json](../musescore/validation/source-reconstruction.json) and [host-regressions.json](../musescore/validation/host-regressions.json).

All three companion VST3 plugins were rebuilt with MSVC from the published production C++ sources. The eight smoother DSP cases, raw-reference broker regression, and eleven-bus regression passed. The report is retained in [plugin-build-validation.json](evidence/plugin-build-validation.json). No commercial player was loaded and no new native listening acceptance was performed for these rebuilt plugins. In particular, the MSVC smoother is a new build of the source, not a byte-identical copy of the historically deployed Zig binary.

Git's staged patch, support files, and runtime files were checked against their published SHA-256 manifests. Binary build outputs remain excluded from this source release.

## Checks run on the public runtime package

- The published expression map preserves the installed profile's `schemaVersion`, `enabled`, `midiNoteConvention`, `instruments`, `dispatch`, `unsupported`, `resourceIds`, and `notationAliases` values. Only historical development-status and local provenance metadata were removed; a publication note and license notice were added.
- The three selector source files were copied from the installed version. Only SPDX/copyright comments and line-ending normalization were added.
- `node runtime/tests/test-selector.js` passed. It covers all 549 caption/token/snapshot round trips, exact selection where visible names collide, aliases, XML escaping, target-instrument guards, transaction boundaries and rollback mocks.
- Qt 6.10.2 `qmlformat --normalize` parsed the packaged QML successfully. This is a syntax check, not a native UI test.

The checked runtime data contains 549 unique canonical tokens and 404 unique captions. Violin I, Violin II and viola each expose 539 catalog branches; cello adds 10 effects for a total of 549. The expression map also contains named compatibility routes, so its route-entry counts are larger than the selector branch count.

## Earlier native and listening evidence

The final local acceptance summary was created on 2026-11-17 at 22:58. It records technical checks and user playback/WAV listening acceptance for these representative groups:

| Group | Coverage |
| --- | --- |
| A | New short and long variants, plus arco recovery |
| B | Legato, agile/slurred legato and portamento |
| C | Ricochet a2/a3/a4 and saltando |
| D | Recorded crescendo/diminuendo duration variants with ties |
| E | Automatic speed, vibrato-blend branch and spiccato/staccato sequence |
| F | Cello effects, harmonics and composite rules |

Together these groups covered **27 distinct route IDs, including 23 advanced catalog branch IDs**. They did not audition every catalog branch. Groups were tested across successive development binaries; they were not all re-run on one final executable.

Native selector/state skeleton traversal checked **539 Violin I cases plus 549 cello cases = 1,088 cases**, with zero mismatches. Samples were disabled and there were no musical note events. This verifies selection structure, not sound. The entire Violin II and viola catalogs were not independently traversed natively.

Native UI records additionally cover shortcut invocation, insertion on ordinary rests and Undo, wrong-instrument rejection, replacement Undo/Redo, save/reopen with exact preselection, and manual editing to `pizz.` followed by an updated playback plan without changing the other parts.

The final host with the timer/lifetime fix has SHA-256 `a877222e478aecc517901279e8335501911f0c212125e26dddede69ce009ac9f`. Final D and F export/exit checks passed. The timer-fixed D export had a technical comparison with the earlier accepted D audio, but that specific D WAV did **not** receive a separate listening confirmation. F has the later listening and shutdown records. Historical crashes and failed earlier candidates remain historical failures; they are not counted as passes.

The updated Synchron Player core was **1.3.0.3022**. Earlier plugin regression records include accepted realtime playback and native WAV export for Smooth Bridge and the legacy Offline Fix, plus a quartet regression preserving 50/10/10/15 ms smoother settings. New-process export waiting and cancellation were exercised. Load readiness remains a process-activity heuristic, not an official sample-ready API.

## What this does not establish

- No claim of 549 individual listening tests or 1,088 musical audio tests.
- No complete CC20 or other blend-endpoint sweep; group E used a blend branch without new sweep automation.
- No arbitrary tempo adaptation or guaranteed bounce count for recorded ornaments/dynamics.
- No guarantee for every overlapped voice, range, preset, host, operating system, or later player update.
- No claim that this publication recompiled and auditioned every component from scratch; source/build checks are described in the component build documents and release notes.

Before relying on a new build, test a small score with dynamics, articulation changes, save/reopen, realtime playback, direct export from a newly started host, cancellation, and normal shutdown. Compare expected pitches and timings with actual audio. Keep technical checks separate from musical listening judgement.
