#!/usr/bin/env python3
"""Build pinned SDK Mbed TLS for HOST and test the production signature verifier.
Does not change the SDK, access hardware, or install dependencies.
"""
import argparse,os,shlex,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf-path',type=Path,required=True)
a=p.parse_args();sdk=a.idf_path.resolve()
revision=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
if revision!='fff9895c82d744c7237be8847347bdd1b07c6643':p.error('Requires the pinned ESP-IDF v6.1 revision')
source=sdk/'components/mbedtls/mbedtls';build=root/'tmp/m6-crypto-host';bridge=root/'tmp/m6-crypto-host-shims'
(bridge/'mbedtls').mkdir(parents=True,exist_ok=True)
# Espressif has legacy include names in software sources. Supply ONLY a host
# forwarding bridge; default host Mbed TLS uses no ESP32 acceleration/config.
for name in ['bignum','ecp']:
    (bridge/'mbedtls'/f'{name}.h').write_text('#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS\n#include "mbedtls/private/'+name+'.h"\n')
cc=shlex.split(os.environ.get('HOST_CC','cc'))
subprocess.run(['cmake','-S',str(source),'-B',str(build),'-DENABLE_PROGRAMS=OFF','-DENABLE_TESTING=OFF','-DGEN_FILES=OFF','-DMBEDTLS_FATAL_WARNINGS=OFF','-DTF_PSA_CRYPTO_FATAL_WARNINGS=OFF',f'-DCMAKE_C_FLAGS=-I{bridge}',f'-DCMAKE_C_COMPILER={cc[0]}'],check=True)
subprocess.run(['cmake','--build',str(build),'-j','4'],check=True)
subprocess.run([sys.executable,str(root/'tests/ota_package_test.py')],check=True)
binary=root/'tmp/host-tests/ota_signature';binary.parent.mkdir(exist_ok=True)
subprocess.run(cc+['-std=c11','-I'+str(root/'main'),'-I'+str(source/'include'),'-I'+str(source/'tf-psa-crypto/include'),'-I'+str(source/'tf-psa-crypto/drivers/builtin/include'),str(root/'main/ota_signature.c'),str(root/'tests/ota_signature_test.c'),str(build/'library/libtfpsacrypto.a'),'-pthread','-o',str(binary)],check=True)
v=root/'tmp/m6-signature-vectors'
subprocess.run([str(binary)]+[str(v/n) for n in ['valid.bin','release.pub','wrong.pub','test-private.key']],check=True)

# HTTP adapter regressions run in scripts/run_tests.py without signing dependencies.
startup=root/'tmp/host-tests/discovery_startup'
subprocess.run(cc+['-std=gnu11','-I'+str(root/'tests/ota_stubs'),'-I'+str(root/'main'),str(root/'main/neighbors.c'),str(root/'tests/discovery_startup_test.c'),'-o',str(startup)],check=True)
subprocess.run([str(startup)],check=True)
