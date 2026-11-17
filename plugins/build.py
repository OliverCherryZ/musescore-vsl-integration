"""Build Windows x64 VST3 bridges and run sample-free C++ tests with MSVC.

Run from an x64 Native Tools Command Prompt. Dependencies are supplied by the
caller; this script never downloads, installs, registers, or loads VSL software.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=os.environ.get('VST3_SDK_ROOT'),
                        help='Directory containing pluginterfaces/ (or VST3_SDK_ROOT)')
    parser.add_argument('--output-dir', type=Path, default=Path(__file__).resolve().parent / 'build' / 'msvc')
    parser.add_argument('--cl', default='cl.exe', help='MSVC compiler executable')
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--skip-tests', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('These plugins currently support Windows x64 only.')
    if not args.sdk:
        parser.error('Provide --sdk or VST3_SDK_ROOT.')
    root = Path(__file__).resolve().parent
    sdk = args.sdk.resolve(strict=True)
    output = args.output_dir.resolve()
    if output == root or root.is_relative_to(output):
        parser.error('Output must not be the source directory or one of its parents.')
    cl = shutil.which(args.cl)
    if not cl:
        parser.error('cl.exe was not found; use an x64 Native Tools Command Prompt.')
    target_arch = os.environ.get('VSCMD_ARG_TGT_ARCH')
    if target_arch and target_arch.lower() not in ('x64', 'amd64'):
        parser.error('Use an x64 MSVC target environment.')
    common = [sdk / 'pluginterfaces/base/funknown.cpp',
              sdk / 'pluginterfaces/base/coreiids.cpp', root / 'deps/vstinitiids.cpp']
    targets = [
        ('Synchron Smooth Bridge.vst3', ['smooth_bridge.cpp', 'raw_reference_broker.cpp'], True, []),
        ('Synchron Offline Bridge Fix.vst3', ['offline_bridge.cpp'], True, []),
        ('VSL Host Dynamics Smoother.vst3', ['dynamics_smoother.cpp'], True, []),
    ]
    if not args.skip_tests:
        targets += [
            ('smoother_selftest.exe', ['dynamics_smoother.cpp'], False, ['/DSMOOTHER_SELF_TEST=1']),
            ('broker_smoke.exe', ['broker_smoke.cpp', 'raw_reference_broker.cpp'], False, []),
            ('multibus_smoke.exe', ['multibus_smoke.cpp', 'raw_reference_broker.cpp'], False, []),
        ]
    jobs = []
    for filename, names, dll, defines in targets:
        sources = [root / name for name in names] + common
        for source in sources:
            if not source.is_file():
                parser.error(f'Missing source dependency: {source}')
        command = [cl, '/nologo', '/MT', '/O2', '/EHsc', '/std:c++17', '/utf-8',
                   '/DRELEASE=1', '/DBRIDGE_VARIANT_FIX=1', '/I' + str(sdk)]
        if dll:
            command += ['/LD']
        command += defines + [str(source) for source in sources]
        command += ['/Fe:' + str(output / filename), '/link', '/MACHINE:X64', 'ole32.lib']
        jobs.append({'name': filename, 'command': command, 'test': not dll})
    if args.plan_only:
        print(json.dumps(jobs, indent=2))
        return 0
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for job in jobs:
        print('Building ' + job['name'], flush=True)
        result = subprocess.run(job['command'], cwd=output, capture_output=True)
        (output / (job['name'] + '.build.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            print((result.stdout + result.stderr).decode('utf-8', 'replace'))
            return result.returncode
        artifact = output / job['name']
        record = {'name': job['name'], 'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest()}
        if job['test']:
            tested = subprocess.run([str(artifact)], cwd=output, capture_output=True)
            (output / (job['name'] + '.test.log')).write_bytes(tested.stdout + tested.stderr)
            record['testExitCode'] = tested.returncode
            record['testOutput'] = tested.stdout.decode('utf-8', 'replace').strip()
            if tested.returncode:
                print((tested.stdout + tested.stderr).decode('utf-8', 'replace'))
                return tested.returncode
            print(record['testOutput'], flush=True)
        results.append(record)
    report = {'status': 'BUILD_AND_SELECTED_TESTS_PASSED', 'testsSkipped': args.skip_tests,
              'installed': False, 'loadedProprietaryPlayer': False, 'artifacts': results}
    (output / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
