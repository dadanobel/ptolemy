// SPDX-License-Identifier: GPL-2.0-or-later

import CryptoKit
import Darwin
import DiskArbitration
import Foundation
import IOKit

struct InstallError: Error, CustomStringConvertible, LocalizedError {
    let description: String
    var errorDescription: String? { description }
    init(_ message: String) { description = message }
}
func check(_ value: Bool, _ message: String) throws { if !value { throw InstallError(message) } }
func plist(_ data: Data) throws -> [String: Any] {
    guard let p = try PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any] else {
        throw InstallError("Invalid property list")
    }
    return p
}
func plistData(_ value: [String: Any]) throws -> Data {
    try PropertyListSerialization.data(fromPropertyList: value, format: .xml, options: 0)
}
func regular(_ url: URL) throws {
    var s = stat()
    try check(
        lstat(url.path, &s) == 0 && s.st_mode & S_IFMT == S_IFREG, "Expected a regular file: \(url.lastPathComponent)")
}
func directory(_ url: URL) throws {
    var s = stat()
    try check(
        lstat(url.path, &s) == 0 && s.st_mode & S_IFMT == S_IFDIR, "Expected a directory, not a link: \(url.path)")
}
func checkedChild(_ root: URL, _ relative: String) throws -> URL {
    let parts = relative.split(separator: "/", omittingEmptySubsequences: false)
    try check(
        !parts.isEmpty && parts.allSatisfy { !$0.isEmpty && $0 != "." && $0 != ".." && !$0.contains("\0") },
        "Unsafe relative path")
    return root.appendingPathComponent(relative)
}
func sha1(_ data: Data) -> String { Insecure.SHA1.hash(data: data).map { String(format: "%02x", $0) }.joined() }
/// This run's log is session.log; the previous run's stays as session-previous.log, older ones are removed.
func openSessionLog(_ folder: URL) throws -> (URL, FileHandle) {
    let fm = FileManager.default
    let log = folder.appendingPathComponent("session.log")
    for name in (try? fm.contentsOfDirectory(atPath: folder.path)) ?? []
    where name.hasPrefix("session-") && name.hasSuffix(".log") {
        try? fm.removeItem(at: folder.appendingPathComponent(name))
    }
    try? fm.moveItem(at: log, to: folder.appendingPathComponent("session-previous.log"))
    fm.createFile(atPath: log.path, contents: nil, attributes: [.posixPermissions: 0o600])
    return (log, try FileHandle(forWritingTo: log))
}
func command(_ path: String, _ arguments: [String]) throws -> Data {
    let p = Process()
    let out = Pipe()
    let err = Pipe()
    p.executableURL = URL(fileURLWithPath: path)
    p.arguments = arguments
    p.standardInput = FileHandle.nullDevice
    p.standardOutput = out
    p.standardError = err
    // Drain both pipes concurrently: hdiutil can fill stderr while stdout is read.
    let group = DispatchGroup()
    let lock = NSLock()
    var errorData = Data()
    group.enter()
    DispatchQueue.global().async {
        let bytes = err.fileHandleForReading.readDataToEndOfFile()
        lock.lock()
        errorData = bytes
        lock.unlock()
        group.leave()
    }
    do { try p.run() } catch {
        err.fileHandleForWriting.closeFile()
        group.wait()
        throw error
    }
    out.fileHandleForWriting.closeFile()
    err.fileHandleForWriting.closeFile()
    let data = out.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    group.wait()
    try check(
        p.terminationReason == .exit && p.terminationStatus == 0,
        "\(URL(fileURLWithPath: path).lastPathComponent): \(String(decoding: errorData.prefix(8192), as: UTF8.self))")
    return data
}
final class ProductLock {
    private var fd: Int32 = -1
    init(_ folder: URL) throws {
        fd = open(folder.appendingPathComponent("session.lock").path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0o600)
        guard fd >= 0 else { throw InstallError("Cannot lock this installation") }
        if flock(fd, LOCK_EX | LOCK_NB) != 0 {
            close(fd)
            fd = -1
            throw InstallError("This installation is already in use")
        }
    }
    deinit { if fd >= 0 { close(fd) } }
}
struct InstallerIdentity: Codable {
    var version: String
    var build: String
    var system: String { version.hasPrefix("10.5") ? "Leopard" : "Tiger" }  // the machine's name for the system: words and look follow it
}
func ppc32Kernel(_ data: Data) -> Bool {
    func word(_ at: Int, _ little: Bool = false) -> UInt32 {
        guard at >= 0 && at + 4 <= data.count else { return 0 }
        return (0..<4).reduce(UInt32(0)) { v, i in v | UInt32(data[at + i]) << UInt32((little ? i : 3 - i) * 8) }
    }
    guard data.count >= 28 else { return false }
    if word(0) == 0xfeed_face { return word(4) == 18 }
    if word(0) == 0xcafe_babe {
        let n = Int(word(4))
        guard n > 0 && n <= 64 && 8 + n * 20 <= data.count else { return false }
        for i in 0..<n where word(8 + i * 20) == 18 {
            let offset = Int(word(16 + i * 20))
            let size = Int(word(20 + i * 20))
            if size >= 28 && offset >= 8 + n * 20 && offset <= data.count - 28 && size <= data.count - offset
                && word(offset) == 0xfeed_face && word(offset + 4) == 18
            {
                return true
            }
        }
    }
    return false
}
func validateSystemVolume(_ volume: URL, installer: Bool = true) throws -> InstallerIdentity {
    try directory(volume)
    let version = try plist(
        Data(contentsOf: volume.appendingPathComponent("System/Library/CoreServices/SystemVersion.plist")))
    guard let v = version["ProductVersion"] as? String,
        ["10.4", "10.5"].contains(where: { v == $0 || v.hasPrefix($0 + ".") })
    else { throw InstallError("Choose a Mac OS X Tiger (10.4) or Leopard (10.5) PowerPC installer") }
    let kernel = volume.appendingPathComponent("mach_kernel")
    try regular(kernel)
    let file = try FileHandle(forReadingFrom: kernel)
    defer { try? file.close() }
    let header = try file.read(upToCount: 4096) ?? Data()
    var powerPC = ppc32Kernel(header)
    if !powerPC && header.count >= 8 && header.prefix(4) == Data([0xca, 0xfe, 0xba, 0xbe]) {
        func be(_ at: Int) -> UInt32 { header[at..<at + 4].reduce(UInt32(0)) { $0 << 8 | UInt32($1) } }
        let count = Int(be(4))
        let length = try file.seekToEnd()
        if count > 0 && count <= 64 && header.count >= 8 + 20 * count {
            for i in 0..<count where be(8 + i * 20) == 18 {
                let offset = UInt64(be(16 + i * 20))
                let size = UInt64(be(20 + i * 20))
                if offset >= UInt64(8 + 20 * count) && size >= 28 && offset <= length && size <= length - offset {
                    try file.seek(toOffset: offset)
                    if ppc32Kernel(try file.read(upToCount: 28) ?? Data()) { powerPC = true }
                }
            }
        }
    }
    try check(powerPC, "The selected system has no 32-bit PowerPC kernel")
    if installer { try directory(volume.appendingPathComponent("System/Installation/Packages")) }
    return InstallerIdentity(version: v, build: version["ProductBuildVersion"] as? String ?? "")
}
// Image ownership lasts through the child process. Detach only the exact IOMedia
// object we attached; a recycled disk number is never an eject authority.
final class ImageMount {
    let device: String, volume: URL, registry: UInt64
    private let execute: (String, [String]) throws -> Data
    private let identity: (String) throws -> UInt64
    private var detached = false
    static func mediaID(_ device: String) throws -> UInt64 {
        guard let session = DASessionCreate(nil), let disk = DADiskCreateFromBSDName(nil, session, device) else {
            throw InstallError("Disk Arbitration is unavailable")
        }
        let media = DADiskCopyIOMedia(disk)
        try check(media != 0, "Image device disappeared")
        defer { IOObjectRelease(media) }
        var id: UInt64 = 0
        try check(IORegistryEntryGetRegistryEntryID(media, &id) == KERN_SUCCESS, "Cannot identify the image device")
        return id
    }
    static func requireDetached(_ image: URL, execute: (String, [String]) throws -> Data = command) throws {
        let info = try plist(execute("/usr/bin/hdiutil", ["info", "-plist"]))
        let path = image.resolvingSymlinksInPath().standardizedFileURL.path
        let mounted = (info["images"] as? [[String: Any]] ?? []).contains { entry in
            guard let name = entry["image-path"] as? String else { return false }
            return URL(fileURLWithPath: name).resolvingSymlinksInPath().standardizedFileURL.path == path
        }
        try check(!mounted, "Eject this image in Finder before Ptolemy uses it")
    }
    /// shadow: hdiutil keeps every write in that file; the image file itself stays unchanged.
    init(
        image: URL, readOnly: Bool, shadow: URL? = nil, execute: @escaping (String, [String]) throws -> Data = command,
        identity: @escaping (String) throws -> UInt64 = ImageMount.mediaID
    ) throws {
        self.execute = execute
        self.identity = identity
        try Self.requireDetached(image, execute: execute)
        var a = ["attach", "-plist", "-nobrowse", "-noautoopen", "-owners", "off"]
        if readOnly { a.append("-readonly") }
        if let shadow { a += ["-shadow", shadow.path] }
        a.append(image.path)
        let result = try plist(execute("/usr/bin/hdiutil", a))
        let entities = result["system-entities"] as? [[String: Any]] ?? []
        guard
            let dev = entities.compactMap({ $0["dev-entry"] as? String }).first(where: {
                $0.range(of: "^/dev/disk[0-9]+$", options: .regularExpression) != nil
            })
        else { throw InstallError("Unknown image device response; inspect attached images before recovery") }
        device = dev
        let id = try identity(dev)
        registry = id
        func detach() { if (try? identity(dev)) == id { _ = try? execute("/usr/bin/hdiutil", ["detach", dev]) } }
        let volumes = entities.compactMap { $0["mount-point"] as? String }
        guard volumes.count == 1 else {
            detach()
            throw InstallError("Expected exactly one Mac volume")
        }
        volume = URL(fileURLWithPath: volumes[0])
        if volume.path == "/" {
            detach()
            throw InstallError("Refusing the host system volume")
        }
    }
    deinit { try? close() }
    func close() throws {
        if detached { return }
        try check(try identity(device) == registry, "Image device was replaced; automatic eject refused")
        _ = try execute("/usr/bin/hdiutil", ["detach", device])
        detached = true
    }
}
/// Leopard's install DVD has no /var/db/.AppleSetupDone, so its kextd treats the session as a first boot and serves
/// volume locks only 300 s after it starts. A Ptolemy install can reach "Finalize disk" sooner: the installer's kextcache
/// then gets EAGAIN and the install ends with "could not make the computer start up from Macintosh HD". The guest reads
/// the DVD through a shadow file that adds that empty marker, so kextd waits its usual 60 s; the image is never written.
func openInstaller(_ item: Installation, shadow: URL) throws -> ImageMount {
    let image = URL(fileURLWithPath: item.installer)
    try? FileManager.default.removeItem(at: shadow)
    guard item.installerIdentity.system == "Leopard" else { return try ImageMount(image: image, readOnly: true) }
    let writable = try ImageMount(image: image, readOnly: false, shadow: shadow)
    let made = FileManager.default.createFile(
        atPath: writable.volume.appendingPathComponent("private/var/db/.AppleSetupDone").path, contents: nil)
    try writable.close()
    try check(made, "Cannot prepare the Leopard installer")
    return try ImageMount(image: image, readOnly: true, shadow: shadow)
}
enum InstallPhase: String, Codable { case installer, toolsPending, ready }
let cpus = [(id: "750", name: "G3 750"), (id: "7400", name: "G4 7400"), (id: "7447a", name: "G4 7447A")]  // a machine's processors: --cpu value, name
struct Installation: Codable {
    var schema = 1, id = UUID(), name = "Tiger", cpu = "7447a", ram = 2048, diskGiB = 40, phase = InstallPhase.installer
    var installer: String, installerIdentity: InstallerIdentity
    func validate() throws {
        try check(
            schema == 1 && !name.isEmpty && name.utf8.count <= 80 && !name.contains("\0"), "Invalid installation record"
        )
        try check(
            cpus.contains(where: { $0.id == cpu }) && (512...2048).contains(ram) && ram % 64 == 0,
            "Invalid CPU or memory settings")
        try check(cpu != "750" || installerIdentity.system != "Leopard", "Leopard needs a G4 processor")
        try check((8...120).contains(diskGiB), "Disk size must be 8–120 GiB (this board uses LBA28)")
    }
    static func load(_ folder: URL) throws -> Installation {
        let r = try JSONDecoder().decode(
            Self.self, from: Data(contentsOf: folder.appendingPathComponent("installation.json")))
        try r.validate()
        return r
    }
    func save(_ folder: URL) throws {
        try validate()
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        try e.encode(self).write(to: folder.appendingPathComponent("installation.json"), options: .atomic)
    }
    var diskArguments: [String] {
        [
            "create", "-size", "\(diskGiB)g", "-type", "SPARSEBUNDLE", "-fs", "HFS+J", "-layout", "SPUD", "-volname",
            "Macintosh HD", "-imagekey", "sparse-band-size=2048",
        ]
    }
}
struct ProductResources {
    let engine: URL, rom: URL, tools: URL
    init(root: URL) {
        engine = root.appendingPathComponent("ptolemy-engine")
        rom = root.appendingPathComponent("openbios-ppc")
        tools = root.appendingPathComponent("PtolemyTools.dmg")
    }
    func validate() throws {
        for p in [engine, rom, tools] { try regular(p) }
        try check(FileManager.default.isExecutableFile(atPath: engine.path), "The bundled engine is not executable")
    }
    func arguments(_ item: Installation, _ folder: URL, cd: String?) -> [String] {
        var a = [
            "--rom", rom.path, "--radeon-fast", "--disk", folder.appendingPathComponent("Tiger.sparsebundle").path,
            "--ram-mb", String(item.ram), "--audio", "--nvram", folder.appendingPathComponent("nvram.bin").path, "--of",
            "auto-boot?=true", "--network-user", "--window", "--seconds", "0",
        ]  // rootless network on by default
        // Explicit model selection leaves the engine's optimization policy independent.
        a += ["--cpu", item.cpu]
        if let cd = cd { a += ["--cd", cd, "--boot-cd"] }
        return a
    }
    func environment(_ phase: InstallPhase) -> [String: String] {
        var e = ProcessInfo.processInfo.environment
        e["PT_FULLSCREEN"] = "1"
        e["PT_EDID_MODE"] = "screen"
        e["PT_POINTER_V2"] = e["PT_POINTER_V2"] ?? "1"
        e["PT_R1_UX"] = e["PT_R1_UX"] ?? "1"
        e["PT_PRODUCT_UI"] = "1"
        e["PT_RESTART"] = phase == .ready ? "reexec" : "exit"
        return e
    }
}
final class ProductLibrary {
    let root: URL
    let execute: (String, [String]) throws -> Data
    init(root: URL? = nil, execute: @escaping (String, [String]) throws -> Data = command) throws {
        let chosen =
            root
            ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Ptolemy/Installations")
        try FileManager.default.createDirectory(
            at: chosen, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        guard let canonical = realpath(chosen.path, nil) else {
            throw InstallError("Cannot resolve the installation library")
        }
        defer { free(canonical) }
        self.root = URL(fileURLWithPath: String(cString: canonical), isDirectory: true)
        self.execute = execute
    }

    func list() throws -> [URL] {
        try FileManager.default.contentsOfDirectory(
            at: root, includingPropertiesForKeys: nil, options: .skipsHiddenFiles
        ).filter { $0.pathExtension == "ptolemy" && (try? Installation.load($0)) != nil }.sorted {
            $0.lastPathComponent < $1.lastPathComponent
        }
    }
    func create(_ value: Installation) throws -> URL {
        try value.validate()
        let destination = root.appendingPathComponent(value.id.uuidString + ".ptolemy", isDirectory: true)
        let stage = root.appendingPathComponent(".create-" + UUID().uuidString)
        try FileManager.default.createDirectory(
            at: stage, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        var published = false
        defer { if !published { try? FileManager.default.removeItem(at: stage) } }
        _ = try execute(
            "/usr/bin/hdiutil", value.diskArguments + [stage.appendingPathComponent("Tiger.sparsebundle").path])
        try directory(stage.appendingPathComponent("Tiger.sparsebundle"))
        try value.save(stage)
        try FileManager.default.moveItem(at: stage, to: destination)
        published = true
        return destination
    }
    func resize(_ folder: URL, to size: Int, settings: Installation? = nil) throws {
        let lease = try ProductLock(folder)
        defer { withExtendedLifetime(lease) {} }
        var item = try settings ?? Installation.load(folder)
        try ImageMount.requireDetached(folder.appendingPathComponent("Tiger.sparsebundle"), execute: execute)
        try check(size > item.diskGiB && size <= 120, "Choose a larger disk, up to 120 GiB")
        let copy = root.appendingPathComponent(".resize-" + UUID().uuidString + ".ptolemy")
        try directory(folder)
        _ = try execute("/bin/cp", ["-c", "-R", folder.path, copy.path])
        var exchanged = false
        defer { if !exchanged { try? FileManager.default.removeItem(at: copy) } }
        _ = try execute(
            "/usr/bin/hdiutil", ["resize", "-size", "\(size)g", copy.appendingPathComponent("Tiger.sparsebundle").path])
        let copyLease = try ProductLock(copy)
        defer { withExtendedLifetime(copyLease) {} }
        item.diskGiB = size
        try item.save(copy)
        // Publish the disk and its configuration together; retain the old bundle.
        try check(
            renameatx_np(AT_FDCWD, folder.path, AT_FDCWD, copy.path, UInt32(RENAME_SWAP)) == 0,
            "Cannot publish resized installation")
        exchanged = true
        let backup = root.appendingPathComponent("Before Resize-" + UUID().uuidString + ".ptolemy-backup")
        try FileManager.default.moveItem(at: copy, to: backup)
        dropBackups(of: item.id, keeping: backup.lastPathComponent)  // one backup per machine: the bundle before its latest resize
    }

    func trash(_ folder: URL) throws {
        try check(
            folder.deletingLastPathComponent().standardizedFileURL.path == root.standardizedFileURL.path,
            "Installation is outside this library")
        let lease = try ProductLock(folder)
        defer { withExtendedLifetime(lease) {} }
        let id = try? Installation.load(folder).id
        try FileManager.default.trashItem(at: folder, resultingItemURL: nil)
        if let id { dropBackups(of: id) }
    }
    private func dropBackups(of id: UUID, keeping kept: String? = nil) {
        let fm = FileManager.default
        for old in (try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? []
        where old.pathExtension == "ptolemy-backup" && old.lastPathComponent != kept
            && (try? Installation.load(old))?.id == id
        { try? fm.removeItem(at: old) }
    }
}
struct ToolsPreinstaller {
    static func install(volume: URL, medium: URL) throws {
        _ = try validateSystemVolume(volume, installer: false)
        try directory(medium)
        try directory(medium.appendingPathComponent("payload"))
        let manifest = medium.appendingPathComponent("manifest.sha1")
        try regular(manifest)
        let bytes = try Data(contentsOf: manifest)
        try check(bytes.count <= 1024 * 1024, "Tools manifest is too large")
        guard let text = String(data: bytes, encoding: .utf8) else { throw InstallError("Invalid Tools manifest") }
        let receipt = sha1(bytes)
        let library = volume.appendingPathComponent("Library")
        try directory(library)
        let dest = library.appendingPathComponent("Ptolemy Tools")
        let stage = library.appendingPathComponent(".Ptolemy Tools-" + UUID().uuidString)
        /* root/payload/name: no link on its path below root, a regular file with this SHA-1. */
        func payload(_ root: URL, _ name: String, _ hash: String, _ link: String, _ mismatch: String) throws {
            let file = try checkedChild(root.appendingPathComponent("payload"), name)
            var parent = file
            while parent.path != root.path {
                try check(!(try parent.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink ?? false), link)
                parent = parent.deletingLastPathComponent()
            }
            try regular(file)
            try check(sha1(Data(contentsOf: file)) == hash, mismatch)
        }
        var entries = [(String, Int, String)]()
        var seen = Set<String>()
        for line in text.split(separator: "\n") {
            let fields = line.split(separator: " ", maxSplits: 2)
            try check(fields.count == 3, "Invalid Tools record")
            let hash = String(fields[0])
            let mode = String(fields[1])
            let name = String(fields[2])
            _ = try checkedChild(medium, name)
            try check(
                hash.count == 40 && hash.allSatisfy { "0123456789abcdef".contains($0) }
                    && ["0644", "0755"].contains(mode) && seen.insert(name).inserted, "Invalid Tools entry")
            try payload(medium, name, hash, "Link in Tools payload", "Tools checksum mismatch")
            entries.append((hash, mode == "0755" ? 0o755 : 0o644, name))
        }
        try check(!entries.isEmpty, "Empty Tools payload")
        let fm = FileManager.default
        if fm.fileExists(atPath: dest.path) {
            try directory(dest)
            try regular(dest.appendingPathComponent("manifest.sha1"))
            try check(
                try Data(contentsOf: dest.appendingPathComponent("manifest.sha1")) == bytes,
                "Different Tools already exist; preserved")
            for (hash, _, name) in entries {
                try payload(
                    dest, name, hash, "Link in installed Tools payload",
                    "Installed Tools checksum mismatch; files preserved")
            }
        } else {
            try fm.createDirectory(
                at: stage, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o755])
            defer { try? fm.removeItem(at: stage) }
            for (_, mode, name) in entries {
                let output = try checkedChild(stage.appendingPathComponent("payload"), name)
                try fm.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
                try fm.copyItem(at: medium.appendingPathComponent("payload/" + name), to: output)
                try fm.setAttributes([.posixPermissions: mode], ofItemAtPath: output.path)
            }
            for (hash, _, name) in entries {
                let file = stage.appendingPathComponent("payload/" + name)
                try check(sha1(Data(contentsOf: file)) == hash, "Tools changed while copying")
            }
            for name in ["install.py", "manifest.sha1"] {
                try regular(medium.appendingPathComponent(name))
                try fm.copyItem(at: medium.appendingPathComponent(name), to: stage.appendingPathComponent(name))
            }
            let contents = stage.appendingPathComponent("Ptolemy Tools Setup.app/Contents")
            let mac = contents.appendingPathComponent("MacOS")
            try fm.createDirectory(at: mac, withIntermediateDirectories: true)
            let script =
                "#!/bin/sh\nmkdir -p \"$HOME/Library/Logs\"\nexec /usr/bin/python \"/Library/Ptolemy Tools/install.py\" >> \"$HOME/Library/Logs/Ptolemy Tools Setup.log\" 2>&1\n"
            try Data(script.utf8).write(to: mac.appendingPathComponent("setup"))
            try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: mac.appendingPathComponent("setup").path)
            try plistData([
                "CFBundleExecutable": "setup", "CFBundleIdentifier": "org.ptolemy.experimental.tools-setup",
                "CFBundleName": "Ptolemy Tools Setup", "CFBundlePackageType": "APPL", "LSUIElement": true,
            ]).write(to: contents.appendingPathComponent("Info.plist"))
            try Data(receipt.utf8).write(to: stage.appendingPathComponent(".ptolemy-receipt"))
            try fm.moveItem(at: stage, to: dest)
        }
        let prefs = library.appendingPathComponent("Preferences")
        try directory(prefs)
        let file = prefs.appendingPathComponent("loginwindow.plist")
        if fm.fileExists(atPath: file.path) { try regular(file) }
        var value = fm.fileExists(atPath: file.path) ? try plist(Data(contentsOf: file)) : [:]
        var items = value["AutoLaunchedApplicationDictionary"] as? [[String: Any]] ?? []
        let path = "/Library/Ptolemy Tools/Ptolemy Tools Setup.app"
        if !items.contains(where: { $0["Path"] as? String == path }) { items.append(["Hide": false, "Path": path]) }
        value["AutoLaunchedApplicationDictionary"] = items
        try plistData(value).write(to: file, options: .atomic)
    }
}
