#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only fixture checks; no guest runs or user disks. Retain all fixtures."""
import json,pathlib,subprocess,sys,os,hashlib,zipfile,stat
kit=pathlib.Path(__file__).resolve().parent.parent
manager=kit/'build/host/ptolemy-manager';root=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else kit/'reports/host-proof';root.mkdir(exist_ok=False)
def call(*args,success=True):
    result=subprocess.run([str(manager),*map(str,args)],capture_output=True,text=True)
    if success and result.returncode:raise RuntimeError(result.stderr)
    if not success:assert result.returncode,result.stdout
    return result.stdout
source=root/'zip-source';source.mkdir();payload=source/'payload';payload.write_bytes(bytes(range(256))*512)
fork=b'PTOLEMY-RESOURCE-FORK';subprocess.run(['/usr/bin/xattr','-w','-x','com.apple.ResourceFork',fork.hex(),str(payload)],check=True)
archive=root/'payload.zip';subprocess.run(['/usr/bin/ditto','-c','-k','--sequesterRsrc','--keepParent',str(source),str(archive)],check=True)
prepared=json.loads(call('prepare',archive,root/'prepared','storage'))
try:
    staged=list((root/'prepared').glob('zip-*/contents/zip-source/payload'))
    assert len(staged)==1 and staged[0].read_bytes()==payload.read_bytes()
    assert bytes.fromhex(subprocess.check_output(['/usr/bin/xattr','-p','-x','com.apple.ResourceFork',str(staged[0])]).decode())==fork
finally:call('detach',prepared['lease'])
# System bsdtar must reject a symlink traversal before any external write.
outside=root/'outside';outside.mkdir();bad=root/'bad.zip'
with zipfile.ZipFile(bad,'w') as z:
    link=zipfile.ZipInfo('link');link.create_system=3;link.external_attr=(stat.S_IFLNK|0o777)<<16
    z.writestr(link,str(outside));z.writestr('link/escape','must not be written')
call('prepare',bad,root/'bad-prepared','storage',success=False);assert not (outside/'escape').exists()
exchange=root/'exchange';call('exchange-create',exchange);call('exchange-in',exchange,payload)
export=root/'export';call('exchange-out',exchange,export)
files=list(export.glob('Drop-*/payload'));assert len(files)==1 and files[0].read_bytes()==payload.read_bytes()
assert bytes.fromhex(subprocess.check_output(['/usr/bin/xattr','-p','-x','com.apple.ResourceFork',str(files[0])]).decode())==fork
offer=json.loads(call('exchange-offer',exchange));assert offer['read_only'] is False
call('exchange-out',exchange,root/'blocked-export',success=False)
call('exchange-ack',exchange,'stale-token','detached',success=False)
call('exchange-ack',exchange,offer['token'],'attached');call('exchange-ack',exchange,offer['token'],'detached')
result={'zip_resource_fork_roundtrip':True,'zip_symlink_escape_rejected':True,'exchange_hfs_apm_raw_roundtrip':True,'exchange_resource_fork_roundtrip':True,'dual_mount_rejected':True,'stale_ack_rejected':True,'scope':'host only; no Tiger mount, USB guest, physical SuperDrive, GUI interaction or installer run','fixtures':str(root)}
(root/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
