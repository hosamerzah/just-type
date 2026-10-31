"""Run sequential suites and write evidence. Exit 3 means desktop checks blocked."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import platform
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--build-x64',type=Path,default=ROOT/'build-clang-x64')
parser.add_argument('--build-x86',type=Path,default=ROOT/'build-clang-x86')
parser.add_argument('--installed',action='store_true',help='Also check installed-profile routing; needs the normal user desktop')
parser.add_argument('--output',type=Path,default=ROOT/'build-package/test-results.json')
args=parser.parse_args()
report=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),windows=platform.platform(),results=[])

def run(name,command,timeout=360):
    start=time.monotonic()
    entry=dict(name=name,command=[str(v) for v in command])
    try:
        process=subprocess.run(entry['command'],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        entry.update(exit_code=process.returncode,status='passed' if process.returncode==0 else 'blocked' if process.returncode==3 else 'failed',output=process.stdout+process.stderr)
    except subprocess.TimeoutExpired:
        entry.update(status='failed',output='Timeout; subprocess stopped. No automatic retry.')
    except OSError as error:
        entry.update(status='blocked',output=str(error))
    entry['seconds']=round(time.monotonic()-start,3)
    if command and str(command[0])=='ctest':
        folder=Path(command[command.index('--test-dir')+1])
        log=folder/'Testing/Temporary/LastTest.log'
        if log.exists():entry['test_log']=log.read_text(encoding='utf-8',errors='replace')
    report['results'].append(entry)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f"{entry['status'].upper()} {name}",flush=True)
    return entry

for architecture,folder in [('x64',args.build_x64),('x86',args.build_x86)]:
    folder=folder.resolve()
    for test in ('installer_state_checks','keyboard_behavior','windows_tsf_integration','settings_ui_integration','build_tool_checks'):
        run(f'{architecture} {test}',['ctest','--test-dir',folder,'-R',f'^{test}$','--timeout','300','--output-on-failure'],330)
    control=folder/'AutoKeyboardControl.exe'
    if control.exists():
        preflight=run(f'{architecture} desktop preflight',[control,'--preflight'],30)
        run(f'{architecture} registration metadata',[control,'--status'],30)
        if args.installed and preflight['status']=='passed':
            run(f'{architecture} Windows installed-profile dispatcher',[folder/'keyboard_tsf_tests.exe','--installed-profile'],120)
    for executable in ('AutoKeyboardIME.dll','AutoKeyboardControl.exe','keyboard_tsf_tests.exe'):
        path=folder/executable
        if path.exists():report.setdefault('hashes',{})[f'{architecture}/{executable}']=hashlib.sha256(path.read_bytes()).hexdigest()
report['real_application_typing']='Unverified until recorded with the physical-typing checklist; synthetic tests do not certify applications.'
args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
raise SystemExit(1 if any(r['status']=='failed' for r in report['results']) else 3 if any(r['status']=='blocked' for r in report['results']) else 0)
