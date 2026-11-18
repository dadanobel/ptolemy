// SPDX-License-Identifier: GPL-2.0-or-later

import CryptoKit
import Foundation

func productSelfTest() throws {
    try check(
        InstallError("Specific recovery instruction").localizedDescription == "Specific recovery instruction",
        "Native alerts must preserve actionable errors")
    let fm = FileManager.default
    let root = fm.temporaryDirectory.appendingPathComponent("ptolemy-product-test-" + UUID().uuidString)
    try fm.createDirectory(at: root, withIntermediateDirectories: false)
    defer { try? fm.removeItem(at: root) }
    func reject(_ body: () throws -> Void) throws {
        var rejected = false
        do { try body() } catch { rejected = true }
        try check(rejected, "Negative control unexpectedly passed")
    }
    let volume = root.appendingPathComponent("Tiger")
    let medium = root.appendingPathComponent("Tools")
    try fm.createDirectory(
        at: volume.appendingPathComponent("System/Library/CoreServices"), withIntermediateDirectories: true)
    try fm.createDirectory(
        at: volume.appendingPathComponent("System/Installation/Packages"), withIntermediateDirectories: true)
    try fm.createDirectory(at: volume.appendingPathComponent("Library/Preferences"), withIntermediateDirectories: true)
    try plistData(["ProductVersion": "10.4.11", "ProductBuildVersion": "8S165"]).write(
        to: volume.appendingPathComponent("System/Library/CoreServices/SystemVersion.plist"))
    var kernel = Data([0xfe, 0xed, 0xfa, 0xce, 0, 0, 0, 18])
    kernel.append(Data(repeating: 0, count: 24))
    try kernel.write(to: volume.appendingPathComponent("mach_kernel"))
    try check(try validateSystemVolume(volume).version == "10.4.11", "PPC Tiger validation")
    let versions = volume.appendingPathComponent("System/Library/CoreServices/SystemVersion.plist")
    try plistData(["ProductVersion": "10.5.4", "ProductBuildVersion": "9E17"]).write(to: versions)
    try check(try validateSystemVolume(volume).system == "Leopard", "PPC Leopard validation")
    try plistData(["ProductVersion": "10.6", "ProductBuildVersion": "10A432"]).write(to: versions)
    try reject { _ = try validateSystemVolume(volume) }
    try plistData(["ProductVersion": "10.4.11", "ProductBuildVersion": "8S165"]).write(to: versions)
    try check(
        !ppc32Kernel(Data([0xfe, 0xed, 0xfa, 0xce, 0, 0, 0, 7])) && !ppc32Kernel(Data("random".utf8)),
        "Intel/random negative control")
    kernel[7] = 7
    try kernel.write(to: volume.appendingPathComponent("mach_kernel"))
    try reject { _ = try validateSystemVolume(volume) }
    kernel[7] = 18
    try kernel.write(to: volume.appendingPathComponent("mach_kernel"))
    try fm.createDirectory(at: medium.appendingPathComponent("payload/bin"), withIntermediateDirectories: true)
    let file = Data("our worker".utf8)
    try file.write(to: medium.appendingPathComponent("payload/bin/worker"))
    try Data("print 'fixture'\n".utf8).write(to: medium.appendingPathComponent("install.py"))
    let hash = Insecure.SHA1.hash(data: file).map { String(format: "%02x", $0) }.joined()
    try Data("\(hash) 0755 bin/worker\n".utf8).write(to: medium.appendingPathComponent("manifest.sha1"))
    let prefs = volume.appendingPathComponent("Library/Preferences/loginwindow.plist")
    try plistData(["Preserve": 17]).write(to: prefs)
    try ToolsPreinstaller.install(volume: volume, medium: medium)
    try ToolsPreinstaller.install(volume: volume, medium: medium)
    let value = try plist(Data(contentsOf: prefs))
    try check(
        value["Preserve"] as? Int == 17 && (value["AutoLaunchedApplicationDictionary"] as? [[String: Any]])?.count == 1,
        "Idempotent Tools and preserved preferences")
    /* Silent update: newer Tools of ours replace the installed ones; a folder without our receipt is preserved. */
    let tools = volume.appendingPathComponent("Library/Ptolemy Tools")
    let newer = Data("our newer worker".utf8)
    try newer.write(to: medium.appendingPathComponent("payload/bin/worker"))
    try Data("\(sha1(newer)) 0755 bin/worker\n".utf8).write(to: medium.appendingPathComponent("manifest.sha1"))
    try ToolsPreinstaller.install(volume: volume, medium: medium)
    try check(
        try Data(contentsOf: tools.appendingPathComponent("payload/bin/worker")) == newer
            && (try fm.contentsOfDirectory(atPath: volume.appendingPathComponent("Library").path)).filter {
                $0.hasPrefix(".Ptolemy Tools")
            }.isEmpty, "Silent update: newer Tools replace ours, nothing left beside")
    try fm.removeItem(at: tools.appendingPathComponent(".ptolemy-receipt"))
    try newer.write(to: medium.appendingPathComponent("payload/bin/worker"))
    try Data("\(sha1(file)) 0755 bin/worker\n".utf8).write(to: medium.appendingPathComponent("manifest.sha1"))
    try file.write(to: medium.appendingPathComponent("payload/bin/worker"))
    try reject { try ToolsPreinstaller.install(volume: volume, medium: medium) }  // someone else's folder: preserved
    try check(try Data(contentsOf: tools.appendingPathComponent("payload/bin/worker")) == newer, "Foreign Tools untouched")
    try Data("tampered".utf8).write(to: medium.appendingPathComponent("payload/bin/worker"))
    try reject { try ToolsPreinstaller.install(volume: volume, medium: medium) }
    try reject { _ = try checkedChild(root, "../outside") }
    try reject { _ = try checkedChild(root, "/absolute") }
    let identity = InstallerIdentity(version: "10.4", build: "8A428")
    let installer = root.appendingPathComponent("installer.iso")
    var item = Installation(installer: installer.path, installerIdentity: identity)
    try check(item.shownIdentity.version == "10.4", "The window names the installer until the system is read")
    item.systemIdentity = InstallerIdentity(version: "10.4.11", build: "8S165")
    try check(item.shownIdentity.build == "8S165", "The window names the installed system once read")
    let legacy = try JSONDecoder().decode(
        Installation.self,
        from: Data(
            #"{"schema":1,"id":"\#(UUID().uuidString)","name":"Tiger","cpu":"7447a","ram":2048,"diskGiB":40,"phase":"ready","installer":"/x.iso","installerIdentity":{"version":"10.4","build":"8A428"}}"#
                .utf8))
    try check(legacy.systemIdentity == nil && legacy.shownIdentity.build == "8A428", "installation.json without systemIdentity")
    try check(legacy.toolsDigest == nil, "installation.json without toolsDigest: Tools are put on the disk at the next start")
    item.systemIdentity = nil
    let machine = root.appendingPathComponent("u1"), disk = machine.appendingPathComponent("Tiger.sparsebundle")
    try fm.createDirectory(at: disk, withIntermediateDirectories: true)
    try plistData(["bundle-backingstore-version": 1]).write(to: disk.appendingPathComponent("Info.plist"))
    try check(!(try ensureBundleV1(disk) { _, _ in throw InstallError("hdiutil must not run for a version-1 disk") }), "U1: v1 untouched")
    try plistData(["bundle-backingstore-version": 2]).write(to: disk.appendingPathComponent("Info.plist"))
    try check(
        try ensureBundleV1(disk) { _, args in
            let out = URL(fileURLWithPath: args.last!)
            try fm.createDirectory(at: out, withIntermediateDirectories: false)
            try plistData(["bundle-backingstore-version": 1]).write(to: out.appendingPathComponent("Info.plist"))
            return Data()
        }, "U1: v2 converted")
    try check(
        (try plist(Data(contentsOf: disk.appendingPathComponent("Info.plist"))))["bundle-backingstore-version"] as? Int == 1
            && fm.fileExists(atPath: machine.appendingPathComponent("Tiger-v2.sparsebundle").path), "U1: v1 in place, v2 kept")
    var commands = [(String, [String])]()
    var failResize = false
    let library = try ProductLibrary(root: root.appendingPathComponent("Library")) { tool, args in
        commands.append((tool, args))
        if args.first == "create" {
            try fm.createDirectory(at: URL(fileURLWithPath: args.last!), withIntermediateDirectories: false)
        }
        if tool == "/bin/cp" { try fm.copyItem(at: URL(fileURLWithPath: args[2]), to: URL(fileURLWithPath: args[3])) }
        if args.first == "resize" && failResize { throw InstallError("Injected resize failure") }
        return try plistData(["images": []])
    }
    let folder = try library.create(item)
    try check(try library.list() == [folder], "Library publication")  // URL equality, as the window selects it
    let recipe = commands[0].1
    try check(
        recipe.contains("SPARSEBUNDLE") && recipe.contains("HFS+J") && recipe.contains("SPUD"), "Sparse HFS/APM recipe")
    do {
        let lease = try ProductLock(folder)
        try reject { _ = try ProductLock(folder) }
        withExtendedLifetime(lease) {}
    }
    failResize = true
    try reject { try library.resize(folder, to: 80) }
    try check(try Installation.load(folder).diskGiB == 40, "Failed resize preserved configuration")
    failResize = false
    try library.resize(folder, to: 80)
    try check(try Installation.load(folder).diskGiB == 80, "Atomic resize publication")
    try library.resize(folder, to: 100)
    let backups = try fm.contentsOfDirectory(at: library.root, includingPropertiesForKeys: nil).filter {
        $0.pathExtension == "ptolemy-backup"
    }
    try check(
        backups.count == 1 && (try Installation.load(backups[0])).diskGiB == 80,
        "One resize backup per machine, the latest")
    try reject { try library.resize(folder, to: 20) }
    item.ram = 513
    try reject { try item.validate() }
    item.ram = 2048
    item.cpu = "G5"
    try reject { try item.validate() }
    item.cpu = "7447a-dual"
    try reject { try item.validate() }  // Tiger: no Dual G4
    var leopard = item
    leopard.installerIdentity = InstallerIdentity(version: "10.5", build: "9A581")
    try leopard.validate()
    try check(
        !cpuChoices(item).contains { $0.id == "7447a-dual" } && cpuChoices(leopard).contains { $0.id == "7447a-dual" },
        "Dual G4 is offered under Leopard only")
    let resources = ProductResources(root: root)
    item.cpu = "7447a"
    let a = resources.arguments(item, folder, cd: "/dev/rdisk20")
    for cpu in cpus {
        var chosen = item
        chosen.cpu = cpu.id
        let args = resources.arguments(chosen, folder, cd: nil)
        try check(
            args.contains("--window") && args.firstIndex(of: "--cpu").map { args[$0 + 1] == (cpu.id == "7447a-dual" ? "7447a" : cpu.id) } == true,
            "Explicit CPU launch contract")
        try check((resources.environment(.ready, chosen)["PT_CPUS"] == "2") == (cpu.id == "7447a-dual"), "Dual G4 asks the engine for two CPUs")
    }
    let oldFast = getenv("PT_CPU_FAST").map { String(cString: $0) }
    setenv("PT_CPU_FAST", "0", 1)
    try check(resources.environment(.ready)["PT_CPU_FAST"] == "0", "CPU profile opt-out survives launcher")
    if let oldFast = oldFast { setenv("PT_CPU_FAST", oldFast, 1) } else { unsetenv("PT_CPU_FAST") }

    try check(a.contains("--network-user"), "Tiger gets the rootless network")
    try check(
        a.contains("--boot-cd") && !a.contains("--overlay") && resources.environment(.installer)["PT_RESTART"] == "exit"
            && resources.environment(.ready)["PT_FULLSCREEN"] == "1", "Installer/ready launch contract")
    let mounted = try plistData(["images": [["image-path": installer.path]]])
    try reject { try ImageMount.requireDetached(installer, execute: { _, _ in mounted }) }
    var detached = [String]()
    let attachment = try plistData([
        "system-entities": [["dev-entry": "/dev/disk25"], ["dev-entry": "/dev/disk25s3", "mount-point": volume.path]]
    ])
    var mediaID: UInt64 = 55
    let mount = try ImageMount(
        image: installer, readOnly: true,
        execute: { _, args in
            if args.first == "info" { return try plistData(["images": []]) }
            if args.first == "attach" {
                try check(args.contains("-readonly"), "Installer must mount read-only")
                return attachment
            }
            detached = args
            return Data()
        }, identity: { _ in mediaID })
    let shadowed = try ImageMount(
        image: installer, readOnly: false, shadow: root.appendingPathComponent("installer.shadow"),
        execute: { _, args in
            if args.first == "info" { return try plistData(["images": []]) }
            if args.first == "attach" {
                try check(
                    args.contains("-shadow") && !args.contains("-readonly"), "Writable installer only through a shadow")
                return attachment
            }
            return Data()
        }, identity: { _ in mediaID })
    try shadowed.close()
    mediaID = 56
    try reject { try mount.close() }
    try check(detached.isEmpty, "Recycled disk number was not ejected")
    mediaID = 55
    try mount.close()
    try check(detached == ["detach", "/dev/disk25"], "Exact owned image detach")
    let previous = ProcessInfo.processInfo.environment["PT_R1_UX"]
    setenv("PT_R1_UX", "0", 1)
    try check(resources.environment(.ready)["PT_R1_UX"] == "0", "Explicit UX opt-out must survive the product wrapper")
    if let previous = previous { setenv("PT_R1_UX", previous, 1) } else { unsetenv("PT_R1_UX") }
    let logs = root.appendingPathComponent("Logs")
    try fm.createDirectory(at: logs, withIntermediateDirectories: false)
    try Data().write(to: logs.appendingPathComponent("session-\(UUID().uuidString).log"))
    try Data().write(to: logs.appendingPathComponent("installation.json"))
    for _ in 0..<3 { try openSessionLog(logs).1.close() }
    try check(
        Set(try fm.contentsOfDirectory(atPath: logs.path)) == [
            "session.log", "session-previous.log", "installation.json",
        ], "Session logs: this run's and the previous one")
    print(
        "PASS: PPC/Intel image controls, sparse disk recipe, locks, atomic create/resize rollback, one resize backup, session logs, native Tools checksums/idempotence and launch policy"
    )
}
