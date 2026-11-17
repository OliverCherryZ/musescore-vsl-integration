"""Package an uncommitted MuseScore checkout without changing its Git state.

Run with Python 3 and Git on PATH. All Git writes happen in temporary directories.
The generated patch uses the exact official v4.7.7 baseline, including new files.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile

BASE = "b750c43e316cf6a00e70451870b231b12f400bda"


def run(cwd, *args):
    return subprocess.run(["git", "-c", "core.autocrlf=false", "-C", str(cwd), *args],
                          check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if run(source, "rev-parse", "HEAD").decode().strip() != BASE:
        raise SystemExit("Expected the official v4.7.7 baseline commit")
    before_status = run(source, "status", "--porcelain=v1", "-z")
    tracked = run(source, "diff", "HEAD", "--name-only", "-z").decode().split("\0")
    added = run(source, "ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")
    tracked, added = sorted(filter(None, tracked)), sorted(filter(None, added))
    paths = sorted(set(tracked + added))
    snapshots = {path: (source / path).read_bytes() for path in paths}
    baseline = {path: run(source, "show", BASE + ":" + path) for path in tracked}
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="musescore-vsl-source-") as temporary:
        temp = Path(temporary)
        staging, verification = temp / "staging", temp / "verification"
        for directory in (staging, verification):
            directory.mkdir()
            run(directory, "init", "--quiet")
            for path, data in baseline.items():
                destination = directory / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
        run(staging, "add", "--all")
        base_tree = run(staging, "write-tree").decode().strip()
        for path, data in snapshots.items():
            destination = staging / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        run(staging, "add", "--force", "--all")
        patch = run(staging, "diff", "--cached", "--binary", "--full-index", base_tree)
        patch_path = output / "musescore-v4.7.7-vsl-integration.patch"
        patch_path.write_bytes(patch)
        run(verification, "apply", "--check", str(patch_path))
        run(verification, "apply", str(patch_path))
        files = []
        for path, data in snapshots.items():
            restored = (verification / path).read_bytes()
            if restored != data:
                raise RuntimeError("Reconstruction mismatch: " + path)
            files.append({"path": path, "status": "modified" if path in tracked else "added",
                          "bytes": len(data), "sha256": digest(data),
                          "baseSha256": digest(baseline[path]) if path in baseline else None})
    if run(source, "status", "--porcelain=v1", "-z") != before_status:
        raise RuntimeError("Source status changed during packaging")
    if any((source / path).read_bytes() != data for path, data in snapshots.items()):
        raise RuntimeError("Source bytes changed during packaging")
    manifest = {
        "formatVersion": 1,
        "releaseDate": "2026-11-18",
        "upstreamRepository": "https://github.com/musescore/MuseScore.git",
        "upstreamTag": "v4.7.7", "baseCommit": BASE,
        "patch": {"path": patch_path.name, "bytes": len(patch), "sha256": digest(patch)},
        "modifiedTrackedFiles": len(tracked), "addedFiles": len(added), "files": files,
        "verification": {
            "baselineFilesReadFromExactUpstreamGitObjects": True,
            "independentTemporaryRepository": True,
            "gitApplyCheckPassed": True,
            "allPatchedFilesByteIdenticalToSource": True,
            "sourceGitIndexAndWorkingTreeUnchanged": True,
            "applicationRebuiltDuringPackaging": False,
            "scope": "Every touched file was restored from its exact official base blob and patched; this is source reconstruction verification, not a new full build."
        }
    }
    (output / "source-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"modified": len(tracked), "added": len(added),
                      "patchBytes": len(patch), "patchSha256": digest(patch),
                      "allPatchedFilesByteIdentical": True}, indent=2))


if __name__ == "__main__":
    main()
