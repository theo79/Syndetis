"""Compile policy assertions with the configured ESP-IDF compiler; no hardware."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
description = json.loads((ROOT / "build/project_description.json").read_text())
subprocess.run([description["c_compiler"], "-std=c11", "-Wall", "-Werror",
                "-I", str(ROOT / "main"), "-fsyntax-only",
                str(ROOT / "tests/autotest_rules_test.c")], check=True)
count = (ROOT / "tests/autotest_rules_test.c").read_text().count("EXPECT(") - 1
print(f"PASS: {count} compile-time policy assertions (local PASS, warnings, failures, masks, gateways).")
