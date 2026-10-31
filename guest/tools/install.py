#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0-or-later
# Python 2.3 (Tiger). User-only, versioned install; never edits /System or ISO.
import os,sys,sha,stat,shutil,tempfile

def fail(message):raise RuntimeError(message)
def lexists(path):return os.path.exists(path) or os.path.islink(path)
def spawn(args):return os.spawnv(os.P_WAIT,args[0],args)
def xml(s):return s.replace('&','&amp;').replace('<','&lt;').replace('>','&gt;').replace('"','&quot;')
def safe(name):
    if not name or name.startswith('/') or '\\' in name or '\x00' in name or '..' in name.split('/') or '' in name.split('/'):fail('Unsafe payload path')
    return name
def digest(path):
    f=open(path,'rb');h=sha.new()
    try:
        while True:
            b=f.read(65536)
            if not b:break
            h.update(b)
    finally:f.close()
    return h.hexdigest()
def directory(path):
    if lexists(path):
        s=os.lstat(path)
        if not stat.S_ISDIR(s.st_mode) or s.st_uid!=os.getuid():fail('Expected an owned directory: '+path)
    else:os.mkdir(path,0700)
def records(root):
    path=os.path.join(root,'manifest.sha1');data=open(path,'rb').read()
    if len(data)>1024*1024:fail('Manifest too large')
    result=[];seen={}
    for line in data.splitlines():
        fields=line.split(' ',2)
        if len(fields)!=3:fail('Invalid manifest')
        checksum,mode,name=fields;safe(name)
        if len(checksum)!=40 or checksum.strip('0123456789abcdef') or mode not in ('0644','0755') or name in seen:fail('Invalid manifest entry')
        seen[name]=1;result.append((checksum,int(mode,8),name))
    if not result:fail('Empty payload')
    return sha.new(data).hexdigest()[:20],result
def verify(root,entries):
    for checksum,mode,name in entries:
        path=root
        for part in name.split('/'):
            path=os.path.join(path,part)
            if os.path.islink(path):fail('Symlink in payload: '+name)
        if not os.path.isfile(path) or digest(path)!=checksum:fail('Payload checksum mismatch: '+name)
def atomic_link(target,path):
    if lexists(path) and not os.path.islink(path):fail('Will not replace an existing file/application: '+path)
    temp=path+'.ptolemy-'+str(os.getpid())
    if lexists(temp):fail('Staging link exists: '+temp)
    os.symlink(target,temp)
    try:os.rename(temp,path)
    finally:
        if lexists(temp):os.unlink(temp)
def main():
    if os.getuid()==0:fail('Run as the logged-in Tiger user, without sudo')
    if sys.platform!='darwin':fail('This installer runs inside Tiger')
    media=os.path.dirname(os.path.abspath(__file__));payload=os.path.join(media,'payload')
    version,entries=records(media);verify(payload,entries)
    home=os.path.expanduser('~');library=os.path.join(home,'Library');support=os.path.join(library,'Application Support')
    directory(library);directory(support);root=os.path.join(support,'Ptolemy');directory(root)
    versions=os.path.join(root,'versions');directory(versions);dest=os.path.join(versions,version)
    applications=os.path.join(home,'Applications');directory(applications)
    link=os.path.join(applications,'Ptolemy Agent.app')
    expected=os.path.join(root,'current','Ptolemy Agent.app')
    if lexists(link) and (not os.path.islink(link) or os.readlink(link)!=expected):fail('Existing application will be preserved: '+link)
    agents=os.path.join(library,'LaunchAgents');directory(agents)
    label='org.ptolemy.experimental.guest-agent';plist=os.path.join(agents,label+'.plist')
    config='<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>Label</key><string>'+label+'</string><key>ProgramArguments</key><array><string>/usr/bin/open</string><string>'+xml(link)+'</string></array><key>RunAtLoad</key><true/></dict></plist>\n'
    if lexists(plist):
        if os.path.islink(plist) or open(plist,'rb').read()!=config:fail('Existing LaunchAgent differs; preserved')
    if lexists(dest):directory(dest);verify(dest,entries)
    else:
        stage=tempfile.mkdtemp(prefix='.install-',dir=versions)
        try:
            for checksum,mode,name in entries:
                out=os.path.join(stage,name);parent=os.path.dirname(out)
                if not os.path.isdir(parent):os.makedirs(parent,0700)
                shutil.copyfile(os.path.join(payload,name),out);os.chmod(out,mode)
            verify(stage,entries);shutil.copyfile(os.path.join(media,'manifest.sha1'),os.path.join(stage,'receipt.sha1'))
            os.rename(stage,dest);stage=None
        finally:
            if stage:shutil.rmtree(stage)
    atomic_link(os.path.join('versions',version),os.path.join(root,'current'))
    atomic_link(expected,link)
    if not lexists(plist):
        fd,tmp=tempfile.mkstemp(prefix='.ptolemy-',dir=agents);os.close(fd)
        try:
            f=open(tmp,'wb');f.write(config);f.close();os.chmod(tmp,0600);os.rename(tmp,plist)
        finally:
            if os.path.exists(tmp):os.unlink(tmp)
        spawn(['/bin/launchctl','load',plist])
    print 'Installed Ptolemy Guest Tools '+version
    print 'Current: '+os.path.join(root,'current')
    print 'Tools start ON; Stop and the command log remain visible in the window and menu bar.'
    print 'Older versions and command logs are preserved. No administrator password is required.'
    return spawn(['/usr/bin/open',link])
if __name__=='__main__':
    try:sys.exit(main())
    except SystemExit:raise  # Python 2.3: SystemExit is an Exception subclass
    except Exception,e:print >>sys.stderr,'Installation failed:',e;sys.exit(1)
