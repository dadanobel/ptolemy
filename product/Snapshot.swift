// SPDX-License-Identifier: GPL-2.0-or-later
// `Ptolemy --snapshot DIR`: every state of the launcher's window drawn into PNG files without showing a window (no screen
// recording permission, no library, no engine): empty, an image dragged over, the new machine's sheet with its disk size,
// the list with Tiger and Leopard and each one's pane, the settings - in the light and the dark appearance, and again with
// reduced transparency. The windows are the real ones (Launcher.swift) over made-up machines.
// What a snapshot cannot show: the system's live materials (glass, the sidebar's blur) are composed by the window server
// over what lies behind a window on screen; drawn off screen they are holes. So the list is drawn without the sidebar's
// material (MainWindow, offscreen), and the window's background colour is put under every picture.
import AppKit

private func capture(_ view: NSView?) -> NSBitmapImageRep? {
    guard let v = view else { return nil }
    v.layoutSubtreeIfNeeded()
    v.displayIfNeeded()
    guard let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) else { return nil }
    v.cacheDisplay(in: v.bounds, to: rep)
    return rep
}
/* The whole window with its title bar and toolbar: the frame view above the content view. */
private func capture(_ window: NSWindow) -> NSBitmapImageRep? { capture(window.contentView?.superview ?? window.contentView) }
/* The window's background colour under a picture: what is translucent or a hole in it becomes what a window shows. */
private func backed(_ image: NSBitmapImageRep, _ paper: NSColor) -> NSBitmapImageRep {
    guard let context = NSGraphicsContext(bitmapImageRep: image) else { return image }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    paper.setFill()
    NSRect(origin: .zero, size: image.size).fill(using: .destinationOver)
    NSGraphicsContext.restoreGraphicsState()
    return image
}
/* A sheet over its window: the window dimmed, the sheet under the toolbar with a shadow. */
private func over(_ base: NSBitmapImageRep, _ sheet: NSBitmapImageRep, _ paper: NSColor) -> NSBitmapImageRep {
    guard let context = NSGraphicsContext(bitmapImageRep: base) else { return base }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    let b = base.size, s = sheet.size
    NSColor(white: 0, alpha: 0.22).setFill()
    NSRect(origin: .zero, size: b).fill(using: .sourceOver)
    let r = NSRect(x: ((b.width - s.width) / 2).rounded(), y: b.height - s.height - 64, width: s.width, height: s.height)
    let path = NSBezierPath(roundedRect: r, xRadius: 12, yRadius: 12)
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowColor = NSColor(white: 0, alpha: 0.45)
    shadow.shadowBlurRadius = 24
    shadow.shadowOffset = NSSize(width: 0, height: -8)
    shadow.set()
    paper.setFill()
    path.fill()
    NSGraphicsContext.restoreGraphicsState()
    path.addClip()
    // Over the sheet's background, not instead of it: a plain draw(in:) copies the sheet's clear pixels too.
    sheet.draw(in: r, from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: false, hints: nil)
    NSGraphicsContext.restoreGraphicsState()
    return base
}
private func settle() { RunLoop.current.run(until: Date().addingTimeInterval(0.2)) }

