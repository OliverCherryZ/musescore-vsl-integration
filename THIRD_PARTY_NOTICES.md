# Third-party notices

## MuseScore Studio

The MuseScore patch is based on MuseScore Studio v4.7.7, commit `b750c43e316cf6a00e70451870b231b12f400bda`.

- Upstream: https://github.com/musescore/MuseScore
- Copyright: MuseScore Limited and contributors, as identified in the source files.
- License: GNU General Public License version 3. The upstream notice and license are retained in `LICENSES/MuseScore-LICENSE.txt` and in reconstructed upstream sources.
- Local changes are distributed as a patch; existing copyright headers remain intact.

## Steinberg VST3 interfaces

The plugin build targets `vst3_pluginterfaces` tag `v3.7.12_build_20`. That release offers a GPL v3 licensing option; this GPL project uses that option for the interface dependency. Download the pinned dependency as described in the plugin build instructions. The commercial Vienna plugin is not part of that dependency or this repository.

- Upstream: https://github.com/steinbergmedia/vst3_pluginterfaces/tree/v3.7.12_build_20
- License: https://github.com/steinbergmedia/vst3_pluginterfaces/blob/v3.7.12_build_20/LICENSE.txt
- `plugins/deps/vstinitiids.cpp` retains its embedded Steinberg BSD 3-Clause copyright and license notice. That notice applies to the file independently of the repository's default license.

## Zstandard

The MuseScore patch includes Zstandard 1.5.7 sources needed by the local host changes. Their original copyright, `LICENSE` and `COPYING` are retained in the reconstructed tree. Zstandard offers BSD 3-Clause and GPL v2 licensing alternatives; the BSD option is used for this GPL v3 distribution.

- Upstream: https://github.com/facebook/zstd/tree/v1.5.7
- Keep the included Zstandard notices with any redistributed source or binary incorporating it.

## Separately installed products

Vienna Synchron Player, Synchron Solo Strings, their samples, commercial presets, license data, and installers are not redistributed or relicensed here. Users supply their own compatible licensed installation. The source uses the public VST3 host/plugin interface; these wrappers do not grant rights to the underlying products.

MuseScore, VST, and Vienna product names remain the property of their respective owners. This repository is an independent integration and is not endorsed by those owners.
