#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One isolated Half-Life run; source images are never written.
--disk-mode clone: APFS clones of the Tiger image and hl.dmg, removed after success.
The Tiger image is build/tiger.sparsebundle (only what the guest uses), or --image.
--disk-mode overlay: read-only bases with in-memory overlays; the stand exports the
guest's writes to the HL volume as hl-out.dmg, which is read only after a confirmed export.
Each run records the stand's own disk writes (proc_pid_rusage), SSD writes, swap,
thermal pressure, power source, CPU time and the stand's own CPU energy; anything unavailable is null, never 0.
A validation run checks GPU behavior; measure FPS separately without validation.
--idle: boot to the Finder desktop and idle (no HL volume, no input): energy and wakeup baseline.
Every 5 s the run also records a series: time, own CPU energy, CPU time and wakeups.
"""
import argparse,ctypes,hashlib,importlib.util,json,os,re,shutil,struct,subprocess,sys,time
from pathlib import Path
kit=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser();p.add_argument('project',type=Path);p.add_argument('run',type=Path)
p.add_argument('--stats',action='store_true');p.add_argument('--fp',choices=['old','new'],default='old');p.add_argument('--vram',choices=['trapped','quiescent'],default='quiescent');p.add_argument('--cpu',choices=['750','7400','7447a'],default='750');p.add_argument('--window',action='store_true');p.add_argument('--validation',action='store_true');p.add_argument('--seconds',type=int,default=150);p.add_argument('--vram-mb',choices=['128','256'],default='128')
p.add_argument('--binary',type=Path,help='exact firmware binary; window runs get a private app bundle')
p.add_argument('--rom',type=Path,help='Open Firmware ROM (default: firmware/openbios/obj-ppc/openbios-qemu.elf, else PROJECT/build/rom/openbios-ppc-ptolemy-s6)')
p.add_argument('--script',type=Path,help='input script instead of tests/half-life*.txt (e.g. with extra shot lines)')
p.add_argument('--profile',action='store_true',help='explicit PT_PROF; disable for FPS acceptance')
p.add_argument('--idle',action='store_true',help='Finder desktop only: energy and wakeup baseline')
p.add_argument('--disk-mode',choices=['clone','overlay'],default='overlay',help='clone: control mode, and for binaries without --overlay/--usb-save')
# Another base image (always an overlay), the guest agent enabled by an input script, host audio
p.add_argument('--image',type=Path,help='base Tiger image instead of the default (raw or sparse bundle; for A/B of the disk format)')
p.add_argument('--agent',type=Path,help='--idle: input script that enables the guest agent; adds a private --agent-socket')
p.add_argument('--audio',action='store_true',help='host audio output (--audio)')
a=p.parse_args();project=a.project.resolve(strict=True);run=a.run.absolute()
if not 30<=a.seconds<=3600:p.error('--seconds must be 30..3600')
source_exe=a.binary.resolve(strict=True) if a.binary else kit/'build/ptolemy-engine'
if not source_exe.is_file() or not os.access(source_exe,os.X_OK):p.error('--binary must be an executable file')
libc=ctypes.CDLL(None,use_errno=True)
def cmd(*c):
 try:return subprocess.run(c,capture_output=True,text=True,timeout=20).stdout
 except (OSError,subprocess.SubprocessError):return ''
def ssd():  # bytes written to the internal SSD since boot (IOKit); information only
 for c in cmd('ioreg','-r','-c','IOBlockStorageDriver','-l','-w0').split('+-o IOBlockStorageDriver')[1:]:
  n=re.search(r'"BSD Name" = "(disk\d+)"',c);w=re.search(r'"Bytes \(Write\)"=(\d+)',c)
  if n and w and n.group(1)=='disk0':return int(w.group(1))
def thermal():  # 0 nominal, 1 moderate, 2 heavy, 3 trapping, 4 sleeping
 m=re.search(r'(\d+)\s*$',cmd('notifyutil','-g','com.apple.system.thermalpressurelevel'));return int(m.group(1)) if m else None
def swapouts():
 m=re.search(r'Swapouts:\s+(\d+)',cmd('vm_stat'));return int(m.group(1)) if m else None
def exited(pid):  # waitid(P_PID, WEXITED|WNOWAIT|WNOHANG): exited but not yet reaped
 si=ctypes.create_string_buffer(128)
 while libc.waitid(1,pid,si,0x25):
  if ctypes.get_errno()!=4:raise OSError(ctypes.get_errno(),'waitid')
 return struct.unpack_from('<i',si.raw,12)[0]==pid
def rusage(pid):  # RUSAGE_INFO_V4 of the exited, unreaped stand; None if unavailable
 b=ctypes.create_string_buffer(296)
 if libc.proc_pid_rusage(pid,4,b):return None
 q=lambda o:struct.unpack_from('<Q',b.raw,o)[0];t=(ctypes.c_uint32*2)();libc.mach_timebase_info(t);tick=t[0]/t[1]
 return {'logical_writes':q(232),'diskio_written':q(152),'diskio_read':q(144),  # CPU time and wakeups for thermal/efficiency comparisons
  'user_s':round(q(16)*tick/1e9,3),'system_s':round(q(24)*tick/1e9,3),'wall_s':round((q(88)-q(80))*tick/1e9,1),'pkg_idle_wkups':q(32),'interrupt_wkups':q(40),'instructions':q(248),'cycles':q(256)}
def power(pid):  # TASK_POWER_INFO_V2 via a name port, arm64 layout checked against the SDK: own CPU energy (nJ); None if unavailable. Its GPU-time field stays 0 for Metal work, so it is not recorded
 me=ctypes.c_uint32.in_dll(libc,'mach_task_self_');port=ctypes.c_uint32();b=(ctypes.c_uint32*26)();n=ctypes.c_uint32(26)
 if libc.task_name_for_pid(me,pid,ctypes.byref(port)):return None  # fails once the stand is a zombie
 kr=libc.task_info(port,26,b,ctypes.byref(n));libc.mach_port_deallocate(me,port)
 if kr:return None
 q=lambda i:b[i]|b[i+1]<<32
 return {'energy_j':round(q(20)/1e9,2)}
delta=lambda x,y:None if x is None or y is None else y-x
run.mkdir(parents=True,exist_ok=False);(run/'frames').mkdir()
base=project/'build/tiger.sparsebundle'
if a.image:base=a.image.resolve(strict=True)
if a.disk_mode=='clone':
 subprocess.run(['cp','-c','-R',str(base),str(run/base.name)],check=True)
 subprocess.run([sys.executable,str(kit/'tools/prepare-bench-volume.py'),str(project/'build/bench/hl.dmg'),str(run/'hl.dmg')],check=True)
 results=run/'hl.dmg';disks=['--disk',str(run/base.name),'--usb-disk',str(results)]
else:  # one prepared HL volume per script/settings version, reused read-only by every run
 key=hashlib.sha256((kit/'tools/bench-native.command').read_bytes()+(kit/'tools/prepare-bench-volume.py').read_bytes()+json.dumps([os.environ.get('PT_BENCH_RES'),os.environ.get('PT_BENCH_MSAA')]).encode()).hexdigest()[:12]
 hl=project/'build/bench'/f'hl-{key}.dmg'
 if not hl.exists():subprocess.run([sys.executable,str(kit/'tools/prepare-bench-volume.py'),str(project/'build/bench/hl.dmg'),str(hl)],check=True)
 results=run/'hl-out.dmg';disks=['--overlay','--disk',str(base),'--usb-disk',str(hl),'--usb-save',str(results)]
if a.idle:disks=['--overlay','--disk',str(base)]  # no HL volume
exe=source_exe
if a.window:
 app=run/'PtolemyRadeonTest.app'
 subprocess.run([str(kit/'tools/make-test-app.sh'),str(source_exe),str(app)],cwd=kit,check=True)
 exe=app/'Contents/MacOS/PtolemyRadeonTest'
built=kit/'firmware/openbios/obj-ppc/openbios-qemu.elf'
rom=a.rom or (built if built.is_file() else project/'build/rom/openbios-ppc-ptolemy-s6')   # its 1.25 GiB PCI window also fits --vram-mb 256
args=[str(exe),'--rom',str(rom),'--radeon-fast' if a.vram=='quiescent' else '--radeon','--cpu',a.cpu,*(['--vram-mb',a.vram_mb] if a.vram_mb!='128' else []),
 *disks,'--of','auto-boot?=true','--of','boot-args=-v',
 '--input-script',(str(a.agent.resolve(strict=True)) if a.agent else os.devnull) if a.idle else str(a.script or kit/'tests'/('half-life.txt' if a.cpu=='750' else 'half-life-g4.txt')),'--shots',str(run/'frames'),'300','--seconds',str(a.seconds)]
if a.agent:(run/'agent').mkdir(mode=0o700);args+=['--agent-socket',str(run/'agent'/'control')]
if a.audio:args+=['--audio']
if a.window:args+=['--window']
env=os.environ.copy();env.pop('PT_FP_OLD',None);
if a.fp=='old':env['PT_FP_OLD']='1'
env.pop('PT_STATS',None);
if a.stats:env['PT_STATS']='1'  # JIT exit snapshots every 5 s (slows the run)
env.pop('PT_PROF',None)
if a.profile:env['PT_PROF']='1'
env.pop('MTL_DEBUG_LAYER',None)
env.pop('MTL_SHADER_VALIDATION',None)
if a.validation:env['MTL_DEBUG_LAYER']=env['MTL_SHADER_VALIDATION']='1'
result={'cpu':a.cpu,'vram_mb':int(a.vram_mb),'window':a.window,'metal_validation':a.validation,'fp':a.fp,'stats':a.stats,'PT_VMX_NEON':env.get('PT_VMX_NEON'),'PT_AA_GPU':env.get('PT_AA_GPU'),'PT_BENCH_RES':env.get('PT_BENCH_RES'),'vram':a.vram,'PT_FP_OLD':env.get('PT_FP_OLD'),'command':args,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
keys=['PT_FP_OLD','PT_FMA_FAST','PT_VMX_NEON','PT_VMX_FP','PT_VMX_MEM','PT_VMX_VSCR',
 'PT_FPR_CACHE','PT_FP_HOIST','PT_FP_LAZY','PT_FP_ROUND_FAST','PT_FMA_DIRECT_S','PT_CR_FUSE','PT_VMX_INT2',
 'PT_AA_GPU','PT_BENCH_RES','PT_BENCH_MSAA','PT_EDID_MODE','PT_GL_P0','PT_MAXBLK','PT_CHECK','PT_STATS','PT_PROF',
 'MTL_DEBUG_LAYER','MTL_SHADER_VALIDATION','PT_GPU_STATS','PT_GPU_CP_WAKE','PT_GPU_PM4_DRAIN','PT_OVERLAY_MAX_MB']
keys=sorted(set(keys+['PT_FP_FR','PT_FP_MIXED','PT_FP_MORE','PT_VMX_FP2','PT_VMX_MHADD',
 'OPTICS_DIRECT_VRAM','OPTICS_STRICT','OPTICS_EARLY_FENCE','OPTICS_FENCE_BLOCK','OPTICS_FENCE_WAIT',
 'OPTICS_COMMIT_LAZY','OPTICS_COMMIT_DRAWS','OPTICS_BIND_CACHE']) |
 {k for k in env if k.startswith(('PT_','OPTICS_','MTL_')) and not re.search(r'TOKEN|SECRET|PASSWORD|API_KEY',k)})
result.update(image=str(a.image) if a.image else None,agent=str(a.agent) if a.agent else None,audio=a.audio)
result.update(profile=a.profile,environment={k:env.get(k) for k in keys},disk_mode=a.disk_mode,rom=str(rom),rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
 source_binary=str(source_exe),source_binary_sha256=hashlib.sha256(source_exe.read_bytes()).hexdigest())
if a.disk_mode=='overlay':result['hl_variant']=hl.name
o=cmd('pmset','-g','batt');result['power']='AC' if 'AC Power' in o else 'battery' if 'Battery Power' in o else None
t0=time.monotonic()  # start every run cool; a fanless Air throttles under long series
while thermal() not in (0,None) and time.monotonic()-t0<300:time.sleep(5)
wait_s=round(time.monotonic()-t0);before=(ssd(),swapouts(),thermal());hot=[];timed_out=False;last=None;series=[]  # last own-energy sample, at most 0.5 s before exit
with (run/'console.txt').open('wb') as log:
 proc=subprocess.Popen(args,cwd=kit,env=env,stdout=log,stderr=subprocess.STDOUT);start=time.monotonic()
 try:
  deadline=time.monotonic()+a.seconds+120;next_t=0
  while not exited(proc.pid):
   now=time.monotonic()
   if now>=next_t:
    hot.append(thermal());next_t=now+5;u=rusage(proc.pid)  # live process: wall_s is meaningless here
    series.append([round(now-start,1),last and last['energy_j'],u and round(u['user_s']+u['system_s'],2),u and u['pkg_idle_wkups'],u and u['interrupt_wkups']])
   last=power(proc.pid) or last
   if now>deadline and not timed_out:proc.kill();timed_out=True
   time.sleep(0.5)
  result['process_io']=rusage(proc.pid);result['idle']=a.idle
  result['series']={'columns':['t_s','energy_j','cpu_s','pkg_idle_wkups','interrupt_wkups'],'rows':series}
  if result['process_io']:result['process_io'].update(last or {'energy_j':None})
 except BaseException:
  proc.kill();raise
 finally:proc.wait()
result['returncode']=-1 if timed_out else proc.returncode
if timed_out:result['error']='process exceeded stop deadline'
after=(ssd(),swapouts(),thermal());temps=[t for t in [before[2],*hot,after[2]] if t is not None]
written=delta(before[0],after[0])
result.update(ssd_disk0_written_mb=None if written is None else round(written/1e6,1),swapouts=delta(before[1],after[1]),
 thermal={'wait_s':wait_s,'before':before[2],'max':max(temps) if temps else None,'after':after[2]})
if a.disk_mode=='overlay':result['overlay']=re.findall(r'^PT_DISK .*$',(run/'console.txt').read_text(errors='replace'),re.M)
result['config']=re.findall(r'^ptolemy config: (.*)$',(run/'console.txt').read_text(errors='replace'),re.M)  # printed by the process itself
if result['returncode']!=0:result.setdefault('error','stand exited unsuccessfully')
elif a.idle:pass  # no HL results to read
elif a.disk_mode=='overlay' and not (results.exists() and any(l.startswith(f'PT_DISK {hl.name} ') and 'saved_mb=' in l for l in result['overlay'])):
 result['error']='overlay export not confirmed'
else:
 spec=importlib.util.spec_from_file_location('pt_hfs',kit/'tools/hfs-read.py');hfs=importlib.util.module_from_spec(spec);spec.loader.exec_module(hfs);h=None
 try:
  h=hfs.HFS(results)
  for name in ['native-fps.txt','native-display.txt','native-status.txt','native-game.log']:
   (run/name).write_bytes(h.file('/'+name))
  text=(run/'native-fps.txt').read_text();result['fps']=[float(x) for x in re.findall(r'([0-9.]+) fps',text)]
  if len(result['fps'])!=3:result['error']='expected exactly three timerefresh results'
  result['quartz_extreme']='Quartz Extreme: Supported' in (run/'native-display.txt').read_text()
  if not result['quartz_extreme']:result['error']='Quartz Extreme not reported as supported'
 except (OSError,ValueError,KeyError) as e:result['error']=str(e)
 finally:
  if h:h.close()
(run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False))
if not result.get('error') and not os.environ.get('PT_KEEP_CLONES'):  # clones and the export are kept only to diagnose failures
 for n in (base.name,'hl.dmg','hl-out.dmg'):
  shutil.rmtree(run/n) if (run/n).is_dir() else (run/n).unlink(missing_ok=True)
if result.get('error'):raise SystemExit(1)
