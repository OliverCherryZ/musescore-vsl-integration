"""Stage a Windows ZIP distribution from an existing custom MuseScore install.

Only a new destination is written. This script does not install/register plugins.
Run from the repository checkout that supplies the public runtime configuration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', required=True, type=Path)
    parser.add_argument('--companion-plugins', required=True, type=Path)
    parser.add_argument('--runtime-licenses', required=True, type=Path)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args()
    source = args.installation.resolve(strict=True)
    destination = args.destination.resolve()
    if destination.exists():
        parser.error('Use a new destination; existing work is not overwritten.')
    repo = Path(__file__).resolve().parents[1]
    folders = ('bin', 'extensions', 'locale', 'qml', 'sound', 'styles', 'tables',
               'templates', 'translations', 'wallpapers', 'workspaces')
    for name in folders:
        if not (source / name).is_dir():
            parser.error('Missing installation directory: ' + name)
    destination.mkdir(parents=True)
    for name in folders:
        shutil.copytree(source / name, destination / name,
                        ignore=shutil.ignore_patterns('*.pdb', 'icuuc.dll', 'qmltooling'))
    standard_plugins = ('intervals', 'lilyrics', 'new_retrograde', 'note_names', 'tuning')
    for name in standard_plugins:
        shutil.copytree(source / 'plugins' / name, destination / 'plugins' / name)
    shutil.copytree(repo / 'runtime/plugins/vsl_articulation_selector',
                    destination / 'plugins/vsl_articulation_selector')
    shutil.copytree(repo / 'runtime/expression-maps', destination / 'expression-maps')
    shutil.copytree(args.companion_plugins.resolve(strict=True), destination / 'Companion-Plugins')
    shutil.copytree(repo / 'docs', destination / 'docs')
    shutil.copytree(repo / 'examples', destination / 'examples')
    shutil.copy2(repo / 'LICENSE', destination / 'LICENSE')
    shutil.copytree(repo / 'LICENSES', destination / 'LICENSES')
    license_source = args.runtime_licenses.resolve(strict=True)
    license_destination = destination / 'LICENSES/Runtime'
    license_destination.mkdir()
    notice_index = []
    for index, notice in enumerate(sorted(license_source.rglob('*'))):
        if not notice.is_file():
            continue
        relative = notice.relative_to(license_source)
        output = (license_destination / notice.name if len(relative.parts) == 1 else
                  license_destination / relative.parts[0] / (str(index).zfill(3) + '-' + notice.name))
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, output)
        notice_index.append({'originalPath': relative.as_posix(),
                             'distributedPath': output.relative_to(license_destination).as_posix(),
                             'sha256': hashlib.sha256(notice.read_bytes()).hexdigest()})
    (license_destination / 'original-paths.json').write_text(
        json.dumps(notice_index, indent=2) + '\n', encoding='utf-8')
    shutil.copy2(repo / 'THIRD_PARTY_NOTICES.md', destination / 'THIRD_PARTY_NOTICES.md')
    (destination / 'Start-MuseScore.cmd').write_text(
        '@echo off\r\nstart "" "%~dp0bin\\MuseScore4.exe" %*\r\n', encoding='ascii', newline='')
    print(destination)


if __name__ == '__main__':
    main()
