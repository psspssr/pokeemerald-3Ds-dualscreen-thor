#!/usr/bin/env python3
"""Capture a bounded, local Perfetto trace and Emerald memory snapshots over adb.

Requires Android 10+, adb access and an already running game. Does not start,
stop or reconfigure the app, change device performance settings, or read saves.
The output directory must be new. Trace data includes system scheduling events.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
import uuid

PACKAGE = 'com.emerald3ds.android'


class Adb:
    def __init__(self, binary, serial):
        self.command = [binary, '-s', serial]

    def run(self, *args, timeout=20, input=None):
        return subprocess.run(self.command + list(args), input=input,
                              capture_output=True, text=True, timeout=timeout,
                              check=True).stdout.strip()

    def shell(self, *args, **kwargs):
        return self.run('shell', shlex.join(args), **kwargs)

    def game_pid(self):
        try:
            pid = self.shell('pidof', PACKAGE)
        except subprocess.CalledProcessError as error:
            if error.returncode != 1:
                raise
            return None
        return pid if re.fullmatch(r'[1-9][0-9]*', pid) else None


def trace_config(seconds, sdk):
    # Native SurfaceView rendering must be assessed from its actual submission
    # intervals and scheduling, not Android UI Choreographer/gfxinfo FPS.
    timeline = '''data_sources { config { name: "android.surfaceflinger.frametimeline" } }
''' if sdk >= 31 else ''
    return f'''buffers {{ size_kb: 16384 fill_policy: RING_BUFFER }}
duration_ms: {seconds * 1000}
write_into_file: true
file_write_period_ms: 1000
max_file_size_bytes: 67108864
data_sources {{ config {{ name: "linux.ftrace" ftrace_config {{
  ftrace_events: "sched/sched_switch"
  ftrace_events: "sched/sched_waking"
  ftrace_events: "power/cpu_frequency"
  ftrace_events: "power/cpu_idle"
  atrace_categories: "gfx"
  atrace_categories: "view"
  atrace_categories: "input"
  atrace_categories: "wm"
  atrace_categories: "am"
  atrace_apps: "{PACKAGE}"
}} }} }}
data_sources {{ config {{ name: "linux.process_stats" process_stats_config {{
  scan_all_processes_on_start: true
  proc_stats_poll_ms: 1000
}} }} }}
{timeline}'''


def capture(adb, out, seconds):
    out.mkdir(parents=True, exist_ok=False)
    report = {'package': PACKAGE, 'requested_seconds': seconds,
              'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'status': 'collecting', 'warnings': []}
    remote = f'/data/misc/perfetto-traces/emerald-{uuid.uuid4().hex}.perfetto-trace'

    def optional(name, *command):
        try:
            (out / name).write_text(adb.shell(*command) + '\n')
        except (subprocess.SubprocessError, OSError) as error:
            report['warnings'].append(f'{name}: {error}')

    try:
        report['device'] = {key: adb.shell('getprop', prop) for key, prop in {
            'manufacturer': 'ro.product.manufacturer', 'model': 'ro.product.model',
            'android': 'ro.build.version.release', 'sdk': 'ro.build.version.sdk',
            'abis': 'ro.product.cpu.abilist', 'build': 'ro.build.display.id',
        }.items()}
        sdk = int(report['device']['sdk'])
        if sdk < 29:
            raise ValueError('This capture helper requires Android 10 or newer.')
        pid = adb.game_pid()
        if pid is None:
            raise ValueError('Open the game before starting a profile.')
        report['pid_before'] = pid
        package = adb.shell('dumpsys', 'package', PACKAGE)
        report['package_version'] = [line.strip() for line in package.splitlines()
                                     if re.search(r'\bversion(?:Name|Code)=', line)]
        for name, args in {
            'memory-before.txt': ('dumpsys', 'meminfo', PACKAGE),
            'display-before.txt': ('dumpsys', 'display'),
            'thermal-before.txt': ('dumpsys', 'thermalservice'),
        }.items():
            optional(name, *args)
        config = trace_config(seconds, sdk)
        (out / 'trace-config.pbtxt').write_text(config)
        print(f'Recording {seconds} seconds. Play the scene you want to measure.', flush=True)
        # The remote session has its own duration/size bounds even if adb drops.
        trace_started = time.monotonic()
        result = adb.shell('perfetto', '--txt', '-c', '-', '-o', remote,
                           input=config, timeout=seconds + 30)
        report['trace_command_seconds'] = round(time.monotonic() - trace_started, 3)
        (out / 'perfetto.txt').write_text(result + '\n')
        adb.run('pull', remote, str(out / 'game.perfetto-trace'), timeout=30)
        size = (out / 'game.perfetto-trace').stat().st_size
        if size == 0:
            raise ValueError('Perfetto returned an empty trace.')
        report['trace_bytes'] = size
        optional('memory-after.txt', 'dumpsys', 'meminfo', PACKAGE)
        optional('thermal-after.txt', 'dumpsys', 'thermalservice')
        report['pid_after'] = adb.game_pid()
        if report['pid_after'] != pid:
            raise ValueError('The game process changed during capture; inspect the retained trace.')
        report['status'] = 'captured'
        return report
    except (subprocess.SubprocessError, OSError, ValueError, KeyboardInterrupt) as error:
        report['status'] = 'failed'
        report['error'] = str(error) or type(error).__name__
        if isinstance(error, subprocess.CalledProcessError):
            report['command_error'] = (error.stderr or error.stdout or '').strip()[:4096]
        raise
    finally:
        # Remove only this invocation's random trace, never other sessions.
        try:
            adb.shell('rm', '-f', remote, timeout=10)
        except (subprocess.SubprocessError, OSError) as error:
            report['warnings'].append(f'Remote trace cleanup: {error}')
        report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (out / 'capture.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('ANDROID_SERIAL'),
                        help='Explicit adb device serial (or ANDROID_SERIAL)')
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--seconds', type=int, default=30, choices=range(5, 121), metavar='5..120')
    parser.add_argument('--out', type=Path, required=True, help='New local output directory')
    args = parser.parse_args()
    if not args.serial:
        parser.error('--serial or ANDROID_SERIAL is required; no implicit device selection')
    try:
        capture(Adb(args.adb, args.serial), args.out, args.seconds)
    except (subprocess.SubprocessError, OSError, ValueError, KeyboardInterrupt) as error:
        print(f'Profile capture failed: {error or type(error).__name__}', file=sys.stderr)
        return 1
    print(f'Saved {args.out / "game.perfetto-trace"} and memory snapshots.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
