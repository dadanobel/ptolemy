#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0-or-later
# Python 2.3 (Tiger). User-only, versioned install; never edits /System or ISO.
import os, sys, sha, stat, shutil, tempfile, time, fcntl


def fail(message):
    raise RuntimeError(message)


def lexists(path):
    return os.path.exists(path) or os.path.islink(path)


def spawn(args):
    return os.spawnv(os.P_WAIT, args[0], args)


def xml(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


# A payload path stays relative and inside the payload.
def safe(name):
    if (not name or name.startswith('/') or '\\' in name or '\x00' in name or '..' in name.split('/')
            or '' in name.split('/')):
        fail('Unsafe payload path')
    return name


def digest(path):
    f = open(path, 'rb')
    h = sha.new()
    try:
        while True:
            b = f.read(65536)
            if not b:
                break
            h.update(b)
    finally:
        f.close()
    return h.hexdigest()


# An existing directory must be a real one owned by the user; a missing one is created private.
def directory(path):
    if lexists(path):
        s = os.lstat(path)
        if not stat.S_ISDIR(s.st_mode) or s.st_uid != os.getuid():
            fail('Expected an owned directory: ' + path)
    else:
        os.mkdir(path, 0700)


# The manifest's (checksum, mode, name) entries and the version: the start of the manifest's own SHA-1.
def records(root):
    path = os.path.join(root, 'manifest.sha1')
    data = open(path, 'rb').read()
    if len(data) > 1024 * 1024:
        fail('Manifest too large')
    result = []
    seen = {}
    for line in data.splitlines():
        fields = line.split(' ', 2)
        if len(fields) != 3:
            fail('Invalid manifest')
        checksum, mode, name = fields
        safe(name)
        if (len(checksum) != 40 or checksum.strip('0123456789abcdef') or mode not in ('0644', '0755')
                or name in seen):
            fail('Invalid manifest entry')
        seen[name] = 1
        result.append((checksum, int(mode, 8), name))
    if not result:
        fail('Empty payload')
    return sha.new(data).hexdigest()[:20], result


# Every entry is a regular file (no symlink on its path) with the manifest's checksum.
def verify(root, entries):
    for checksum, mode, name in entries:
        path = root
        for part in name.split('/'):
            path = os.path.join(path, part)
            if os.path.islink(path):
                fail('Symlink in payload: ' + name)
        if not os.path.isfile(path) or digest(path) != checksum:
            fail('Payload checksum mismatch: ' + name)


# Points path at target through a renamed temporary link; never replaces a real file.
def atomic_link(target, path):
    if lexists(path) and not os.path.islink(path):
        fail('Will not replace an existing file/application: ' + path)
    temp = path + '.ptolemy-' + str(os.getpid())
    if lexists(temp):
        fail('Staging link exists: ' + temp)
    os.symlink(target, temp)
    try:
        os.rename(temp, path)
    finally:
        if lexists(temp):
            os.unlink(temp)


# Antiochus and its demonstration rosettast: payload/Antiochus/NAME, linked from ~/Applications/Ptolemy Tools/Antiochus.
TOOLS = ('Antiochus.app', 'rosettast.app')
# Earlier Tools linked them in ~/Applications itself: (that link's name, the application that replaces it).
OLD = (('Antiochus.app', 'Antiochus.app'), ('Rosetta Stone.app', 'rosettast.app'))


# Ptolemy Video (guest/video, bbbunny): QuickTime finds the component through a link in ~/Library/QuickTime to the
# current version's copy, so a new version replaces it and removal takes it away.
VIDEO = 'Ptolemy Video.component'


# A link of ours: a symlink to versions/<20 hex digits>/tail in the Tools folder. Anything else is someone's own.
def ours(root, link, tail):
    if not os.path.islink(link):
        return False
    prefix = os.path.join(root, 'versions') + '/'
    target = os.readlink(link)
    rest = target[len(prefix):]
    return (target.startswith(prefix) and rest[20:21] == '/' and not rest[:20].strip('0123456789abcdef')
            and rest[21:] == tail)


# The (target, link) pairs this payload brings. An application it lacks keeps its link, at the version that has it.
def tool_links(root, payload, dest, folder):
    result = []
    for name in TOOLS:
        if not os.path.isdir(os.path.join(payload, 'Antiochus', name)):
            continue
        link = os.path.join(folder, name)
        if lexists(link) and not ours(root, link, 'Antiochus/' + name):
            fail('Existing application will be preserved: ' + link)
        result.append((os.path.join(dest, 'Antiochus', name), link))
    return result


def launch_agent(library, link):
    label = 'org.ptolemy.experimental.guest-agent'
    config = ('<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>Label</key><string>' + label
              + '</string><key>ProgramArguments</key><array><string>/usr/bin/open</string><string>' + xml(link)
              + '</string></array><key>RunAtLoad</key><true/></dict></plist>\n')
    return os.path.join(library, 'LaunchAgents', label + '.plist'), config


# The DrawSprocket shim (guest/dsp/ptdsp.c) stays in the installed version; Ptolemy Agent starts DrawSprocket games with
# it. Earlier builds put it into the session's environment instead - the library and an environment.plist naming it, in
# ~/.MacOSX - which does not work in Tiger (loginwindow drops DYLD_*) and breaks Leopard's session once the library is
# missing (u215). shim() is that pair, so that remove_shim() can take away what an earlier build wrote.
def shim(home):
    folder = os.path.join(home, '.MacOSX')
    library = os.path.join(folder, 'ptdsp.dylib')
    config = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" '
              '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict><key>DYLD_INSERT_LIBRARIES'
              '</key><string>' + xml(library) + '</string></dict></plist>\n')
    return folder, library, os.path.join(folder, 'environment.plist'), config


