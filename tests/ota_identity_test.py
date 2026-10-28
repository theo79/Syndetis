"""Offline identity validation. --idf-path also exercises the real NVS generator."""
import argparse, importlib.util, ipaddress, tempfile
from pathlib import Path
from datetime import datetime,timedelta,timezone
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID,ExtendedKeyUsageOID
root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('prepare',root/'scripts/prepare_ota_identity.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
parser=argparse.ArgumentParser();parser.add_argument('--idf-path',type=Path);args=parser.parse_args()
key=ec.generate_private_key(ec.SECP256R1());release=ec.generate_private_key(ec.SECP256R1())
now=datetime.now(timezone.utc);name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'TEST ONLY Syndetis')])
def certificate(san=True,server=True,ca=False,expired=False,dns="syndetis-ota.test"):
    c=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(days=2)).not_valid_after(now-timedelta(days=1) if expired else now+timedelta(days=1))
    c=c.add_extension(x509.BasicConstraints(ca=ca,path_length=None),critical=True)
    c=c.add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('192.168.4.1' if san else '192.0.2.10'))]+([x509.DNSName(dns)] if dns else [])),critical=False)
    c=c.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH if server else ExtendedKeyUsageOID.CLIENT_AUTH]),critical=False)
    return c.sign(key,hashes.SHA256()).public_bytes(serialization.Encoding.PEM)
def private(k):return k.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption())
def public(k):return k.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo)
with tempfile.TemporaryDirectory(prefix='m6-identity-test-',dir=root/'tmp') as folder:
    d=Path(folder);cert=d/'cert.pem';tls=d/'tls.key';pub=d/'release.pub'
    cert.write_bytes(certificate());tls.write_bytes(private(key));tls.chmod(0o600);pub.write_bytes(public(release))
    assert module.identity_material(cert,tls,pub)
    for variant in [certificate(san=False),certificate(server=False),certificate(ca=True),certificate(expired=True),certificate(dns=None),certificate(dns="test"),certificate(dns="evil.test"),certificate(dns="*.test"),certificate(dns="syndetis-ota.test.evil.test")]:
        cert.write_bytes(variant)
        try:module.identity_material(cert,tls,pub)
        except ValueError:pass
        else:raise AssertionError('Accepted invalid TLS identity')
    cert.write_bytes(certificate());tls.write_bytes(private(release))
    try:module.identity_material(cert,tls,pub)
    except ValueError:pass
    else:raise AssertionError('Accepted mismatched TLS key')
    tls.write_bytes(private(key));pub.write_bytes(public(ec.generate_private_key(ec.SECP384R1())))
    try:module.identity_material(cert,tls,pub)
    except ValueError:pass
    else:raise AssertionError('Accepted wrong signing curve')
    pub.write_bytes(private(release))
    try:module.identity_material(cert,tls,pub)
    except ValueError:pass
    else:raise AssertionError('Accepted release private key')
    pub.write_bytes(public(release))
    if args.idf_path:
        result=module.prepare(cert,tls,pub,d/'identity',args.idf_path)
        assert result.stat().st_size==0x20000 and result.stat().st_mode&0o777==0o600
        assert (d/'identity'/'cert.blob').read_bytes().endswith(b'\0')
        print('PASS: real pinned-SDK offline NVS identity image, size and private file permissions; no hardware access')
print('PASS: TLS/public-trust provisioning validation: key match, SAN, lifetime, serverAuth, leaf certificate and P-256-only public trust')
