#!/usr/bin/env python3
"""Offline app signing: emits an ESP32 app .bin followed by a 128-byte OTA trailer.
Requires Python cryptography. Never writes/embeds a private key in firmware.
"""
import argparse, hashlib, struct
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
MAGIC=b'SYNDO6\0\1'
TRAILER_SIZE=128

def signed_image(image, key):
    if len(image)<288 or image[0]!=0xe9 or image[12:14]!=b'\0\0' or image[32:36]!=bytes.fromhex('3254cdab'):
        raise ValueError('Expected a compiled ESP32 application image')
    if image[80:112].split(b'\0',1)[0]!=b'Syndetis':
        raise ValueError('Wrong application product name')
    if not isinstance(key,ec.EllipticCurvePrivateKey) or not isinstance(key.curve,ec.SECP256R1):
        raise ValueError('Use an ECDSA P-256 release-signing key')
    length=struct.pack('<I',len(image));digest=hashlib.sha256(image).digest()
    signature=key.sign(MAGIC+length+digest,ec.ECDSA(hashes.SHA256()))
    if len(signature)>80:raise ValueError('Signature does not fit trailer')
    return image+MAGIC+length+struct.pack('<I',len(signature))+digest+signature.ljust(80,b'\0')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path);p.add_argument('--key',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.output.resolve() in (args.image.resolve(),args.key.resolve()):p.error('Output must not overwrite source image or signing key')
    # Encrypted offline keys prompt via terminal, never command-line passphrases.
    pem=args.key.read_bytes();password=None
    if b'ENCRYPTED' in pem:
        from getpass import getpass
        password=getpass('Signing key passphrase: ').encode()
    key=serialization.load_pem_private_key(pem,password=password)
    result=signed_image(args.image.read_bytes(),key)
    with args.output.open('xb') as out:out.write(result)
    print(f'Signed release: {args.output} ({len(result)} bytes). Keep private key offline.')
if __name__=='__main__':main()