# Writes path through a renamed temporary file in its folder; data None: a copy of source.
def put(path, data, source):
    fd, tmp = tempfile.mkstemp(prefix='.ptolemy-', dir=os.path.dirname(path))
    os.close(fd)
    try:
        if data is None:
            shutil.copyfile(source, tmp)
        else:
            f = open(tmp, 'wb')
            f.write(data)
            f.close()
        os.chmod(tmp, 0644)
        os.rename(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


# The environment first, then the library - which stays when an environment.plist that is not ours names it.
def remove_shim(home):
    folder, library, plist, config = shim(home)
    named = 0
    if lexists(plist):
        if not os.path.islink(plist) and open(plist, 'rb').read() == config:
            os.unlink(plist)
        else:
            named = os.path.islink(plist) or open(plist, 'rb').read().find('ptdsp.dylib') >= 0
    if named:
        print 'Kept, because ' + plist + ' names it: ' + library
    elif os.path.isfile(library) and not os.path.islink(library):
        os.unlink(library)
    off = os.path.join(folder, 'ptdsp.off')
    if os.path.isfile(off) and not os.path.islink(off):
        os.unlink(off)
    try:
        os.rmdir(folder)  # only when nothing else is in it
    except OSError:
        pass


# A running agent of another version holds the worker lock: the new one would start beside it and stay OFF (t877).
# Ask it to quit (it stops its worker first) and wait until the lock is free. Not when it is this version already.
def stop_other_agent(root, app):
    mine = app and os.path.realpath(app)  # None: stop any agent
    other = 0
    for line in os.popen('/bin/ps -axwwo command').read().splitlines():
        at = line.find('/Contents/MacOS/PtolemyAgent')
        rest = line[at + 28:]  # after the executable: nothing, or LaunchServices' -psn_ argument
        if at > 0 and (not rest or rest.startswith(' ')) and os.path.realpath(line[:at]) != mine:
            other = 1
    if not other:
        return
    try:
        os.setsid()  # the installer may run under that agent: leave its process group, which it stops
    except OSError:
        pass
    spawn(['/usr/bin/osascript', '-e', 'tell application "Ptolemy Agent" to quit'])
    lock = os.path.join(root, 'agent.lock')
    for i in range(80):
        fd = os.open(lock, os.O_RDWR | os.O_CREAT, 0600)
        try:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(fd, fcntl.LOCK_UN)
                return
            except IOError:
                pass
        finally:
            os.close(fd)
        time.sleep(0.25)
    fail('The running Ptolemy Agent did not quit; quit it from its menu and run the installer again')


# install.py remove: takes away what the installer made and nothing else - the agent's start at login, the links that
# are ours, the installed versions. Someone else's application or link in the same places stays; so do the command logs.
def remove():
    home = os.path.expanduser('~')
    library = os.path.join(home, 'Library')
    root = os.path.join(library, 'Application Support', 'Ptolemy')
    applications = os.path.join(home, 'Applications')
    folder = os.path.join(applications, 'Ptolemy Tools', 'Antiochus')
    link = os.path.join(applications, 'Ptolemy Agent.app')
    if os.path.isdir(root):
        stop_other_agent(root, None)
    remove_shim(home)
    video = os.path.join(library, 'QuickTime', VIDEO)
    if os.path.islink(video) and os.readlink(video) == os.path.join(root, 'current', VIDEO):
        os.unlink(video)
    plist, config = launch_agent(library, link)
    if lexists(plist):
        if os.path.islink(plist) or open(plist, 'rb').read() != config:
            print 'A different LaunchAgent is preserved: ' + plist
        else:
            spawn(['/bin/launchctl', 'unload', plist])
            os.unlink(plist)
    if os.path.islink(link) and os.readlink(link) == os.path.join(root, 'current', 'Ptolemy Agent.app'):
        os.unlink(link)
    for name in TOOLS:
        if ours(root, os.path.join(folder, name), 'Antiochus/' + name):
            os.unlink(os.path.join(folder, name))
    for name, new in OLD:
        if ours(root, os.path.join(applications, name), name):
            os.unlink(os.path.join(applications, name))
    current = os.path.join(root, 'current')
    if os.path.islink(current):
        os.unlink(current)
    versions = os.path.join(root, 'versions')
    if os.path.isdir(versions) and not os.path.islink(versions):
        for name in os.listdir(versions):
            path = os.path.join(versions, name)
            if len(name) == 20 and not name.strip('0123456789abcdef') and os.path.isdir(path) and not os.path.islink(path):
                shutil.rmtree(path)
    lock = os.path.join(root, 'agent.lock')
    if os.path.isfile(lock) and not os.path.islink(lock):
        os.unlink(lock)
    for path in (folder, os.path.dirname(folder), versions, root):  # only when nothing else is in them
        try:
            os.rmdir(path)
        except OSError:
            pass
    print 'Removed Ptolemy Guest Tools.'
    if os.path.isdir(root):
        print 'Kept (command logs or files that are not the installer\'s): ' + root
    return 0


def main():
    if os.getuid() == 0:
        fail('Run as the logged-in Tiger user, without sudo')
    if sys.platform != 'darwin':
        fail('This installer runs inside Tiger')
    if sys.argv[1:] == ['remove']:
        return remove()
    media = os.path.dirname(os.path.abspath(__file__))
    payload = os.path.join(media, 'payload')
    version, entries = records(media)
    verify(payload, entries)
    home = os.path.expanduser('~')
    library = os.path.join(home, 'Library')
    support = os.path.join(library, 'Application Support')
    directory(library)
    directory(support)
    root = os.path.join(support, 'Ptolemy')
    directory(root)
    versions = os.path.join(root, 'versions')
    directory(versions)
    dest = os.path.join(versions, version)
    applications = os.path.join(home, 'Applications')
    directory(applications)
    folder = os.path.join(applications, 'Ptolemy Tools', 'Antiochus')
    tools = tool_links(root, payload, dest, folder)
    video = os.path.join(library, 'QuickTime', VIDEO)
    video_target = os.path.join(root, 'current', VIDEO)
    with_video = os.path.isdir(os.path.join(payload, VIDEO))
    if with_video and lexists(video) and not (os.path.islink(video) and os.readlink(video) == video_target):
        fail('Existing QuickTime component will be preserved: ' + video)
    if tools:
        directory(os.path.dirname(folder))
        directory(folder)
    link = os.path.join(applications, 'Ptolemy Agent.app')
    expected = os.path.join(root, 'current', 'Ptolemy Agent.app')
    if lexists(link) and (not os.path.islink(link) or os.readlink(link) != expected):
        fail('Existing application will be preserved: ' + link)
    agents = os.path.join(library, 'LaunchAgents')
    directory(agents)
    plist, config = launch_agent(library, link)
    if lexists(plist):
        if os.path.islink(plist) or open(plist, 'rb').read() != config:
            fail('Existing LaunchAgent differs; preserved')
    # A version is staged in a temporary folder and renamed into place only once all of it verifies.
    if lexists(dest):
        directory(dest)
        verify(dest, entries)
    else:
        stage = tempfile.mkdtemp(prefix='.install-', dir=versions)
        try:
            for checksum, mode, name in entries:
                out = os.path.join(stage, name)
                parent = os.path.dirname(out)
                if not os.path.isdir(parent):
                    os.makedirs(parent, 0700)
                shutil.copyfile(os.path.join(payload, name), out)
                os.chmod(out, mode)
            verify(stage, entries)
            shutil.copyfile(os.path.join(media, 'manifest.sha1'), os.path.join(stage, 'receipt.sha1'))
            os.rename(stage, dest)
            stage = None
        finally:
            if stage:
                shutil.rmtree(stage)
    stop_other_agent(root, os.path.join(dest, 'Ptolemy Agent.app'))  # before current moves: an old agent still resolves to its own version
    atomic_link(os.path.join('versions', version), os.path.join(root, 'current'))
    atomic_link(expected, link)
    for target, path in tools:
        atomic_link(target, path)
    if with_video:
        directory(os.path.dirname(video))
        atomic_link(video_target, video)
    for name, new in OLD:  # the links of earlier Tools, once the application is in its new place
        if ours(root, os.path.join(applications, name), name) and lexists(os.path.join(folder, new)):
            os.unlink(os.path.join(applications, name))
    remove_shim(home)  # an earlier build's environment.plist and library; someone else's stay
    if os.path.isfile(os.path.join(dest, 'ptdsp.dylib')):
        print 'DrawSprocket games (Cro-Mag Rally): start them from the Ptolemy menu, DrawSprocket Games.'
    if not lexists(plist):
        fd, tmp = tempfile.mkstemp(prefix='.ptolemy-', dir=agents)
        os.close(fd)
        try:
            f = open(tmp, 'wb')
            f.write(config)
            f.close()
            os.chmod(tmp, 0600)
            os.rename(tmp, plist)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)
        spawn(['/bin/launchctl', 'load', plist])
    print 'Installed Ptolemy Guest Tools ' + version
    print 'Current: ' + os.path.join(root, 'current')
    print 'Tools start ON; Stop and the command log remain visible in the window and menu bar.'
    print 'Older versions and command logs are preserved. No administrator password is required.'
    return spawn(['/usr/bin/open', link])


if __name__ == '__main__':
    try:
        sys.exit(main())
    except SystemExit:
        raise  # Python 2.3: SystemExit is an Exception subclass
    except Exception, e:
        print >>sys.stderr, 'Installation failed:', e
        sys.exit(1)
