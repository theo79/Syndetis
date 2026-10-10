"""Evaluate actual firmware pure logic with C++ constexpr; no hardware.
Every loop and assertion executes during compilation. No duplicated model.
"""
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
description=json.loads((ROOT/'build/project_description.json').read_text())
subprocess.run([description['c_compiler'],'-x','c++','-std=gnu++20','-Wall','-Werror',
                '-I',str(ROOT/'main'),'-fsyntax-only',str(ROOT/'tests/discovery_logic_test.c')],check=True)
print('PASS: constexpr evaluation of actual discovery logic: /24 /25 /26 /30 /16 /31 /32, limits, exclusions, numeric sorting, ARP/ICMP presence, invalid masks and IPs.')
