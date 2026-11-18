# SPDX-License-Identifier: GPL-2.0-or-later
# Run with the guest Python and the patched Tools installer path.
import imp, os, shutil, sys, tempfile
m=imp.load_source('ant_installer',sys.argv[1])
t=tempfile.mkdtemp(prefix='ant-install-check-')
try:
    root=os.path.join(t,'Ptolemy');payload=os.path.join(t,'payload');apps=os.path.join(t,'Applications')
    for name in ('Antiochus.app', 'Rosetta Stone.app'):
        dest=os.path.join(root,'versions','0'*20);link=os.path.join(apps,name)
        os.makedirs(os.path.join(payload,name));
        if not os.path.isdir(apps):os.makedirs(apps)
        if not os.path.isdir(dest):os.makedirs(dest)
        os.mkdir(link);open(os.path.join(link,'owner'),'w').write('preserve')
        rejected=False
        try:m.optional_antiochus(root,payload,dest,apps,name)
        except Exception:rejected=True
        assert rejected and open(os.path.join(link,'owner')).read()=='preserve'
        print 'Foreign application preserved'
        shutil.rmtree(link);os.symlink('/tmp/unrelated-app',link);rejected=False
        try:m.optional_antiochus(root,payload,dest,apps,name)
        except Exception:rejected=True
        assert rejected and os.readlink(link)=='/tmp/unrelated-app'
        print 'Foreign symlink preserved'
        os.unlink(link);old=os.path.join(root,'versions','1'*20,name);os.symlink(old,link)
        assert m.optional_antiochus(root,payload,dest,apps,name)==(os.path.join(dest,name),link)
        shutil.rmtree(os.path.join(payload,name))
        assert m.optional_antiochus(root,payload,dest,apps,name) is None and os.readlink(link)==old
        print 'Tools update without Antiochus preserves its version link'
finally:shutil.rmtree(t)
