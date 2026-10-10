"""Evaluate actual CDP parser and selection bodies using normal GCC constexpr.
No packet parser is implemented in Python. No hardware is contacted.
"""
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/switch_info_cdp_host'
OUT.mkdir(parents=True,exist_ok=True)
compiler=json.loads((ROOT/'build/project_description.json').read_text())['c_compiler']
model=(ROOT/'main/switch_info_parse.h').read_text()
model=re.sub(r'^bool (switch_info_\w+)\(',r'constexpr bool \1(',model,flags=re.M)
(OUT/'switch_info_parse.h').write_text(model)
selection=(ROOT/'main/switch_info_select.h').read_text()
selection=re.sub(r'^(unsigned|bool) (switch_info_\w+)\(',r'constexpr \1 \2(',selection,flags=re.M)
selection=selection.replace('const switch_neighbor_t *switch_info_selected','constexpr const switch_neighbor_t *switch_info_selected')
(OUT/'switch_info_select.h').write_text(selection)
lldp=(ROOT/'main/switch_info_parse.c').read_text()
source=(ROOT/'main/switch_info_cdp.c').read_text()+'\n'+lldp[lldp.index('bool switch_info_same_neighbor'):]
source+='\n'+(ROOT/'main/switch_info_select.c').read_text()
source=re.sub(r'^#include .*\n','',source,flags=re.M)
source=source.replace('static const uint8_t','static constexpr uint8_t')
for name in ['out','n','target']:
    source=source.replace(f'memset({name}, 0, sizeof(*{name}));',f'*{name} = {{}};')
source=source.replace('goto malformed;','return test_malformed(out);')
source=source.replace('malformed:\n    *out = {};\n    return false;','')
source=re.sub(r'^(static )?(bool|void|uint16_t|uint32_t|unsigned) (\w+)\(',r'\1constexpr \2 \3(',source,flags=re.M)
source=source.replace('const switch_neighbor_t *switch_info_selected','constexpr const switch_neighbor_t *switch_info_selected')
source=re.sub(r'\bmemcpy\b','test_copy',source)
source=re.sub(r'\bmemcmp\b','test_compare',source)
shim=r'''
#include "switch_info_select.h"
#include <initializer_list>
template<class A,class B> constexpr void test_copy(A *a,const B *b,size_t n) { for(size_t i=0;i<n;++i) a[i]=b[i]; }
template<class A,class B> constexpr int test_compare(const A *a,const B *b,size_t n) {
    for(size_t i=0;i<n;++i) { if(a[i]!=b[i]) return a[i]<b[i] ? -1:1; }
    return 0;
}
constexpr bool test_malformed(switch_neighbor_t *out) { *out={}; return false; }
'''
fixture=OUT/'cdp_constexpr.cpp'
fixture.write_text(shim+source+(ROOT/'tests/switch_info_cdp_test.cpp').read_text())
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror',
                '-fconstexpr-ops-limit=100000000','-I',str(OUT),'-fsyntax-only',str(fixture)],check=True)
backend=(ROOT/'main/switch_info.c').read_text()
assert 'slot->protocol = protocol;' in backend
assert backend.count('slot_current_locked(slot)')==2
assert 'memset(&context.observations, 0, sizeof(context.observations));' in backend
assert 'switch_info_observe(&context.observations, &parsed, now)' in backend
assert 'slot->received_us + (int64_t)parsed.ttl * 1000000' in backend
assert 'ETH_CMD_S_PROMISCUOUS' not in backend and 'ETH_CMD_S_ALL_MULTICAST' not in backend
print('PASS: actual CDP encapsulation/length/checksum(v1/v2, even/odd signed-byte vectors), TLVs, optional/address envelopes, padding, malformed/duplicate/count bounds and mutation fixtures.')
print('PASS: actual LLDP preference/CDP fallback, independent expiry, matching withdrawals, queue-delay cutoff, lifecycle/generation/epoch predicates and worker integration invariants.')