func productSnapshot(_ directory: String) throws {
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)
    app.finishLaunching()
    let out = URL(fileURLWithPath: directory, isDirectory: true)
    try FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
    let tiger = Installation(installer: "/Tiger.iso", installerIdentity: InstallerIdentity(version: "10.4.11", build: "8S165"))
    var leopard = Installation(installer: "/Leopard.iso", installerIdentity: InstallerIdentity(version: "10.5.8", build: "9L31a"))
    leopard.name = "Leopard"
    leopard.phase = .ready
    var ready = tiger
    ready.phase = .ready
    func row(_ i: Installation, _ state: String, running: Bool = false) -> RowModel {
        RowModel(
            guest: Guest(i.installerIdentity), name: i.name,
            system: "\(Art.fullName(i.installerIdentity)) (\(i.installerIdentity.build))", state: state, running: running)
    }
    func pane(_ i: Installation, running: Bool) -> StageModel {
        let s = i.installerIdentity.system
        return StageModel(
            guest: Guest(i.installerIdentity), status: running ? "Running for 12 minutes" : "Ready", name: i.name,
            system: "\(Art.fullName(i.installerIdentity)) (\(i.installerIdentity.build))",
            specs: "PowerPC G4 7447A · 2 GB memory · \(i.diskGiB) GB disk",
            note: running ? "To stop, shut down from \(s)’s Apple menu." : "",
            primary: running ? "Show \(s)’s Window" : "Start \(s)", primaryEnabled: true, actionsEnabled: !running, busy: false)
    }
    let empty = StageModel(
        guest: nil, status: "Welcome", name: "No machines yet", system: "",
        specs: "Drop a Tiger or Leopard install DVD image onto this window, or choose New Machine…", note: "",
        primary: "New Machine…", primaryEnabled: true, actionsEnabled: false, busy: false)
    var written = 0, list = ""
    for dark in [false, true] {
        for opaque in [false, true] {
            Env.transparencyOff = opaque
            Env.contrastUp = false
            Env.motionOff = true
            let look = NSAppearance(named: dark ? .darkAqua : .aqua)
            let suffix = (dark ? "dark" : "light") + (opaque ? "-reduced-transparency" : "") + ".png"
            var paper = NSColor.windowBackgroundColor
            look?.performAsCurrentDrawingAppearance { paper = NSColor(cgColor: paper.cgColor) ?? paper }
            let ui = MainWindow(target: nil, offscreen: true)
            ui.window.appearance = look
            ui.window.setFrameOrigin(NSPoint(x: -30000, y: -30000))
            ui.window.orderFrontRegardless()
            func write(_ name: String, sheet: NSView? = nil) throws {
                settle()
                guard var image = capture(ui.window) else { throw InstallError("The window could not be drawn for \(name)") }
                image = backed(image, paper)
                if let sheet = sheet {
                    guard let top = capture(sheet) else { throw InstallError("The sheet could not be drawn for \(name)") }
                    image = over(image, top, paper)
                }
                guard let png = image.representation(using: .png, properties: [:]) else {
                    throw InstallError("No PNG for \(name)")
                }
                try png.write(to: out.appendingPathComponent("\(name)-\(suffix)"))
                written += 1
            }
            ui.sidebar.set([], selected: 0)
            ui.stage.show(empty)
            ui.setPrimary(empty.primary, showsWindow: false)
            try write("1-empty")
            ui.stage.dropTarget = true
            try write("2-image-dragged")
            ui.stage.dropTarget = false
            let form = NewMachineForm()
            form.window.appearance = look
            form.chosen.stringValue = "\(Art.fullName(tiger.installerIdentity)) (\(tiger.installerIdentity.build)) for PowerPC · Tiger.iso"
            form.slider.doubleValue = 40
            try write("3-new-machine-disk-size", sheet: form.window.contentView)
            let rows = [row(ready, "Stopped"), row(leopard, "Running", running: true)]
            ui.sidebar.set(rows, selected: 0)
            ui.stage.show(pane(ready, running: false))
            ui.setPrimary("Start Tiger", showsWindow: false)
            try write("4-list-tiger-selected")
            list = ui.sidebar.geometry
            ui.sidebar.set(rows, selected: 1)
            ui.stage.show(pane(leopard, running: true))
            ui.setPrimary("Show Leopard’s Window", showsWindow: true)
            try write("5-list-leopard-running")
            ui.sidebar.set([row(ready, "Stopped"), row(leopard, "Stopped")], selected: 1)
            ui.stage.show(pane(leopard, running: false))
            ui.setPrimary("Start Leopard", showsWindow: false)
            try write("6-leopard-stopped")
            var installing = tiger
            installing.name = "Tiger (new)"
            ui.sidebar.set([row(ready, "Stopped"), row(installing, "Installing: step 1 of 3")], selected: 1)
            var m = pane(installing, running: false)
            m.status = "Installation not finished"
            m.primary = "Continue Installation"
            m.note = "Installation paused. Continue Installation resumes it."
            ui.stage.show(m)
            ui.setPrimary(m.primary, showsWindow: false)
            try write("7-tiger-installing")
            let settings = settingsAlert(installing)
            settings.alert.window.appearance = look
            settings.alert.layout()
            ui.sidebar.set(rows, selected: 0)
            ui.stage.show(pane(ready, running: false))
            try write("8-settings", sheet: settings.alert.window.contentView)
            ui.window.orderOut(nil)
        }
    }
    print("Ptolemy: \(written) snapshots in \(out.path); the list: \(list)")
}
