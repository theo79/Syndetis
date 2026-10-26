from pathlib import Path
import argparse,os,shlex,subprocess,sys
root=Path(__file__).resolve().parent.parent
os.chdir(root)
parser=argparse.ArgumentParser();parser.add_argument('--skip-browser',action='store_true');args=parser.parse_args()
cc=shlex.split(os.environ.get('CC','cc'),posix=os.name!='nt');out=root/'tmp/host-tests';out.mkdir(parents=True,exist_ok=True)
suffix='.exe' if os.name=='nt' else ''
cases={
'core':['-Imain','main/neighbors.c','main/button_logic.c','tests/core_test.c'],
'ui':['-Itests/host_stubs','-Imain','main/neighbors.c','main/network_policy.c','main/manual_ipv4.c','tests/ui_test.c'],
'json':['-Itests/host_stubs','-Imain','main/neighbors.c','main/network_policy.c','tests/status_json_test.c'],
'dhcp':['-Itests/m4_stubs','-Itests/host_stubs','-Imain','main/network_policy.c','main/manual_ipv4.c','main/ping_packet.c','tests/dhcp_test.c'],
'manual':['-Itests/m4_stubs','-Itests/host_stubs','-Imain','main/network_policy.c','main/manual_ipv4.c','main/ping_packet.c','tests/manual_ipv4_test.c'],
'ping':['-Itests/ping_stubs','-Itests/m4_stubs','-Itests/host_stubs','-Imain','main/network_policy.c','main/manual_ipv4.c','main/ping_packet.c','tests/ping_test.c'],
'http':['-Itests/host_stubs','-Imain','main/neighbors.c','main/network_policy.c','tests/http_actions_test.c'],
'guard':['-Itests/m4_stubs','-Itests/host_stubs','-Imain','tests/web_guard_test.c']}
for name,inputs in cases.items():
    binary=out/(name+suffix);subprocess.run(cc+['-std=c11']+inputs+['-o',str(binary)],check=True)
    if name!='json':subprocess.run([str(binary)],check=True)
subprocess.run(['node','tests/status_json.test.cjs',str(out/('json'+suffix))],check=True)
subprocess.run(['node','tests/dashboard.test.cjs'],check=True)
if not args.skip_browser:subprocess.run(['node','tests/dashboard.browser.cjs'],check=True)
print('PASS: portable C/Node'+('/browser' if not args.skip_browser else ' (browser explicitly skipped)')+' regressions')
