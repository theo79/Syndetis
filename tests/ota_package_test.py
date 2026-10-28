"""Real offline P-256 signatures; temporary keys only, never production trust."""
import hashlib, importlib.util, struct
from pathlib import Path
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('sign_firmware',root/'scripts/sign_firmware.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
key=ec.generate_private_key(ec.SECP256R1())
image=bytearray(512);image[0]=0xe9;image[1]=1
image[28:32]=struct.pack('<I',256);image[48:54]=b'0.5.1\0';image[32:36]=bytes.fromhex('3254cdab');image[80:89]=b'Syndetis\0'
def verify(package,public):
    app,t=package[:-128],package[-128:]
    if t[:8]!=module.MAGIC or struct.unpack('<I',t[8:12])[0]!=len(app):return False
    if hashlib.sha256(app).digest()!=t[16:48]:return False
    length=struct.unpack('<I',t[12:16])[0]
    if not 8<=length<=80 or any(t[48+length:]):return False
    try:public.verify(t[48:48+length],t[:12]+t[16:48],ec.ECDSA(hashes.SHA256()))
    except (ValueError,InvalidSignature):return False
    return True
package=module.signed_image(bytes(image),key)
assert len(package)==len(image)+128 and verify(package,key.public_key())
for offset in [1,79,400,len(image),len(image)+8,len(image)+12,len(image)+16,len(image)+48]:
    bad=bytearray(package);bad[offset]^=0x80;assert not verify(bad,key.public_key())
assert not verify(package[:-1],key.public_key())
assert not verify(package,ec.generate_private_key(ec.SECP256R1()).public_key())
for modified in [b'not firmware',bytes(image[:12])+b'\x02'+bytes(image[13:]),bytes(image[:80])+b'Other\0'+bytes(image[86:])]:
    try:module.signed_image(modified,key)
    except ValueError:pass
    else:raise AssertionError('Signer accepted wrong target/product')
try:module.signed_image(bytes(image),ec.generate_private_key(ec.SECP384R1()))
except ValueError:pass
else:raise AssertionError('Signer accepted wrong curve')
print('PASS: real P-256 signed release format, wrong key, image/metadata/signature corruption, truncation and signer product/chip/curve rejection')

# Keep only ephemeral TEST material under ignored tmp/ for the SDK verifier test.
from cryptography.hazmat.primitives import serialization
out=root/'tmp/m6-signature-vectors';out.mkdir(exist_ok=True)
(out/'valid.bin').write_bytes(package)
(out/'release.pub').write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
(out/'wrong.pub').write_bytes(ec.generate_private_key(ec.SECP256R1()).public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
(out/'test-private.key').write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
(out/'test-private.key').chmod(0o600)
