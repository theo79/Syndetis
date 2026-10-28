"""Verify the reviewed Milestone 6 config, complete partition map and image fit."""
from pathlib import Path
import argparse,hashlib,json,re,struct
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser();p.add_argument('build',nargs='?',default='build-m6');a=p.parse_args();build=(root/a.build).resolve()
meta=json.loads((build/'project_description.json').read_text())
version=re.search(r'set\(PROJECT_VER \"([^\"]+)\"\)',(root/'CMakeLists.txt').read_text()).group(1)
assert meta['target']=='esp32' and meta['project_version']==version
assert Path(meta['project_path']).resolve()==root
for source in meta['build_component_info']['main']['sources']:assert Path(source).resolve().is_relative_to(root)
def opts(path):
    result={}
    for line in path.read_text().splitlines():
        m=re.match(r'CONFIG_(\w+)=(.*)',line)
        if m:result[m[1]]=m[2]
        m=re.match(r'# CONFIG_(\w+) is not set',line)
        if m:result[m[1]]='n'
    return result
expected=opts(root/'sdkconfig.pinned');actual=opts(Path(meta['config_file']))
assert expected==actual,'Effective Kconfig differs from the reviewed configuration'
assert actual['BOOTLOADER_APP_ROLLBACK_ENABLE']=='y'
assert actual['ESPTOOLPY_FLASHSIZE']=='"4MB"'
assert actual['SECURE_BOOT']=='n' and actual['SECURE_FLASH_ENC_ENABLED']=='n'
table=(build/'partition_table/partition-table.bin').read_bytes();parts=[]
for i in range(0,len(table),32):
    if struct.unpack_from('<H',table,i)[0]!=0x50aa:break
    _,kind,subtype,offset,size,label,flags=struct.unpack_from('<HBBII16sI',table,i)
    parts.append((label.rstrip(b'\0').decode(),kind,subtype,offset,size,flags))
assert parts==[('nvs',1,2,0x9000,0x6000,0),('phy_init',1,1,0xf000,0x1000,0),
               ('otadata',1,0,0x10000,0x2000,0),('ota_0',0,0x10,0x20000,0x1e0000,0),
               ('ota_1',0,0x11,0x200000,0x1e0000,0),('ota_keys',1,2,0x3e0000,0x20000,0)]
for left,right in zip(parts,parts[1:]):assert left[3]+left[4]<=right[3]
assert parts[-1][3]+parts[-1][4]==0x400000
assert all(x[3]%0x10000==0 for x in parts if x[1]==0)
app=build/'Syndetis.bin';size=app.stat().st_size;assert size<=0x1e0000
assert app.read_bytes()[23]==1,"Raw application must carry an appended ESP image hash"
boot=(build/'bootloader/bootloader.bin').stat().st_size;assert boot<=0x8000-0x1000
assert (build/'ota_data_initial.bin').stat().st_size==0x2000
flash=json.loads((build/'flasher_args.json').read_text())['flash_files']
assert '0x20000' in flash and '0x10000' in flash
assert '0x3e0000' not in flash,'Per-device identity must never be in generic app flash args'
report={'target':meta['target'],'application_version':meta['project_version'],
        'application_bytes':size,'application_partition_bytes':0x1e0000,'free_bytes':0x1e0000-size,
        'upload_bytes':size,'credential_provisioning_required':False,'update_transport':'HTTP:8080','bootloader_bytes':boot,'bootloader_free_bytes':0x7000-boot,
        'sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'effective_config_identical':True,
        'partition_layout_verified':True,'rollback_enabled':True,'hardware_validated':False}
(build/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
