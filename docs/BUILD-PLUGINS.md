# Build the companion VST3 plugins

The three companion plugins currently target **Windows x64**. Their production C++ sources are copied without changes from the tested local integration. They require a separately installed and licensed Vienna Synchron Player and the relevant licensed sample libraries for music playback. No Vienna player, sample, activation data, or preset-state blob is distributed here.

| Plugin | Purpose |
| --- | --- |
| Synchron Smooth Bridge | Forwards the player interfaces, makes the per-block processing mode follow the successful setup mode, and publishes an audio reference for the companion effect. |
| VSL Host Dynamics Smoother | First-insert effect that smooths measured host gain against the bridge reference while preserving single-note dynamics. |
| Synchron Offline Bridge Fix | Legacy compatibility bridge for scores using the earlier plugin class. |

## Requirements

- Windows x64 and Python 3.10 or newer.
- Visual Studio 2022 C++ Build Tools with an x64 MSVC toolset and Windows SDK. The original updated bridges used MSVC 19.44, `/MT`, `/O2`, and C++17.
- Steinberg VST3 **pluginterfaces 3.7.12, tag `v3.7.12_build_20`**, in a directory containing `pluginterfaces/base/funknown.cpp`. Get the pinned source from the [official repository](https://github.com/steinbergmedia/vst3_pluginterfaces/tree/v3.7.12_build_20). The GPLv3 option of its accompanying license is used for this open-source project. The small `deps/vstinitiids.cpp` source retains its embedded BSD license.

Example dependency layout:

```text
vst3-sdk/
  pluginterfaces/
    base/funknown.cpp
    base/coreiids.cpp
    vst/...
```

The SDK is not bundled. Use the stated version when reproducing this integration; newer SDKs have not been validated by this project. `deps/VST3-LICENSE.txt` preserves the notice from the tested SDK version.

## Compile and test

Open **x64 Native Tools Command Prompt for VS 2022**, then run from the repository root:

```powershell
python plugins/build.py --sdk C:\dev\vst3-sdk
```

Alternatively set `VST3_SDK_ROOT`, or pass `--output-dir C:\dev\vsl-plugin-build` to choose another build directory. `--cl` selects a compiler executable; its MSVC include and library environment must already be configured. `--plan-only` prints the commands, and `--skip-tests` builds only the plugins.

The default output is `plugins/build/msvc/`. The build script compiles all three plugins and runs the existing sample-free C++ checks:

- Raw reference broker: source identity, epochs, segment accounting, buffer consumption, and timing continuity.
- Multiple output buses: reference summing and host-gain estimation against eleven stereo buses.
- Dynamics smoother: steady-state identity, raised-cosine transition duration and endpoints, zero gain, variable blocks, invalid-reference rejection, and silence.

The report and compile/test logs stay in the ignored build directory. These checks do not load Vienna Synchron Player, register plugins, modify an installed player, or perform native MuseScore playback. An MSVC rebuild of the smoother differs from the historically deployed Zig 0.13.0 binary; its successful DSP tests are not a claim of byte-for-byte binary reproduction or a new native listening acceptance.

## Runtime setup

The unchanged bridge source expects the installed player entry point at:

```text
C:\Program Files\Common Files\VST3\Vienna Synchron Player.vst3
```

If your player uses a different location, edit the `originalPath` constant in `smooth_bridge.cpp` and `offline_bridge.cpp`, then rebuild. This is the standard player install path, not a bundled dependency. Do not substitute a sample-library path or player core DLL for the VST3 entry point.

Place **Synchron Smooth Bridge.vst3** and **VSL Host Dynamics Smoother.vst3** in the same plugin directory and add that directory to the custom MuseScore build's VST search paths. Select Smooth Bridge as the instrument and place the Smoother first in its effect chain. The smoother locates the bridge relative to its own module. Keep the legacy Offline Bridge Fix only when opening scores that reference it.

The historical quartet setting is 50 / 10 / 10 / 15 ms for Violin I / Violin II / Viola / Cello. The effect's default is 20 ms and its range is 0.5–100 ms; these are gain-transition durations, not moved score onsets. Plugin class IDs and serialized state are retained. The tested player core was 1.3.0.3022. Different hosts, player versions, routing, or buffer layouts need their own playback/export check.

The plugins create diagnostic logs beside their modules when writable. Keep these local; they can contain machine-specific information. Missing or incompatible reference audio causes the smoother to bypass rather than claim successful gain correction.

## Source and license boundaries

`smooth_bridge.cpp`, `offline_bridge.cpp`, `dynamics_smoother.cpp`, the `raw_reference_*` headers/implementation, and the two C++ test files preserve the current maintained source bytes. `build.py` is a new portable build entry point. Vienna software and samples remain separate proprietary dependencies; this repository grants no rights to redistribute them. The project license does not replace Steinberg's retained notices or imply endorsement by MuseScore, Steinberg, or Vienna Symphonic Library.
