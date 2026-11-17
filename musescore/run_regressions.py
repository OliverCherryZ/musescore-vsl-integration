"""Run the built, source-level regression targets without loading a VSL player."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path, help="Destination previously used by prepare_source.py")
    parser.add_argument("--qt-bin", type=Path, help="Qt bin directory containing runtime DLLs")
    parser.add_argument("--build-dir", type=Path, help="Alternate directory containing the built test executables")
    parser.add_argument("--output", type=Path, help="New directory for reports; must not already exist")
    args = parser.parse_args()
    work = args.work.resolve()
    build = args.build_dir.resolve() if args.build_dir else work / "build"
    output = args.output or work / ("test-results-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    if args.qt_bin:
        env["PATH"] = str(args.qt_bin.resolve()) + os.pathsep + env.get("PATH", "")
        env["QT_PLUGIN_PATH"] = str(args.qt_bin.resolve().parent / "plugins")
    for key in ("MUSESCORE_VST_EXPRESSION_MAP", "MUSESCORE_VSL_ARTICULATION_MAP",
                "MUSESCORE_VST_EXPRESSION_TRACE_DIR"):
        env.pop(key, None)
    suffix = ".exe" if os.name == "nt" else ""
    profile13 = work / "host_patch/config/vsl_articulation_map.json"
    profile19 = work / "host_patch/extensions/vst/config/vsl_articulation_map_extended19.json"
    profile20 = work / "host_patch/extensions/harmonic_tremolo/vst/config/vsl_articulation_map_extended20.json"
    cases = [
        ("muse_vst_expression_mapper_tests", [profile13]),
        ("muse_vst_mapper_baseline_compat_tests", [profile13]),
        ("muse_vst_mapper_extension_tests", [profile19, profile13]),
        ("muse_vst_harmonic_tremolo_mapper_tests", [profile20, profile19]),
        ("muse_vsl_load_observation_tests", []),
        ("engraving_tests", ["--gtest_filter=Engraving_PlaybackContextTests.*:Engraving_PlaybackEventsRendererTests.*:Engraving_PlaybackModelTests.*:Engraving_StaffTextMetadataTests.*",
                             "--gtest_output=json:" + (output / "engraving.json").as_posix()]),
        ("muse_audio_rpcpacker_tests", ["--gtest_output=json:" + (output / "rpcpacker.json").as_posix()]),
    ]
    reports = []
    for target, parameters in cases:
        executable = build / (target + suffix)
        if not executable.is_file():
            raise SystemExit("Build the missing target first: " + target)
        command = [str(executable), *map(str, parameters)]
        result = subprocess.run(command, cwd=work, env=env, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=600)
        (output / (target + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        reports.append({"target": target, "exitCode": result.returncode,
                        "executableSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                        "summaryLines": [line for line in result.stdout.splitlines()
                                         if "PASSED" in line or line.startswith("PASS ")]})
        print(target, "PASS" if result.returncode == 0 else "FAIL", flush=True)
    report = {"allPassed": all(item["exitCode"] == 0 for item in reports),
              "executedAt": datetime.datetime.now().astimezone().isoformat(),
              "applicationRebuiltDuringRun": False,
              "results": reports, "scope": "Source regressions; no native playback, export, or listening test."}
    (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    raise SystemExit(0 if report["allPassed"] else 1)


if __name__ == "__main__":
    main()
