"""Compile deterministic execution of the actual ARP callback, without hardware."""
import json, subprocess, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
# Regenerate the fixture from its checked-in generator so callback changes are tested.
exec((ROOT/'tests/fast_discovery_fixture.py').read_text())
compiler=json.loads((ROOT/'build/project_description.json').read_text())['c_compiler']
subprocess.run([compiler,'-x','c++','-std=gnu++20','-Wall','-Werror',
 '-fconstexpr-ops-limit=100000000','-I',str(ROOT/'main'),'-fsyntax-only',
 str(ROOT/'build/fast_discovery_test.cpp')],check=True)
source=(ROOT/'main/discovery.c').read_text()
arp=source[source.index('    arp_job.index=index;'):source.index('    unsigned processed = 0;')]
assert 'esp_ping_' not in arp
assert 'while (processed < hosts)' in source
assert 'cfg.interval_ms=0' in source
assert 'pause_checked(n.revision,1100)' in source
assert 'tcpip_try_callback(lookup_arp,NULL)' in source
assert 'diagnostic_current(arp_job.token)' in source
print('PASS: actual rolling callback: 253 requests/10-entry cache, repeated observations, MAC updates, eviction retention, stale-cache reset, source filtering, link-down and stale callback rejection. Logical ARP sweep 10,120 ms; ICMP scales with discovered hosts only.')

# Determine the effective cache size with the actual configured firmware includes.
commands=json.loads((ROOT/'build/compile_commands.json').read_text())
command=next(item['command'] for item in commands if item['file'].replace('\\','/').endswith('/discovery.c'))
command=re.sub(r' -o .*? -c ', ' -E -dM ', command)
macros=subprocess.run(command,cwd=ROOT/'build',capture_output=True,text=True,check=True).stdout
assert re.search(r'^#define ARP_TABLE_SIZE 10$',macros,re.M)
print('PASS: configured firmware preprocessing confirms ARP_TABLE_SIZE=10.')
