// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import UniformTypeIdentifiers

/* The machines are always listed; the selected one has one large button whose words say what it does now (Start Tiger, Show Tiger's Window,
 * Continue Installation, Finish Setup); a new machine is made in its own sheet. The window, its look and the sheets: Launcher.swift. */
final class PtolemyApp: NSObject, NSApplicationDelegate, NSWindowDelegate, NSDraggingDestination, NSUserInterfaceValidations {
    private var window: NSWindow!, library: ProductLibrary!, ui: MainWindow!, form: NewMachineForm?
    private var urls = [URL](), items = [Installation?](), index = 0, installer: URL?, identity: InstallerIdentity?,
        message = ""
    private var busy = false, quitting = false, running: URL?, started: Date?
    private let lock = NSLock()
    private var process: Process?, cancelled = false
    private var resources: ProductResources { ProductResources(root: Bundle.main.resourceURL!) }
    func applicationDidFinishLaunching(_ n: Notification) {
        do { library = try ProductLibrary() } catch {
            NSAlert(error: error).runModal()
            NSApp.terminate(nil)
            return
        }
        NSApp.setActivationPolicy(.regular)
        ui = MainWindow(target: self)
        window = ui.window
        window.delegate = self
        window.center()
        window.setFrameAutosaveName("PtolemyLauncher")
        ui.sidebar.selected = { [weak self] i in self?.select(i) }
        NSApp.mainMenu = mainMenu(target: self)
        _ = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { [weak self] _ in self?.updateCard() }
        reload()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
    private func reload() {
        do {
            urls = try library.list()
            items = urls.map { try? Installation.load($0) }
        } catch { show(error) }
        if !urls.indices.contains(index) { index = max(0, urls.count - 1) }
        refresh()
    }
    private func refresh() {
        guard ui != nil else { return }
        ui.sidebar.set(
            urls.indices.map { i -> RowModel in
                let item = items[i]
                let live = running == urls[i]
                let state =
                    live
                    ? "Running"
                    : item?.phase == .ready
                        ? "Stopped" : item?.phase == .toolsPending ? "Setup: step 2 of 3" : "Installing: step 1 of 3"
                return RowModel(
                    guest: item.map { Guest($0.installerIdentity) }, name: item?.name ?? "Unreadable machine",
                    system: item.map { "\(Art.fullName($0.shownIdentity)) (\($0.shownIdentity.build))" } ?? "",
                    state: state, running: live)
            }, selected: index)
        updateCard()
    }
    private func updateCard() {
        guard ui != nil else { return }
        let has = urls.indices.contains(index)
        let live = has && running == urls[index]
        let item: Installation? = has ? items[index] : nil
        var m = StageModel()
        m.busy = busy && running == nil
        m.actionsEnabled = has && !busy
        if let item = item {
            let minutes = started.map { Int(Date().timeIntervalSince($0) / 60) } ?? 0
            let cpu = cpus.first(where: { $0.id == item.cpu })?.name ?? item.cpu
            let sys = item.installerIdentity.system
            m.guest = Guest(item.installerIdentity)  // the look follows the selected machine's system
            m.status =
                live
                ? (minutes < 1 ? "Running" : "Running for \(minutes) minute\(minutes == 1 ? "" : "s")")
                : item.phase == .ready
                    ? "Ready"
                    : item.phase == .toolsPending ? "Setup: Ptolemy Tools are next" : "Installation not finished"
            m.name = item.name
            m.system = "\(Art.fullName(item.shownIdentity)) (\(item.shownIdentity.build))"
            m.specs =
                "PowerPC \(cpu) · \(item.ram >= 1024 && item.ram % 1024 == 0 ? "\(item.ram / 1024) GB" : "\(item.ram) MB") memory · \(item.diskGiB) GB disk"
            m.primary =
                live
                ? "Show \(sys)’s Window"
                : item.phase == .ready
                    ? "Start \(sys)" : item.phase == .toolsPending ? "Finish Setup" : "Continue Installation"
            m.primaryEnabled = live ? process != nil : !busy
            m.note =
                live
                ? "To stop, shut down from \(sys)’s Apple menu."
                : busy && running != nil ? "Another machine is running." : message
        } else if has {
            m.guest = .tiger
            m.status = "Unreadable"
            m.name = "Unreadable machine"
            m.specs = "This machine’s settings could not be read."
            m.note = message
            m.primary = "Start"
            m.primaryEnabled = false
            m.actionsEnabled = false
        } else {
            m.status = "Welcome"
            m.name = "No machines yet"
            m.specs = "Drop a Tiger or Leopard install DVD image onto this window, or choose New Machine…"
            m.note = message
            m.primary = "New Machine…"
            m.primaryEnabled = !busy
        }
        primaryEnabled = m.primaryEnabled
        ui.stage.show(m)
        ui.setPrimary(m.primary, showsWindow: live)
        NSApp.mainMenu?.item(withTitle: "Machine")?.submenu?.item(at: 0)?.title = m.primary
    }
    private var primaryEnabled = true
    /* Menu commands and toolbar items are enabled as the stage's buttons are. */
    func validateUserInterfaceItem(_ item: NSValidatedUserInterfaceItem) -> Bool {
        switch item.action {
        case #selector(primaryAction): return primaryEnabled
        case #selector(newMachine): return !busy && form == nil
        case #selector(settings), #selector(remove): return selected != nil && !busy
        case #selector(reveal): return selected != nil
        default: return true
        }
    }
    private var selected: URL? { urls.indices.contains(index) ? urls[index] : nil }
    private func system(_ folder: URL?) -> String {
        folder.flatMap { urls.firstIndex(of: $0) }.flatMap { items[$0]?.installerIdentity.system } ?? "Tiger"
    }
    private func state(_ text: String) {
        DispatchQueue.main.async {
            self.message = text
            self.updateCard()
        }
    }
    private func show(_ error: Error) {
        message = String(describing: error)
        updateCard()
        if let w = window { NSAlert(error: error).beginSheetModal(for: w) }
    }
    private func work(_ body: @escaping () throws -> Void) {
        guard !busy else { return }
        busy = true
        lock.lock()
        cancelled = false
        lock.unlock()
        refresh()
        DispatchQueue.global(qos: .userInitiated).async {
            let result = Result { try body() }
            DispatchQueue.main.async {
                self.busy = false
                self.running = nil
                self.started = nil
                self.reload()
                if case .failure(let e) = result { self.show(e) }
                if self.quitting { NSApp.terminate(nil) }
            }
        }
    }
    private func select(_ i: Int) {
        index = i
        message = ""
        updateCard()
    }
    @objc func primaryAction() {
        guard let folder = selected else {
            newMachine()
            return
        }
        if running == folder {
            lock.lock()
            let pid = process?.processIdentifier
            lock.unlock()
            if let pid { _ = NSRunningApplication(processIdentifier: pid)?.activate(options: [.activateAllWindows]) }
            return
        }
        work {
            try self.resources.validate()
            try self.run(folder)
        }
    }
    @objc func aboutWeb() {
        let a = NSAlert()
        a.messageText = "Web pages in Tiger and Leopard"
        a.informativeText =
            "The browsers of Tiger and Leopard can no longer open most of today’s secure sites. To help them, your Mac fetches http:// pages for them over a secure HTTPS connection whenever the site offers one, and adjusts them a little (links, newer picture formats) so the old browsers can show them. All of this happens on this Mac and nothing is sent anywhere else: the sites are reached the same way as when you browse on your Mac. Browsers that handle https:// themselves, such as PowerFox, connect directly."
        a.beginSheetModal(for: window)
    }
    @objc func reveal() { if let f = selected { NSWorkspace.shared.activateFileViewerSelecting([f]) } }
    /* New machine: its own sheet (install image, name, disk size). */
    @objc func newMachine() {
        guard !busy, form == nil else { return }
        installer = nil
        identity = nil
        let f = NewMachineForm()
        f.choose = { [weak self] in self?.pick() }
        f.create = { [weak self] in self?.create() }
        f.cancel = { [weak self] in self?.closeSheet() }
        form = f
        window.beginSheet(f.window)
    }
    private func closeSheet() {
        if let f = form {
            window.endSheet(f.window)
            form = nil
        }
    }
    private func pick() {
        guard !busy else { return }
        let p = NSOpenPanel()
        p.title = "Choose a PowerPC Tiger or Leopard Install DVD Image"
        p.allowedContentTypes = [.diskImage, UTType(filenameExtension: "iso") ?? .data]
        p.allowsMultipleSelection = false
        p.beginSheetModal(for: form?.window ?? window) { if $0 == .OK, let url = p.url { self.choose(url) } }
    }
    private func choose(_ url: URL) {
        guard !busy else { return }
        form?.chosen.stringValue = "Checking \(url.lastPathComponent)…"
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                try regular(url)
                let image = try ImageMount(image: url, readOnly: true)
                let identity = try validateSystemVolume(image.volume)
                try image.close()
                DispatchQueue.main.async {
                    self.installer = url
                    self.identity = identity
                    guard let f = self.form else { return }
                    if ["Tiger", "Leopard"].contains(f.name.stringValue) { f.name.stringValue = identity.system }
                    f.chosen.stringValue = "\(Art.fullName(identity)) (\(identity.build)) for PowerPC · \(url.lastPathComponent)"
                }
            } catch {
                DispatchQueue.main.async {
                    self.form?.chosen.stringValue = "Not a PowerPC Tiger or Leopard install DVD: \(error)"
                }
            }
        }
    }
    private func create() {
        guard let url = installer, let identity = identity, let f = form else {
            pick()
            return
        }
        var item = Installation(installer: url.path, installerIdentity: identity)
        item.name = f.name.stringValue
        item.diskGiB = Int(f.slider.doubleValue)
        closeSheet()
        work {
            try self.resources.validate()
            self.state("Creating the \(identity.system) disk…")
            let folder = try self.library.create(item)
            DispatchQueue.main.async {
                self.reload()
                if let i = self.urls.firstIndex(of: folder) {
                    self.index = i
                    self.refresh()
                }
            }
            try self.run(folder)
        }
    }
    private func run(_ folder: URL) throws {
        let lease = try ProductLock(folder)
        defer { withExtendedLifetime(lease) {} }
        var item = try Installation.load(folder)
        while true {
            lock.lock()
            let cancel = cancelled
            lock.unlock()
            if cancel { return }
            if item.phase == .toolsPending {
                state("Installing Ptolemy Tools…")
                let medium = try ImageMount(image: resources.tools, readOnly: true)
                do {
                    let target = try ImageMount(
                        image: folder.appendingPathComponent("Tiger.sparsebundle"), readOnly: false)
                    try ToolsPreinstaller.install(volume: target.volume, medium: medium.volume)
                    item.systemIdentity = try? validateSystemVolume(target.volume, installer: false)
                    item.toolsDigest = try? resources.toolsDigest()
                    try target.close()
                    try medium.close()
                } catch {
                    throw InstallError(
                        "Ptolemy Tools could not be installed: \(error). Finish Setup tries this step again; Settings can resume an unfinished installation; the disk is kept."
                    )
                }
                item.phase = .ready
                try item.save(folder)
                continue
            }
            var optical: ImageMount?
            let shadow = folder.appendingPathComponent("installer.shadow")
            if item.phase == .installer {
                state("Opening the \(item.installerIdentity.system) installer…")
                optical = try openInstaller(item, shadow: shadow)
                _ = try validateSystemVolume(optical!.volume)
            }
            try ImageMount.requireDetached(folder.appendingPathComponent("Tiger.sparsebundle"))
            /* Silent Tools update (owner, 17.11): a machine whose disk has other Tools than this Ptolemy carries gets them
             * before it starts - the files on the disk now, the guest's own install at the next login, with no question. A
             * failure only leaves the earlier Tools: the machine starts anyway. Before ensureBundleV1: a writable attach. */
            if item.phase == .ready, item.toolsDigest != (try? resources.toolsDigest()) {
                state("Updating Ptolemy Tools…")
                do {
                    _ = try updateTools(folder, &item, resources)
                    state("")
                } catch {
                    state("Ptolemy Tools were not updated (\(error)); \(item.installerIdentity.system) starts with the Tools it has.")
                }
            }
            if item.phase == .ready, try ensureBundleV1(folder.appendingPathComponent("Tiger.sparsebundle")) {
                state("The disk had been opened in Finder; Ptolemy converted it back.")
            }
            let child = Process()
            child.executableURL = resources.engine
            child.arguments = resources.arguments(
                item, folder, cd: optical.map { $0.device.replacingOccurrences(of: "/dev/disk", with: "/dev/rdisk") })
            child.environment = resources.environment(item.phase, item)
            child.environment?["PT_GUEST_SYSTEM"] = item.installerIdentity.system  // the engine's menus and messages name it
            let (logURL, log) = try openSessionLog(folder)
            child.standardOutput = log
            child.standardError = log
            child.standardInput = FileHandle.nullDevice
            lock.lock()
            if cancelled {
                lock.unlock()
                try? log.close()
                return
            }
            do {
                try child.run()
                process = child
                lock.unlock()
            } catch {
                process = nil
                lock.unlock()
                try? log.close()
                throw error
            }
            DispatchQueue.main.async {
                self.running = folder
                self.started = Date()
                self.message = ""
                self.refresh()
            }
            state(
                item.phase == .installer
                    ? "Complete the installation in \(item.installerIdentity.system); setup continues after Restart."
                    : "")
            child.waitUntilExit()
            try log.close()
            lock.lock()
            process = nil
            let stop = cancelled
            lock.unlock()
            try optical?.close()
            try? FileManager.default.removeItem(at: shadow)
            DispatchQueue.main.async {
                self.running = nil
                self.started = nil
                self.refresh()
            }
            if stop { return }
            if child.terminationReason == .exit && child.terminationStatus == 95 && item.phase == .installer {
                item.phase = .toolsPending
                try item.save(folder)
                continue
            }
            try check(
                child.terminationReason == .exit && child.terminationStatus == 0,
                "\(item.installerIdentity.system) stopped unexpectedly. The disk is kept; see \(logURL.lastPathComponent)."
            )
            if item.phase == .ready, let id = try? installedIdentity(folder),
                id.version != item.systemIdentity?.version || id.build != item.systemIdentity?.build
            {
                item.systemIdentity = id  // an update installed in the guest shows in the window
                try? item.save(folder)
            }
            try? FileManager.default.removeItem(at: folder.appendingPathComponent("Tiger-v2.sparsebundle"))  // U1: started and stopped normally
            state(item.phase == .ready ? "" : "Installation paused. Continue Installation resumes it.")
            return
        }
    }
    @objc func settings() {
        guard !busy, let folder = selected else { return }
        do {
            var item = try Installation.load(folder)
            let (alert, cpu, ram, disk, display, upscale, scale) = settingsAlert(item)
            alert.beginSheetModal(for: window) { response in
                if response == .alertThirdButtonReturn {
                    self.work {
                        do {
                            let lease = try ProductLock(folder)
                            defer { withExtendedLifetime(lease) {} }
                            item.phase = .installer
                            try item.save(folder)
                        }
                        try self.resources.validate()
                        try self.run(folder)
                    }
                    return
                }
                guard response == .alertFirstButtonReturn else { return }
                item.cpu = cpuChoices(item)[cpu.indexOfSelectedItem].id
                item.ram = Int(ram.titleOfSelectedItem!)!
                item.display = display.indexOfSelectedItem > 0 ? displayModes[display.indexOfSelectedItem] : nil
                item.upscale = upscale.state == .on
                let chosen = renderScales[scale.indexOfSelectedItem]
                item.renderScale = chosen == defaultRenderScale ? nil : chosen  // nil follows the default
                guard let size = Int(disk.stringValue), size >= item.diskGiB && size <= 120 else {
                    self.show(InstallError("Disk size must stay the same or increase, up to 120 GB"))
                    return
                }
                self.work {
                    if size > item.diskGiB {
                        self.state("Growing a cloned disk; keeping a recovery copy…")
                        try self.library.resize(folder, to: size, settings: item)
                    } else {
                        let lease = try ProductLock(folder)
                        defer { withExtendedLifetime(lease) {} }
                        try item.save(folder)
                    }
                    self.state("Settings saved.")
                }
            }
        } catch { show(error) }
    }
    @objc func remove() {
        guard !busy, let folder = selected else { return }
        let a = NSAlert()
        a.messageText = "Move this \(system(folder)) machine to the Trash?"
        a.informativeText = "Its disk and saved files go to the Mac’s Trash. Your install DVD image is kept."
        a.addButton(withTitle: "Move to Trash")
        a.addButton(withTitle: "Cancel")
        a.beginSheetModal(for: window) {
            if $0 == .alertFirstButtonReturn {
                self.work {
                    try self.library.trash(folder)
                    self.state("Moved to the Trash.")
                }
            }
        }
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard busy else { return .terminateNow }
        let sys = system(running)
        let a = NSAlert()
        a.messageText = "Shut down \(sys) and quit Ptolemy?"
        a.informativeText =
            "\(sys) shuts down as from its Apple menu; its applications may ask you to save first. Ptolemy quits when \(sys) has stopped."
        a.addButton(withTitle: "Shut Down and Quit")
        a.addButton(withTitle: "Cancel")
        // SIGHUP asks Tiger to shut down (stand_window_macos.m); SIGTERM would stop it at once and lose its unwritten data.
        if a.runModal() == .alertFirstButtonReturn {
            quitting = true
            lock.lock()
            cancelled = true
            let child = process
            lock.unlock()
            if child?.isRunning == true { kill(child!.processIdentifier, SIGHUP) }
            state("Waiting for \(sys) to shut down…")
        }
        return .terminateCancel
    }
    /* An install image dropped anywhere on the window starts a new machine with it. */
    private func droppedImage(_ info: NSDraggingInfo) -> URL? {
        let urls =
            info.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true])
            as? [URL]
        return urls?.count == 1 && !busy ? urls?[0] : nil
    }
    func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard droppedImage(sender) != nil else { return [] }
        ui.stage.dropTarget = true
        return .copy
    }
    func draggingExited(_ sender: NSDraggingInfo?) { ui.stage.dropTarget = false }
    func draggingEnded(_ sender: NSDraggingInfo) { ui.stage.dropTarget = false }
    func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        ui.stage.dropTarget = false
        guard let url = droppedImage(sender) else { return false }
        newMachine()
        choose(url)
        return true
    }
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if busy {
            NSApp.terminate(nil)
            return false
        }
        NSApp.terminate(nil)
        return true
    }
    static func main() {
        let app = NSApplication.shared
        let delegate = PtolemyApp()
        app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    }
}
