#!/usr/bin/env python3
"""Prepare a separate OTA identity NVS image OFFLINE. Never connects to hardware.
Private release-signing keys are not accepted. Output contains a device TLS key.
"""
import argparse, csv, os, subprocess, sys
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

def identity_material(cert_path,key_path,pub_path):
    cert_pem=cert_path.read_bytes();key_pem=key_path.read_bytes();pub_pem=pub_path.read_bytes()
    certificate=x509.load_pem_x509_certificate(cert_pem)
    key=serialization.load_pem_private_key(key_pem,password=None)
    release=serialization.load_pem_public_key(pub_pem)
    if not isinstance(key,ec.EllipticCurvePrivateKey) or not isinstance(key.curve,ec.SECP256R1):
        raise ValueError('Use a per-device P-256 TLS key')
    from cryptography.x509.oid import ExtendedKeyUsageOID
    from datetime import datetime, timezone
    if not certificate.not_valid_before_utc<=datetime.now(timezone.utc)<=certificate.not_valid_after_utc:
        raise ValueError('Device certificate is not currently valid')
    if ExtendedKeyUsageOID.SERVER_AUTH not in certificate.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value:
        raise ValueError('Device certificate requires serverAuth EKU')
    if certificate.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
        raise ValueError('Use a leaf device certificate, not a CA certificate')
    if not isinstance(release,ec.EllipticCurvePublicKey) or not isinstance(release.curve,ec.SECP256R1):
        raise ValueError('Release trust must be a P-256 PUBLIC key')
    def public(k):return k.public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)
    if public(certificate.public_key())!=public(key.public_key()):raise ValueError('Device TLS key does not match certificate')
    sans=certificate.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    import ipaddress
    if ipaddress.ip_address('192.168.4.1') not in sans.get_values_for_type(x509.IPAddress):
        raise ValueError('Device certificate needs IP SAN 192.168.4.1')
    if 'syndetis-ota.test' not in sans.get_values_for_type(x509.DNSName):
        raise ValueError('Device certificate needs exact DNS SAN syndetis-ota.test')
    for data,maximum in [(cert_pem,4096),(key_pem,2048),(pub_pem,1024)]:
        if b'\0' in data or len(data)+1>maximum:raise ValueError('PEM is oversized or contains NUL')
    return cert_pem,key_pem,pub_pem

def prepare(cert_path,key_path,pub_path,out,idf):
    cert_pem,key_pem,pub_pem=identity_material(cert_path,key_path,pub_path)
    generator=idf/'components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py'
    if not generator.is_file():raise ValueError('Use --idf-path pointing to ESP-IDF v6.1')
    out.mkdir(mode=0o700,parents=False,exist_ok=False)
    os.chmod(out,0o700)
    for name,data in [('cert',cert_pem),('key',key_pem),('release_pub',pub_pem)]:
        path=out/f'{name}.blob'
        with path.open('xb') as f:f.write(data+b'\0')
        os.chmod(path,0o600)
    config=out/'identity.csv'
    with config.open('x',newline='') as f:
        writer=csv.writer(f);writer.writerow(['key','type','encoding','value'])
        writer.writerow(['ota_identity','namespace','',''])
        for name in ['cert','key','release_pub']:writer.writerow([name,'file','binary',str((out/f'{name}.blob').resolve())])
    os.chmod(config,0o600)
    image=out/'ota-identity.bin'
    subprocess.run([sys.executable,str(generator),'generate',str(config),str(image),'0x20000'],check=True)
    os.chmod(image,0o600)
    return image

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cert',type=Path,required=True);p.add_argument('--tls-key',type=Path,required=True)
    p.add_argument('--release-public-key',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
    p.add_argument('--idf-path',type=Path,default=Path(os.environ['IDF_PATH']) if 'IDF_PATH' in os.environ else None)
    a=p.parse_args()
    if a.idf_path is None:p.error('Provide --idf-path or activate the pinned SDK environment')
    image=prepare(a.cert,a.tls_key,a.release_public_key,a.output_dir,a.idf_path)
    print(f'Prepared {image}. Contains a device private key: keep private; do not commit or share. No hardware was accessed.')
if __name__=='__main__':main()
