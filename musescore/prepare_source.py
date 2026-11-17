"""Restore the exact patched MuseScore source and sibling test support.

Requires Python 3.9+ and Git. The destination must be absent or empty.
No compiler, VSL player, sample library, or existing MuseScore install is changed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
UPSTREAM = "https://github.com/musescore/MuseScore.git"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args):
    subprocess.run(["git", "-c", "core.autocrlf=false", *map(str, args)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--local-upstream", type=Path,
                        help="Clone an existing official MuseScore checkout offline; its working tree is ignored")
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists() and any(destination.iterdir()):
        parser.error("Destination must be empty; existing work is never overwritten")
    manifest = json.loads((HERE / "source-manifest.json").read_text(encoding="utf-8"))
    patch = HERE / manifest["patch"]["path"]
    if sha256(patch) != manifest["patch"]["sha256"]:
        raise SystemExit("Patch checksum mismatch; obtain an intact release")
    support_manifest = json.loads((HERE / "support-manifest.json").read_text(encoding="utf-8"))
    for item in support_manifest["files"]:
        if sha256(HERE / "support" / item["path"]) != item["sha256"]:
            raise SystemExit("Support checksum mismatch: " + item["path"])
    destination.mkdir(parents=True, exist_ok=True)
    source = destination / "src"
    if args.local_upstream:
        git("clone", "--no-hardlinks", "--no-checkout", args.local_upstream.resolve(), source)
    else:
        git("clone", "--branch", manifest["upstreamTag"], "--depth", "1", "--no-checkout", UPSTREAM, source)
    git("-C", source, "config", "core.autocrlf", "false")
    # Upstream's text=auto attributes can normalize newly added LF/CRLF files
    # even with core.autocrlf=false. Preserve the recorded bytes for patch paths
    # using checkout-local attributes; no upstream tracked file is changed.
    attributes = source / ".git/info/attributes"
    attributes.parent.mkdir(parents=True, exist_ok=True)
    attributes.write_text("# Preserve source-manifest bytes for this restored patch.\n" +
                          "".join(item["path"] + " -text\n" for item in manifest["files"]),
                          encoding="utf-8")
    git("-C", source, "checkout", "--detach", manifest["baseCommit"])
    git("-C", source, "apply", "--check", patch)
    git("-C", source, "apply", "--whitespace=nowarn", patch)
    for entry in (HERE / "support").iterdir():
        target = destination / entry.name
        if entry.is_dir():
            shutil.copytree(entry, target)
        else:
            shutil.copy2(entry, target)
    mismatches = [item["path"] for item in manifest["files"]
                  if sha256(source / item["path"]) != item["sha256"]]
    if mismatches:
        raise SystemExit("Reconstructed source mismatches: " + ", ".join(mismatches))
    result = {"baseCommit": manifest["baseCommit"], "patchSha256": sha256(patch),
              "patchedFilesVerified": len(manifest["files"]),
              "allPatchedFilesByteIdentical": True,
              "supportFilesVerified": len(support_manifest["files"]),
              "builtApplication": False}
    (destination / "source-verification.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    print("Source is ready. See docs/BUILD-MUSESCORE.md for build and test commands.")


if __name__ == "__main__":
    main()
