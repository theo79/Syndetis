from pathlib import Path
import argparse,hashlib
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--manifest',default='SOURCE_SHA256SUMS.txt');args=parser.parse_args()
manifest=(root/args.manifest).resolve();count=0
for line in manifest.read_text(encoding='utf-8').splitlines():
    wanted,relative=line.split('  ',1);p=(manifest.parent/relative).resolve()
    if not p.is_relative_to(root):raise SystemExit('Unsafe manifest path')
    actual=hashlib.sha256(p.read_bytes()).hexdigest()
    if actual!=wanted:raise SystemExit('Checksum mismatch: '+relative)
    count+=1
print('PASS: '+str(count)+' SHA256 checksums ('+args.manifest+')')
