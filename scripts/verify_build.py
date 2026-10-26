from pathlib import Path
import argparse,hashlib,json,re,struct
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser();p.add_argument('build',nargs='?',default='build');a=p.parse_args();build=(root/a.build).resolve()
meta=json.loads((build/'project_description.json').read_text())
assert meta['target']=='esp32' and meta['project_version']=='0.4.0'
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
assert expected==actual,'Effective Kconfig differs from the accepted configuration'
table=(build/'partition_table/partition-table.bin').read_bytes()
assert table==(root/'recovery/milestone5b/partition-table.bin').read_bytes(),'Partition layout changed'
parts=[]
for i in range(0,len(table),32):
    if struct.unpack_from('<H',table,i)[0]!=0x50aa:break
    _,kind,subtype,offset,size,label,_=struct.unpack_from('<HBBII16sI',table,i)
    parts.append((label.rstrip(b'\0').decode(),offset,size))
assert parts==[('nvs',0x9000,0x6000),('phy_init',0xf000,0x1000),('factory',0x10000,0x100000)]
app=build/'Syndetis.bin';size=app.stat().st_size;assert size<=1048576
report={'target':meta['target'],'application_version':meta['project_version'],'application_bytes':size,'free_bytes':1048576-size,'sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'effective_config_identical':True,'partition_identical':True}
(build/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
