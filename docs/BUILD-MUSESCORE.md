# Build the MuseScore integration

The patch is based on **MuseScore Studio v4.7.7**, commit
`b750c43e316cf6a00e70451870b231b12f400bda`. It includes the current notation and
playback changes, compact articulation metadata, loading preparation, RPC
transport, and the final weak-owner timer / VST lifetime fixes. It also includes
the Zstandard 1.5.7 source dependency and its original licenses.

This is a source distribution. Windows x64 was the development platform. Other
platforms have not been validated. The packaging checks on 2026-11-18 verify
source reconstruction and selected existing test executables; they do not claim
a new full application build or a fresh complete audio acceptance pass.

## Restore the exact source

Install Python 3.9+ and Git, then run from the root of this repository:

```powershell
python .\musescore\prepare_source.py ..\musescore-vsl-work
```

The destination must be absent or empty. The script clones the official tag,
checks out the exact commit, checks the patch SHA-256, applies the patch, restores
test support, and verifies every changed/new source file against
`musescore/source-manifest.json`. It never copies an existing local checkout's
uncommitted edits or changes an installed application.

The script adds checkout-local `.git/info/attributes` entries for the patched
paths to retain their recorded line endings. This prevents upstream `text=auto`
rules from changing source-manifest byte hashes on Windows; tracked upstream
attributes are unchanged.

For an offline restore from an existing official checkout containing the base
commit:

```powershell
python .\musescore\prepare_source.py ..\musescore-vsl-work --local-upstream C:\dev\MuseScore
```

The resulting layout is required by the patch's CMake includes:

```text
musescore-vsl-work/
  src/                         official checkout + the complete patch
  host_patch/                  mapper regression CMake/source/profile fixtures
  full_coverage_20261117/      state codec and load observation test support
  source-verification.json    reconstruction results
```

Keep those sibling directories together. Do not apply the earlier development
patches on top of this patch. The upstream copyright notices and GPL-3.0-only
licensing remain intact; bundled Zstandard retains its own license files.

## Configure and build on Windows

Use an x64 Visual Studio developer shell with MSVC and the Windows SDK available.
The development environment used MSVC 14.44.35207, Qt 6.10.2 for MSVC 2022 x64,
CMake 4.4.5 and Ninja 1.13.2. Qt must include the modules required by MuseScore,
including Qt Declarative, Quick, and QML. Upstream dependencies are still obtained
through MuseScore's build scripts and CMake; this repository does not redistribute
the compiler, SDK, Qt installation, commercial plugins, or sample library.

Set `QtRoot` to the actual Qt installation. Choose a separate install directory.
Run each command only after the preceding command succeeds:

```powershell
$work = (Resolve-Path ..\musescore-vsl-work).Path
$QtRoot = 'C:\Qt\6.10.2\msvc2022_64'

cmake -S "$work\src" -B "$work\build" -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  "-DCMAKE_PREFIX_PATH=$QtRoot" `
  "-DCMAKE_INSTALL_PREFIX=$work\install" `
  -DCMAKE_TLS_VERIFY=ON `
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
  -DMUSESCORE_BUILD_CONFIGURATION=app `
  -DMUSE_APP_BUILD_MODE=dev `
  -DMUSE_APP_INSTALL_SUFFIX=VSL `
  -DCMAKE_BUILD_NUMBER=20261118 `
  -DMUSESCORE_REVISION=vsl-integration-source-20261118 `
  -DMUE_RUN_LRELEASE=ON `
  -DMUE_RUN_WINDEPLOYQT=ON `
  -DMUE_DOWNLOAD_SOUNDFONT=OFF `
  -DMUSE_ENABLE_UNIT_TESTS=ON `
  -DMUSE_MODULE_VST=ON `
  -DMUSE_MODULE_NETWORK_WEBSOCKET=ON `
  -DMUSE_MODULE_UPDATE=OFF `
  -DMUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT=OFF `
  -DMUSE_MODULE_GLOBAL_LOGGER_DEBUGLEVEL=OFF `
  -DMUSE_COMPILE_USE_UNITY=ON

cmake --build "$work\build" --parallel 4 --target `
  MuseScoreStudio engraving_tests muse_audio_rpcpacker_tests `
  muse_vst_expression_mapper_tests muse_vst_mapper_baseline_compat_tests `
  muse_vst_mapper_extension_tests muse_vst_harmonic_tremolo_mapper_tests `
  muse_vsl_sample_state_tests muse_vsl_load_observation_tests
```

The app target is `MuseScoreStudio`; its Windows output is `MuseScore4.exe`.
The frontend and audio engine must come from the same patched build because the
internal RPC payload has changed. Build translations before installing:

```powershell
cmake --build "$work\build" --parallel 4 --target translations
cmake --install "$work\build"
```

These are portable build instructions derived from the working configuration.
A clean-machine dependency download/build has not been repeated for this source
publication. The restore script has been checked against a complete local
upstream clone. If CMake cannot fetch a dependency, fix network/certificate
configuration or provide the upstream dependency explicitly; retain TLS checks.

## Run source regressions

After building the targets above, from this repository root run:

```powershell
python .\musescore\run_regressions.py "$work" --qt-bin "$QtRoot\bin"
```

This runs the baseline/extended/harmonic mapper fixtures, sample-load observation,
engraving playback and staff-text metadata tests, and RPC packer tests. A new
timestamped output directory contains logs and the summary. No VSL player,
commercial preset, or native MuseScore UI is loaded by this runner. The profile
fixtures are historical 13/19/20-route regression inputs, with machine-specific
provenance fields removed; use the published current profile for actual playback.

`muse_vsl_sample_state_tests` is built but deliberately excluded from the default
runner: it needs a locally supplied compatible plugin-state blob and an expected
leaf-path specification. The repository does not ship a commercial player's
factory preset or state blob. Its invocation is:

```powershell
& "$work\build\muse_vsl_sample_state_tests.exe" local-state.bin expected-leaves.json result-state.bin
```

The original full-catalog mapper, sequencer, preparation-request, and lifetime
test C++ harnesses are retained under `full_coverage_20261117` as source support.
Only the targets explicitly listed above are wired into the preserved CMake
configuration. The full-catalog/native evidence from 2026-11-17 is historical;
it must not be described as all 549 branches having passed a listening test.

## Configure playback after building

Install your own licensed Synchron Player and Synchron Solo Strings. Build the
bridge/effect components using their repository instructions. Copy the published
expression map and selector files to the locations described in the main README,
then select the intended instrument instances and load a suitable local preset.
The open source code cannot supply the commercial samples. Old MuseScore builds
and MusicXML do not preserve the new hidden staff-text articulation binding;
keep the editable original in MSCZ/MSCX with this patched build.
