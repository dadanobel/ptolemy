// SPDX-License-Identifier: GPL-2.0-or-later
// The launcher's window. Outside it is a plain macOS window: toolbar, sidebar, menus and sheets are the system's own, so they
// take the system's material (Liquid Glass on macOS 26 and later), its light and dark appearance, accent colour and
// accessibility settings. Inside, the selected machine's pane (StageView) is drawn here in the manner of that machine's
// system: brushed metal and Aqua pills for Tiger, a dark unified panel with a shelf for Leopard. Everything is drawn by this
// file or comes from assets/launcher-art.py; no system artwork of either Mac OS X is used.
import AppKit
import QuartzCore

/* The accessibility display options: the system's, or the ones a snapshot asks for (Snapshot.swift). */
enum Env {
    static var transparencyOff: Bool?, contrastUp: Bool?, motionOff: Bool?
    static var reduceTransparency: Bool {
        transparencyOff ?? NSWorkspace.shared.accessibilityDisplayShouldReduceTransparency
    }
    static var increaseContrast: Bool { contrastUp ?? NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast }
    static var reduceMotion: Bool { motionOff ?? NSWorkspace.shared.accessibilityDisplayShouldReduceMotion }
}

enum Guest {
    case tiger, leopard
    init(_ identity: InstallerIdentity) { self = identity.system == "Leopard" ? .leopard : .tiger }
}

enum Art {
    static func image(_ name: String) -> NSImage? {
        Bundle.main.url(forResource: name, withExtension: nil).flatMap { NSImage(contentsOf: $0) }
    }
    static let metal = image("launcher-metal.jpg"), mark = image("mark-glass.png"),
        tigerTile = image("tile-tiger.png"), leopardTile = image("tile-leopard.png")
    static func tile(_ guest: Guest) -> NSImage? { guest == .leopard ? leopardTile : tigerTile }
    /* Inside the stage: Lucida Grande, the system font of both guests (it is still installed on macOS), else the system's. */
    static func font(_ size: CGFloat, bold: Bool = false) -> NSFont {
        NSFont(name: bold ? "LucidaGrande-Bold" : "LucidaGrande", size: size)
            ?? (bold ? .boldSystemFont(ofSize: size) : .systemFont(ofSize: size))
    }
    static func rgb(_ v: UInt32, _ a: CGFloat = 1) -> NSColor {
        NSColor(
            srgbRed: CGFloat(v >> 16 & 255) / 255, green: CGFloat(v >> 8 & 255) / 255, blue: CGFloat(v & 255) / 255,
            alpha: a)
    }
    /* "1.0 beta 5 · dev11": the bundle's version and the build label that tools/build-product.sh writes (PT_PRODUCT_LABEL).
     * The window's subtitle shows it. */
    static let version: String = {
        let info = Bundle.main.infoDictionary ?? [:]
        var s = (info["CFBundleShortVersionString"] as? String ?? "").replacingOccurrences(of: "b", with: " beta ")
        if let l = info["PtolemyBuildLabel"] as? String, !l.isEmpty { s += (s.isEmpty ? "" : " · ") + l }
        return s
    }()
    /* "Mac OS X Tiger 10.4.11": a system's full name, as the list and the pane show it. */
    static func fullName(_ identity: InstallerIdentity) -> String { "Mac OS X \(identity.system) \(identity.version)" }
    /* Aspect-fill an image into r (cropped by the current clip). */
    static func fill(_ image: NSImage?, _ r: NSRect, fraction: CGFloat = 1) {
        guard let i = image, i.size.width > 0, i.size.height > 0 else { return }
        let k = max(r.width / i.size.width, r.height / i.size.height)
        let w = i.size.width * k
        let h = i.size.height * k
        i.draw(
            in: NSRect(x: r.midX - w / 2, y: r.midY - h / 2, width: w, height: h), from: .zero, operation: .sourceOver,
            fraction: fraction, respectFlipped: true, hints: nil)
    }
    static func symbol(_ name: String, _ size: CGFloat, _ color: NSColor) -> NSImage? {
        let c = NSImage.SymbolConfiguration(pointSize: size, weight: .regular).applying(
            NSImage.SymbolConfiguration(paletteColors: [color]))
        return NSImage(systemSymbolName: name, accessibilityDescription: nil)?.withSymbolConfiguration(c)
    }
}

/* A floating panel over the window's content: the system's glass on macOS 26 and later, a visual-effect view before, and
 * a plain opaque view when a snapshot asks for reduced transparency (the system does that itself for the first two).
 * NSGlassEffectView is reached by name, so the same source builds with the macOS 15 SDK. content fills the panel. */
enum GlassPanel {
    static func make(_ content: NSView, radius: CGFloat) -> NSView {
        if Env.transparencyOff == true {
            let v = NSBox()
            v.boxType = .custom
            v.borderWidth = 1
            v.borderColor = .separatorColor
            v.cornerRadius = radius
            v.fillColor = .windowBackgroundColor
            v.contentViewMargins = .zero
            v.contentView = content
            return v
        }
        if let type = NSClassFromString("NSGlassEffectView") as? NSObject.Type, let glass = type.init() as? NSView,
            glass.responds(to: NSSelectorFromString("setContentView:")),
            glass.responds(to: NSSelectorFromString("setCornerRadius:"))
        {
            glass.setValue(radius, forKey: "cornerRadius")
            glass.setValue(content, forKey: "contentView")
            return glass
        }
        let v = NSVisualEffectView()
        v.material = .hudWindow
        v.blendingMode = .withinWindow
        v.state = .active
        v.wantsLayer = true
        v.layer?.cornerRadius = radius
        v.layer?.masksToBounds = true
        content.frame = v.bounds
        content.autoresizingMask = [.width, .height]
        v.addSubview(content)
        return v
    }
}

/* The stage's colours: a guest system in a light or dark appearance, with the contrast and transparency options. */
struct Look {
    let guest: Guest, dark: Bool, contrast: Bool, opaque: Bool
    var text: NSColor {
        guest == .tiger && !dark ? Art.rgb(contrast ? 0x000000 : 0x1c1e21) : Art.rgb(contrast ? 0xffffff : 0xf0f1f3)
    }
    var dim: NSColor {
        if contrast { return text }
        switch (guest, dark) {
        case (.tiger, false): return Art.rgb(0x4a525d)
        case (.tiger, true): return Art.rgb(0xa9b0ba)
        case (.leopard, _): return Art.rgb(0xc3c8cf)
        }
    }
    var accent: NSColor {
        switch (guest, dark) {
        case (.tiger, false): return Art.rgb(contrast ? 0x0b3a78 : 0x245a9c)
        case (.tiger, true): return Art.rgb(0x9cc8ff)
        case (.leopard, _): return Art.rgb(0xa9d0ff)
        }
    }
    /* The well that holds the text: Tiger's inset white content area, Leopard's darker inset. */
    var wellFill: NSColor {
        switch (guest, dark) {
        case (.tiger, false): return Art.rgb(0xf5f6f8, opaque ? 1 : 0.92)
        case (.tiger, true): return Art.rgb(0x1b1d20, opaque ? 1 : 0.88)
        case (.leopard, false): return opaque ? Art.rgb(0x3d4047) : NSColor(white: 0, alpha: 0.34)
        case (.leopard, true): return opaque ? Art.rgb(0x1a1c1f) : NSColor(white: 0, alpha: 0.30)
        }
    }
    var wellEdge: NSColor {
        if contrast { return guest == .tiger && !dark ? .black : .white }
        return guest == .tiger && !dark ? Art.rgb(0x5f6670) : NSColor(white: guest == .tiger ? 0 : 1, alpha: 0.22)
    }
    var rule: NSColor { text.withAlphaComponent(contrast ? 0.6 : 0.14) }
    /* A pill's gradient stops (top, above the middle, below it, bottom), ink and edge. */
    func pill(_ primary: Bool) -> (stops: [UInt32], ink: UInt32, edge: UInt32) {
        if primary {
            return guest == .tiger
                ? ([0xb4dbff, 0x5fa6ee, 0x2f82e2, 0x8fcbff], 0x04203f, 0x1a4d92)
                : ([0x86bbff, 0x4588f0, 0x1c62dc, 0x4f95f6], 0xffffff, 0x0c3a90)
        }
        switch (guest, dark) {
        case (.tiger, false): return ([0xffffff, 0xeef0f3, 0xdfe2e6, 0xf8f9fa], 0x1c1e21, 0x858b94)
        case (.tiger, true): return ([0x62666c, 0x464a4f, 0x3a3d42, 0x4f5358], 0xf0f1f3, 0x0e0f11)
        case (.leopard, _): return ([0x71757c, 0x50545a, 0x40434a, 0x595d64], 0xffffff, 0x101113)
        }
    }
    func paintStage(_ r: NSRect) {
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(rect: r).addClip()
        switch guest {
        case .tiger:
            Art.rgb(dark ? 0x3a3d41 : 0xb9bcc1).setFill()
            r.fill()
            Art.fill(Art.metal, r)
            if dark {
                Art.rgb(0x1e2023, 0.74).setFill()
                r.fill(using: .sourceOver)
            }
            NSGradient(colors: [
                NSColor(white: 1, alpha: dark ? 0.06 : 0.26), NSColor(white: 1, alpha: 0),
                NSColor(white: 0, alpha: dark ? 0.22 : 0.10),
            ])?.draw(in: r, angle: 90)
        case .leopard:
            NSGradient(colors: [Art.rgb(dark ? 0x3b3e44 : 0x767b84), Art.rgb(dark ? 0x1d1f23 : 0x484c54)])?.draw(
                in: r, angle: 90)
        }
        NSGraphicsContext.restoreGraphicsState()
    }
}

/* What the stage shows; PtolemyApp fills it from the selected machine. */
struct StageModel {
    var guest: Guest?  // nil: no machine is selected
    var status = "", name = "", system = "", specs = "", note = "", primary = ""
    var primaryEnabled = true, actionsEnabled = false, busy = false
}
struct RowModel {
    var guest: Guest?
    var name = "", system = "", state = ""
    var running = false
}

/* A capsule button in the guest's manner: Aqua's gloss with a hard line across the middle. primary: the one action that
 * matters. A real NSButton: keyboard, focus ring and VoiceOver are the system's. */
final class PillButton: NSButton {
    let primary: Bool
    var look = Look(guest: .tiger, dark: false, contrast: false, opaque: false) { didSet { needsDisplay = true } }
    init(_ title: String, primary: Bool, target: AnyObject?, action: Selector?) {
        self.primary = primary
        super.init(frame: .zero)
        self.title = title
        self.target = target
        self.action = action
        isBordered = false
        setButtonType(.momentaryPushIn)
        focusRingType = .exterior
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
    override var isFlipped: Bool { true }
    private var capsule: NSBezierPath {
        let b = bounds.insetBy(dx: 0.5, dy: 0.5)
        return NSBezierPath(roundedRect: b, xRadius: b.height / 2, yRadius: b.height / 2)
    }
    override var intrinsicContentSize: NSSize {
        let w = (title as NSString).size(withAttributes: [.font: Art.font(13)]).width
        return NSSize(width: max(ceil(w) + 44, 96), height: 30)
    }
    override var focusRingMaskBounds: NSRect { bounds }
    override func drawFocusRingMask() { capsule.fill() }
    override func draw(_ dirty: NSRect) {
        let b = bounds.insetBy(dx: 0.5, dy: 0.5), p = capsule, c = look.pill(primary)
        guard let g = NSGraphicsContext.current?.cgContext else { return }
        g.saveGState()
        if !isEnabled { g.setAlpha(0.45) }
        g.beginTransparencyLayer(auxiliaryInfo: nil)
        NSGraphicsContext.saveGraphicsState()
        p.addClip()
        NSGradient(colors: c.stops.map { Art.rgb($0) }, atLocations: [0, 0.5, 0.51, 1], colorSpace: .sRGB)?.draw(
            in: b, angle: 90)
        /* The light on the upper half: a soft lozenge under the top edge. */
        let shine = NSRect(x: b.minX + b.height * 0.3, y: b.minY + 1.5, width: b.width - b.height * 0.6, height: b.height * 0.42)
        NSGradient(colors: [NSColor(white: 1, alpha: look.contrast ? 0 : 0.62), NSColor(white: 1, alpha: 0.04)])?.draw(
            in: NSBezierPath(roundedRect: shine, xRadius: shine.height / 2, yRadius: shine.height / 2), angle: 90)
        if isHighlighted {
            NSColor(white: 0, alpha: 0.22).setFill()
            b.fill(using: .sourceOver)
        }
        NSGraphicsContext.restoreGraphicsState()
        (look.contrast ? look.wellEdge : Art.rgb(c.edge)).setStroke()
        p.lineWidth = look.contrast ? 1.5 : 1
        p.stroke()
        let shadow = NSShadow()
        shadow.shadowColor = c.ink == 0xffffff ? NSColor(white: 0, alpha: 0.45) : NSColor(white: 1, alpha: 0.5)
        shadow.shadowOffset = NSSize(width: 0, height: c.ink == 0xffffff ? 1 : -1)
        shadow.shadowBlurRadius = 0
        let a: [NSAttributedString.Key: Any] = [.font: Art.font(13), .foregroundColor: Art.rgb(c.ink), .shadow: shadow]
        let s = (title as NSString).size(withAttributes: a)
        (title as NSString).draw(
            at: NSPoint(x: bounds.midX - s.width / 2, y: bounds.midY - s.height / 2 - 0.5), withAttributes: a)
        g.endTransparencyLayer()
        g.restoreGState()
    }
}

/* One action in the dock: a symbol over its caption. */
final class DockItem: NSButton {
    let symbol: String
    var look = Look(guest: .tiger, dark: false, contrast: false, opaque: false) { didSet { needsDisplay = true } }
    init(_ title: String, symbol: String, target: AnyObject?, action: Selector?) {
        self.symbol = symbol
        super.init(frame: .zero)
        self.title = title
        self.target = target
        self.action = action
        isBordered = false
        setButtonType(.momentaryPushIn)
        focusRingType = .exterior
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
    override var isFlipped: Bool { true }
    override var focusRingMaskBounds: NSRect { bounds.insetBy(dx: 4, dy: 4) }
    override func drawFocusRingMask() {
        NSBezierPath(roundedRect: bounds.insetBy(dx: 4, dy: 4), xRadius: 6, yRadius: 6).fill()
    }
    var symbolRect: NSRect { NSRect(x: bounds.midX - 22, y: 2, width: 44, height: 40) }
    func image(_ alpha: CGFloat = 1) -> NSImage? { Art.symbol(symbol, 30, look.text.withAlphaComponent(alpha)) }
    /// Where the symbol is drawn: centred in symbolRect, never scaled up. Its foot is where it stands on Leopard's shelf.
    func symbolFrame(_ i: NSImage) -> NSRect {
        let k = min(symbolRect.width / max(i.size.width, 1), symbolRect.height / max(i.size.height, 1), 1)
        let w = i.size.width * k, h = i.size.height * k
        return NSRect(x: symbolRect.midX - w / 2, y: symbolRect.maxY - h, width: w, height: h)
    }
    override func draw(_ dirty: NSRect) {
        // Switched off: still 3:1 against Leopard's slate (0.38 gave 2.2:1 - grey on grey).
        let f: CGFloat = !isEnabled ? (look.contrast ? 0.75 : 0.55) : isHighlighted ? 0.6 : 1
        if let i = image() {
            i.draw(in: symbolFrame(i), from: .zero, operation: .sourceOver, fraction: f, respectFlipped: true, hints: nil)
        }
        let a: [NSAttributedString.Key: Any] = [.font: Art.font(11), .foregroundColor: look.text.withAlphaComponent(f)]
        let s = (title as NSString).size(withAttributes: a)
        (title as NSString).draw(at: NSPoint(x: bounds.midX - s.width / 2, y: bounds.height - s.height - 3), withAttributes: a)
    }
}

/* The dock of the machine's actions: Tiger's flat translucent strip, or Leopard's shelf with the symbols' reflections. */
final class DockView: NSView {
    let items: [DockItem]
    var look = Look(guest: .tiger, dark: false, contrast: false, opaque: false) {
        didSet {
            items.forEach { $0.look = look }
            needsDisplay = true
        }
    }
    static let itemWidth: CGFloat = 116, height: CGFloat = 84
    init(_ items: [DockItem]) {
        self.items = items
        super.init(frame: .zero)
        items.forEach(addSubview)
        setAccessibilityRole(.toolbar)
        setAccessibilityLabel("Machine actions")
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
    override var isFlipped: Bool { true }
    var fittingWidth: CGFloat { CGFloat(items.count) * DockView.itemWidth + 24 }
    override func layout() {
        super.layout()
        for (i, item) in items.enumerated() {
            item.frame = NSRect(x: 12 + CGFloat(i) * DockView.itemWidth, y: 0, width: DockView.itemWidth, height: bounds.height)
        }
    }
    override func draw(_ dirty: NSRect) {
        let b = bounds.insetBy(dx: 0.5, dy: 0.5)
        if look.guest == .tiger {
            let p = NSBezierPath(roundedRect: b, xRadius: 5, yRadius: 5)
            (look.opaque
                ? Art.rgb(look.dark ? 0x2a2c30 : 0xdfe2e6) : NSColor(white: 1, alpha: look.dark ? 0.08 : 0.34)).setFill()
            p.fill()
            (look.contrast ? look.wellEdge : NSColor(white: 1, alpha: look.dark ? 0.22 : 0.80)).setStroke()
            p.lineWidth = 1
            p.stroke()
            return
        }
        /* The shelf: seen from above, its far edge shorter than the near one; a bright line along the far edge. The
         * symbols stand on it (their feet a little past the far edge, as on Leopard's Dock), their captions are under it. */
        let top = b.minY + 37, foot = b.minY + 64, lean: CGFloat = 20
        let shelf = NSBezierPath()
        shelf.move(to: NSPoint(x: b.minX + lean, y: top))
        shelf.line(to: NSPoint(x: b.maxX - lean, y: top))
        shelf.line(to: NSPoint(x: b.maxX, y: foot))
        shelf.line(to: NSPoint(x: b.minX, y: foot))
        shelf.close()
        if look.opaque {
            Art.rgb(look.dark ? 0x34373c : 0x5a5e66).setFill()
            shelf.fill()
        } else {
            NSGradient(colors: [NSColor(white: 1, alpha: 0.26), NSColor(white: 1, alpha: 0.08)])?.draw(in: shelf, angle: 90)
        }
        NSColor(white: 1, alpha: look.contrast ? 1 : 0.55).setFill()
        NSRect(x: b.minX + lean, y: top, width: b.width - 2 * lean, height: 1).fill(using: .sourceOver)
        if look.contrast || look.opaque { return }
        NSGraphicsContext.saveGraphicsState()
        shelf.addClip()
        for item in items {
            /* The reflection: the symbol again, upside down under its own foot (an image drawn unflipped in a flipped view). */
            guard let i = item.image() else { continue }
            let s = item.symbolFrame(i)
            i.draw(
                in: NSRect(x: item.frame.minX + s.minX, y: s.maxY + 1, width: s.width, height: s.height),
                from: .zero, operation: .sourceOver, fraction: 0.16, respectFlipped: false, hints: nil)
        }
        NSGraphicsContext.restoreGraphicsState()
    }
}

/* The selected machine's pane, in the manner of its system. */
final class StageView: NSView {
    private(set) var model = StageModel()
    var dropTarget = false {
        didSet {
            // In the view tree only while an image is over the window: drawn off screen, hidden glass still shows.
            if dropTarget { addSubview(badge) } else { badge.removeFromSuperview() }
            needsDisplay = true
        }
    }
    let primary: PillButton, dock: DockView
    private let status = NSTextField(labelWithString: ""), name = NSTextField(labelWithString: ""),
        system = NSTextField(labelWithString: ""), specs = NSTextField(labelWithString: ""),
        note = NSTextField(wrappingLabelWithString: ""), sharedHead = NSTextField(labelWithString: "SHARED WITH THE MAC"),
        sharedText = NSTextField(labelWithString: "Clipboard, dropped files, sound"),
        netHead = NSTextField(labelWithString: "NETWORK"), netText = NSTextField(labelWithString: "On · Web gateway on"),
        hint = NSTextField(wrappingLabelWithString: "Drop a Tiger or Leopard install DVD image here"),
        spinner = NSProgressIndicator(), plate = NSImageView()
    private let badge: NSView
    init(target: AnyObject?) {
        primary = PillButton("New Machine…", primary: true, target: target, action: #selector(PtolemyApp.primaryAction))
        dock = DockView([
            DockItem("Settings…", symbol: "gearshape", target: target, action: #selector(PtolemyApp.settings)),
            DockItem("Show in Finder", symbol: "folder", target: target, action: #selector(PtolemyApp.reveal)),
            DockItem("Web Pages…", symbol: "globe", target: target, action: #selector(PtolemyApp.aboutWeb)),
            DockItem("Move to Trash…", symbol: "trash", target: target, action: #selector(PtolemyApp.remove)),
        ])
        let word = NSTextField(labelWithString: "Drop to add a machine")
        word.font = .systemFont(ofSize: 15, weight: .semibold)
        word.alignment = .center
        let holder = NSView(frame: NSRect(x: 0, y: 0, width: 260, height: 44))
        word.frame = NSRect(x: 0, y: 12, width: 260, height: 20)
        word.autoresizingMask = [.width]
        holder.addSubview(word)
        badge = GlassPanel.make(holder, radius: 22)
        super.init(frame: NSRect(x: 0, y: 0, width: 790, height: 680))
        wantsLayer = true
        primary.keyEquivalent = "\r"
        spinner.style = .spinning
        spinner.controlSize = .small
        spinner.isDisplayedWhenStopped = false
        plate.imageScaling = .scaleProportionallyUpOrDown
        plate.setAccessibilityElement(false)
        hint.alignment = .center
        note.maximumNumberOfLines = 2
        [status, name, system, specs, note, sharedHead, sharedText, netHead, netText, hint].forEach {
            $0.lineBreakMode = $0 === note || $0 === hint ? .byWordWrapping : .byTruncatingTail
            addSubview($0)
        }
        [plate, primary, spinner, dock].forEach(addSubview)
        apply(animated: false)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
    override var isFlipped: Bool { true }
    var look: Look {
        Look(
            guest: model.guest ?? .tiger, dark: effectiveAppearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua,
            contrast: Env.increaseContrast, opaque: Env.reduceTransparency)
    }
    func show(_ m: StageModel) {
        let changed = m.guest != model.guest
        model = m
        apply(animated: changed)
    }
    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        apply(animated: false)
    }
    private func apply(animated: Bool) {
        let k = look
        if animated && !Env.reduceMotion && window != nil {  // a short cross-fade when the system, and so the look, changes
            let t = CATransition()
            t.type = .fade
            t.duration = 0.18
            layer?.add(t, forKey: "look")
        }
        func set(_ l: NSTextField, _ s: String, _ size: CGFloat, _ bold: Bool, _ color: NSColor) {
            l.stringValue = s
            l.font = Art.font(size, bold: bold)
            l.textColor = color
        }
        set(status, model.status.uppercased(), 11, true, k.accent)
        set(name, model.name, 26, true, k.text)
        set(system, model.system, 13, false, k.text)
        set(specs, model.specs, 12, false, k.dim)
        set(note, model.note, 11, false, k.dim)
        set(sharedHead, sharedHead.stringValue, 10, true, k.dim)
        set(sharedText, sharedText.stringValue, 12, false, k.text)
        set(netHead, netHead.stringValue, 10, true, k.dim)
        set(netText, netText.stringValue, 12, false, k.text)
        set(hint, hint.stringValue, 12, false, k.dim)
        primary.title = model.primary
        primary.isEnabled = model.primaryEnabled
        primary.look = k
        dock.look = k
        dock.items.forEach { $0.isEnabled = model.actionsEnabled }
        let has = model.guest != nil
        plate.image = model.guest.flatMap(Art.tile)
        plate.isHidden = !has
        hint.isHidden = has
        dock.isHidden = !has
        [sharedHead, sharedText, netHead, netText].forEach { $0.isHidden = !has }
        if model.busy { spinner.startAnimation(nil) } else { spinner.stopAnimation(nil) }
        spinner.appearance = NSAppearance(named: k.guest == .tiger && !k.dark ? .aqua : .darkAqua)
        needsLayout = true
        needsDisplay = true
    }
    /* Under the toolbar the pane goes on (full-size content); its own content - the well and, below it, the dock - sits
     * in the middle of what is left under the safe area. */
    private var well: NSRect {
        let has = model.guest != nil, top = safeAreaInsets.top
        let h: CGFloat = has ? 262 : 204, group = h + (has ? 24 + DockView.height : 0)
        return NSRect(x: 24, y: top + max(20, ((bounds.height - top - group) / 2).rounded() - 10), width: bounds.width - 48, height: h)
    }
    private var side: NSRect {  // the tile, or the place to drop an image
        let w = well
        return NSRect(x: w.maxX - 24 - 132, y: w.minY + 24, width: 132, height: 132)
    }
    override func draw(_ dirty: NSRect) {
        let k = look, w = well.insetBy(dx: 0.5, dy: 0.5)
        k.paintStage(bounds)
        let p = NSBezierPath(roundedRect: w, xRadius: 9, yRadius: 9)
        k.wellFill.setFill()
        p.fill()
        k.wellEdge.setStroke()
        p.lineWidth = k.contrast ? 1.5 : 1
        p.stroke()
        if model.guest != nil {
            k.rule.setFill()
            NSRect(x: w.minX + 24, y: w.minY + 196, width: w.width - 48, height: 1).fill(using: .sourceOver)
        } else {
            let d = NSBezierPath(roundedRect: side.insetBy(dx: 0.5, dy: 0.5), xRadius: 14, yRadius: 14)
            d.setLineDash([5, 4], count: 2, phase: 0)
            k.dim.setStroke()
            d.lineWidth = 1.5
            d.stroke()
        }
        if dropTarget {
            let r = NSBezierPath(roundedRect: bounds.insetBy(dx: 6, dy: 6), xRadius: 12, yRadius: 12)
            NSColor.controlAccentColor.setStroke()
            r.lineWidth = 3
            r.stroke()
        }
    }
    override func layout() {
        super.layout()
        let w = well, s = side, x = w.minX + 24
        let wide = w.width >= 470  // a narrow pane keeps the text and gives up the tile
        let tw = w.width - 48 - (wide ? s.width + 24 : 0)
        plate.frame = s
        plate.isHidden = !wide || model.guest == nil
        hint.frame = s.insetBy(dx: 10, dy: 28)
        hint.isHidden = !wide || model.guest != nil
        status.frame = NSRect(x: x, y: w.minY + 22, width: tw, height: 14)
        name.frame = NSRect(x: x, y: w.minY + 40, width: tw, height: 34)
        system.frame = NSRect(x: x, y: w.minY + 78, width: tw, height: 18)
        specs.frame = NSRect(x: x, y: w.minY + 98, width: tw, height: 16)
        let ps = primary.intrinsicContentSize
        primary.frame = NSRect(x: x, y: w.minY + 126, width: ps.width, height: ps.height)
        spinner.frame = NSRect(x: primary.frame.maxX + 12, y: w.minY + 133, width: 16, height: 16)
        note.frame = NSRect(x: x, y: w.minY + 162, width: w.width - 48, height: 30)
        let half = (w.width - 48) / 2
        sharedHead.frame = NSRect(x: x, y: w.minY + 212, width: half - 12, height: 13)
        sharedText.frame = NSRect(x: x, y: w.minY + 228, width: half - 12, height: 16)
        netHead.frame = NSRect(x: x + half, y: w.minY + 212, width: half - 12, height: 13)
        netText.frame = NSRect(x: x + half, y: w.minY + 228, width: half - 12, height: 16)
        let dw = min(dock.fittingWidth, bounds.width - 48)
        dock.frame = NSRect(x: ((bounds.width - dw) / 2).rounded(), y: w.maxY + 24, width: dw, height: DockView.height)
        badge.frame = NSRect(x: bounds.midX - 130, y: bounds.midY - 22, width: 260, height: 44)
    }
}

/* One machine in the sidebar: its system's tile, its name, the system's full name with the build, its state. */
final class MachineCell: NSTableCellView {
    private let tile = NSImageView(), name = NSTextField(labelWithString: ""), system = NSTextField(labelWithString: ""),
        state = NSTextField(labelWithString: ""), dot = NSImageView()
    override init(frame: NSRect) {
        super.init(frame: frame)
        name.font = .systemFont(ofSize: NSFont.systemFontSize, weight: .semibold)
        system.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        state.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        system.textColor = .secondaryLabelColor
        state.textColor = .secondaryLabelColor
        [name, system, state].forEach { $0.lineBreakMode = .byTruncatingTail }
        tile.imageScaling = .scaleProportionallyUpOrDown
        tile.setAccessibilityElement(false)
        dot.image = NSImage(systemSymbolName: "circle.fill", accessibilityDescription: "Running")
        dot.contentTintColor = .systemGreen
        dot.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 8, weight: .regular)
        let text = NSStackView(views: [name, system, state])
        text.orientation = .vertical
        text.alignment = .leading
        text.spacing = 1
        let row = NSStackView(views: [tile, text, dot])
        row.spacing = 10
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        textField = name
        imageView = tile
        text.setContentHuggingPriority(.defaultLow, for: .horizontal)
        text.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        NSLayoutConstraint.activate([
            tile.widthAnchor.constraint(equalToConstant: 40), tile.heightAnchor.constraint(equalToConstant: 40),
            row.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 4),
            row.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -6),
            row.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
    func show(_ m: RowModel) {
        name.stringValue = m.name
        system.stringValue = m.system
        state.stringValue = m.state
        tile.image = m.guest.flatMap(Art.tile) ?? NSImage(systemSymbolName: "questionmark.square.dashed", accessibilityDescription: nil)
        dot.isHidden = !m.running
    }
}

/* The sidebar: the machines in the system's source list (selection, arrow keys, VoiceOver are the table's own). */
final class SidebarController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    private let table = NSTableView(), scroll = NSScrollView()
    private let foot = NSTextField(wrappingLabelWithString: "Drop a Tiger or Leopard install DVD image onto the window to add a machine.")
    private var rows = [RowModel](), setting = false
    var selected: ((Int) -> Void)?
    /* Snapshots: no sidebar material and no vibrant rows - drawn off screen those come out as a hole in the window. */
    var flat = false
    var geometry: String { "\(table.numberOfRows) rows, table \(table.frame), drawn rows \(table.rows(in: table.visibleRect))" }
    override func loadView() {
        let v = NSView(frame: NSRect(x: 0, y: 0, width: 250, height: 680))
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("machine"))
        column.resizingMask = .autoresizingMask
        table.addTableColumn(column)
        table.headerView = nil
        table.style = flat ? .inset : .sourceList
        if flat { table.backgroundColor = .clear }
        table.rowHeight = 62
        table.allowsEmptySelection = false
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        table.setAccessibilityLabel("Machines")
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.automaticallyAdjustsContentInsets = true
        foot.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        foot.textColor = .secondaryLabelColor
        foot.alignment = .center
        [scroll, foot].forEach {
            $0.translatesAutoresizingMaskIntoConstraints = false
            v.addSubview($0)
        }
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: v.topAnchor), scroll.leadingAnchor.constraint(equalTo: v.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: v.trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: foot.topAnchor, constant: -8),
            foot.leadingAnchor.constraint(equalTo: v.leadingAnchor, constant: 16),
            foot.trailingAnchor.constraint(equalTo: v.trailingAnchor, constant: -16),
            foot.bottomAnchor.constraint(equalTo: v.bottomAnchor, constant: -14),
        ])
        view = v
    }
    func set(_ list: [RowModel], selected index: Int) {
        _ = view
        setting = true
        rows = list
        table.reloadData()
        if list.indices.contains(index) { table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false) }
        setting = false
    }
    func numberOfRows(in tableView: NSTableView) -> Int { rows.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let id = NSUserInterfaceItemIdentifier("cell")
        let cell = tableView.makeView(withIdentifier: id, owner: self) as? MachineCell ?? MachineCell(frame: .zero)
        cell.identifier = id
        cell.show(rows[row])
        return cell
    }
    func tableViewSelectionDidChange(_ notification: Notification) {
        if !setting && table.selectedRow >= 0 { selected?(table.selectedRow) }
    }
}

/* A titled window that stays where it is put, off any screen: snapshots draw it without showing it (Snapshot.swift). */
final class OffscreenWindow: NSWindow {
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect { frameRect }
}

extension NSToolbarItem.Identifier {
    static let ptNew = NSToolbarItem.Identifier("ptolemy.new"), ptPrimary = NSToolbarItem.Identifier("ptolemy.primary"),
        ptSettings = NSToolbarItem.Identifier("ptolemy.settings")
}

/* The window: a toolbar, the sidebar of machines and the stage, in a split view. target: PtolemyApp (nil in snapshots). */
final class MainWindow: NSObject, NSToolbarDelegate {
    let window: NSWindow, sidebar = SidebarController(), stage: StageView, split = NSSplitViewController()
    private weak var target: AnyObject?
    private var primaryItem: NSToolbarItem?
    init(target: AnyObject?, offscreen: Bool = false) {
        self.target = target
        stage = StageView(target: target)
        let frame = NSRect(x: 0, y: 0, width: 1040, height: 680)
        let mask: NSWindow.StyleMask = [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView]
        window =
            offscreen
            ? OffscreenWindow(contentRect: frame, styleMask: mask, backing: .buffered, defer: false)
            : NSWindow(contentRect: frame, styleMask: mask, backing: .buffered, defer: false)
        super.init()
        let content = NSViewController()
        content.view = stage
        sidebar.flat = offscreen
        let side = offscreen ? NSSplitViewItem(viewController: sidebar) : NSSplitViewItem(sidebarWithViewController: sidebar)
        side.minimumThickness = 230
        side.maximumThickness = 340
        let main = NSSplitViewItem(viewController: content)
        main.minimumThickness = 520
        split.addSplitViewItem(side)
        split.addSplitViewItem(main)
        window.title = "Ptolemy"
        window.subtitle = Art.version
        window.toolbarStyle = .unified
        window.isReleasedWhenClosed = false
        window.contentViewController = split
        window.setContentSize(frame.size)
        window.minSize = NSSize(width: 900, height: 600)
        let bar = NSToolbar(identifier: "ptolemy.main")
        bar.delegate = self
        bar.displayMode = .iconOnly
        window.toolbar = bar
        window.registerForDraggedTypes([.fileURL])  // the window's delegate receives the drop (PtolemyApp)
    }
    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.toggleSidebar, .sidebarTrackingSeparator, .flexibleSpace, .ptNew, .ptPrimary, .ptSettings]
    }
    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        toolbarDefaultItemIdentifiers(toolbar) + [.space]
    }
    func toolbar(
        _ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool
    ) -> NSToolbarItem? {
        let spec: (label: String, symbol: String, action: Selector)
        switch id {
        case .ptNew: spec = ("New Machine", "plus", #selector(PtolemyApp.newMachine))
        case .ptPrimary: spec = ("Start", "play.fill", #selector(PtolemyApp.primaryAction))
        case .ptSettings: spec = ("Settings", "gearshape", #selector(PtolemyApp.settings))
        default: return nil
        }
        let item = NSToolbarItem(itemIdentifier: id)
        item.label = spec.label
        item.paletteLabel = spec.label
        item.toolTip = spec.label
        item.image = NSImage(systemSymbolName: spec.symbol, accessibilityDescription: spec.label)
        item.isBordered = true
        item.target = target
        item.action = spec.action
        if id == .ptPrimary { primaryItem = item }
        return item
    }
    /* The toolbar's main item says what the stage's button says: Start Tiger, Show Tiger's Window, Continue Installation. */
    func setPrimary(_ title: String, showsWindow: Bool) {
        primaryItem?.label = title
        primaryItem?.toolTip = title
        primaryItem?.image = NSImage(systemSymbolName: showsWindow ? "macwindow" : "play.fill", accessibilityDescription: title)
    }
}

/* The new machine's sheet: install image, name, disk size, in the system's controls. PtolemyApp sets the handlers. */
final class NewMachineForm: NSObject {
    let window: NSWindow
    let name = NSTextField(string: "Tiger"),
        chosen = NSTextField(wrappingLabelWithString: "Choose a Tiger or Leopard install DVD image, or drop it onto the window."),
        diskLabel = NSTextField(labelWithString: "40 GB"),
        slider = NSSlider(value: 40, minValue: 8, maxValue: 120, target: nil, action: nil)
    var choose: (() -> Void)?, create: (() -> Void)?, cancel: (() -> Void)?
    override init() {
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 500, height: 250), styleMask: [.titled], backing: .buffered, defer: false)
        super.init()
        let title = NSTextField(labelWithString: "New Machine")
        title.font = .systemFont(ofSize: NSFont.systemFontSize, weight: .semibold)
        let pick = NSButton(title: "Choose Install DVD…", target: self, action: #selector(pressChoose))
        let no = NSButton(title: "Cancel", target: self, action: #selector(pressCancel))
        let yes = NSButton(title: "Create and Install", target: self, action: #selector(pressCreate))
        yes.keyEquivalent = "\r"
        no.keyEquivalent = "\u{1b}"
        slider.target = self
        slider.action = #selector(slide)
        slider.setAccessibilityLabel("Disk size")
        chosen.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        chosen.textColor = .secondaryLabelColor
        chosen.preferredMaxLayoutWidth = 330
        diskLabel.font = .monospacedDigitSystemFont(ofSize: NSFont.systemFontSize, weight: .regular)
        func head(_ s: String) -> NSTextField {
            let l = NSTextField(labelWithString: s)
            l.alignment = .right
            return l
        }
        let disk = NSStackView(views: [slider, diskLabel])
        disk.spacing = 10
        let source = NSStackView(views: [pick, chosen])
        source.orientation = .vertical
        source.alignment = .leading
        source.spacing = 6
        let grid = NSGridView(views: [[head("Install DVD:"), source], [head("Name:"), name], [head("Disk size:"), disk]])
        grid.rowSpacing = 14
        grid.columnSpacing = 10
        grid.column(at: 0).xPlacement = .trailing
        grid.rowAlignment = .firstBaseline
        name.widthAnchor.constraint(equalToConstant: 240).isActive = true
        slider.widthAnchor.constraint(equalToConstant: 240).isActive = true
        let buttons = NSStackView(views: [no, yes])
        buttons.spacing = 10
        let stack = NSStackView(views: [title, grid, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 16
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 24, bottom: 20, right: 24)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let root = NSView()
        root.addSubview(stack)
        window.contentView = root
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            stack.topAnchor.constraint(equalTo: root.topAnchor), stack.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            buttons.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -24),
        ])
        window.initialFirstResponder = name
    }
    @objc private func pressChoose() { choose?() }
    @objc private func pressCreate() { create?() }
    @objc private func pressCancel() { cancel?() }
    @objc private func slide() {
        slider.doubleValue = slider.doubleValue.rounded()
        diskLabel.stringValue = "\(Int(slider.doubleValue)) GB"
    }
}

/* A boot display mode as the settings name it: "1280 × 800 (16:10)"; "screen" fits the Mac's screen. */
func displayName(_ mode: String) -> String {
    let p = mode.split(separator: "x").compactMap { Int($0) }
    guard p.count == 2 else { return "Fit the Screen" }
    let ratio = p[0] * 3 == p[1] * 4 ? "4:3" : p[0] * 10 == p[1] * 16 ? "16:10" : "16:9"
    return "\(p[0]) × \(p[1]) (\(ratio))"
}

/* The machine's settings: an alert with processor, memory, display, picture and disk. Returns the alert and its controls. */
func settingsAlert(_ item: Installation) -> (
    alert: NSAlert, cpu: NSPopUpButton, ram: NSPopUpButton, disk: NSTextField, display: NSPopUpButton, upscale: NSButton,
    scale: NSPopUpButton
) {
    let alert = NSAlert()
    alert.messageText = "Settings for \(item.name)"
    alert.informativeText = "\(Art.fullName(item.shownIdentity)) (\(item.shownIdentity.build))"
    alert.addButton(withTitle: "Save")
    alert.addButton(withTitle: "Cancel")
    if item.phase != .ready { alert.addButton(withTitle: "Resume Installer") }
    let cpu = NSPopUpButton()
    cpu.addItems(withTitles: cpuChoices(item).map { "PowerPC " + $0.name })
    cpu.selectItem(at: cpuChoices(item).firstIndex(where: { $0.id == item.cpu }) ?? 0)
    let ram = NSPopUpButton()
    ram.addItems(withTitles: stride(from: 512, through: 2048, by: 64).map(String.init))
    ram.selectItem(withTitle: String(item.ram))
    let display = NSPopUpButton()
    display.addItems(withTitles: displayModes.map(displayName))
    display.selectItem(at: displayModes.firstIndex(of: item.display ?? "screen") ?? 0)
    let upscale = NSButton(checkboxWithTitle: "Smooth Scaling (MetalFX)", target: nil, action: nil)
    upscale.state = item.upscale ?? true ? .on : .off
    let scale = NSPopUpButton()
    scale.addItems(withTitles: renderScales.map { $0 == "1" ? "As the System Draws" : "\($0)× Sharper" })
    scale.selectItem(at: renderScales.firstIndex(of: item.renderScale ?? defaultRenderScale) ?? 0)
    let disk = NSTextField(string: String(item.diskGiB))
    disk.widthAnchor.constraint(equalToConstant: 64).isActive = true
    func head(_ s: String) -> NSTextField {
        let l = NSTextField(labelWithString: s)
        l.alignment = .right
        return l
    }
    let note = NSTextField(labelWithString: "GB (it can only grow, up to 120)")
    note.textColor = .secondaryLabelColor
    let size = NSStackView(views: [disk, note])
    size.spacing = 6
    let mb = NSTextField(labelWithString: "MB")
    mb.textColor = .secondaryLabelColor
    let memory = NSStackView(views: [ram, mb])
    memory.spacing = 6
    let grid = NSGridView(views: [
        [head("Processor:"), cpu], [head("Memory:"), memory], [head("Display:"), display], [head("Picture:"), upscale],
        [head("3D Graphics:"), scale],
        [head("Disk:"), size],
    ])
    grid.rowSpacing = 10
    grid.columnSpacing = 8
    grid.column(at: 0).xPlacement = .trailing
    grid.rowAlignment = .firstBaseline
    grid.frame = NSRect(origin: .zero, size: grid.fittingSize)
    cpu.setAccessibilityLabel("Processor")
    ram.setAccessibilityLabel("Memory in megabytes")
    disk.setAccessibilityLabel("Disk size in gigabytes")
    display.setAccessibilityLabel("Display mode at startup")
    upscale.setAccessibilityLabel("Smooth a picture shown larger than the guest draws it, with MetalFX")
    scale.setAccessibilityLabel("How much larger than the system asks its 3D graphics are drawn")
    alert.accessoryView = grid
    return (alert, cpu, ram, disk, display, upscale, scale)
}

/* The menu bar: the commands of the window with their shortcuts, and the standard application, edit, view and window menus. */
func mainMenu(target: AnyObject) -> NSMenu {
    func item(_ title: String, _ action: Selector?, _ key: String = "", _ mask: NSEvent.ModifierFlags = .command, to: AnyObject? = nil)
        -> NSMenuItem
    {
        let i = NSMenuItem(title: title, action: action, keyEquivalent: key)
        i.keyEquivalentModifierMask = mask
        i.target = to
        return i
    }
    func menu(_ title: String, _ items: [NSMenuItem]) -> NSMenuItem {
        let top = NSMenuItem()
        top.title = title
        top.submenu = NSMenu(title: title)
        items.forEach { top.submenu?.addItem($0) }
        return top
    }
    let bar = NSMenu()
    bar.addItem(
        menu(
            "Ptolemy",
            [
                item("About Ptolemy", #selector(NSApplication.orderFrontStandardAboutPanel(_:))), .separator(),
                item("Settings…", #selector(PtolemyApp.settings), ",", to: target), .separator(),
                item("Hide Ptolemy", #selector(NSApplication.hide(_:)), "h"),
                item("Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option]),
                item("Show All", #selector(NSApplication.unhideAllApplications(_:))), .separator(),
                item("Quit Ptolemy", #selector(NSApplication.terminate(_:)), "q"),
            ]))
    bar.addItem(
        menu(
            "File",
            [
                item("New Machine…", #selector(PtolemyApp.newMachine), "n", to: target), .separator(),
                item("Show in Finder", #selector(PtolemyApp.reveal), "r", [.command, .shift], to: target),
                item("Move to Trash…", #selector(PtolemyApp.remove), "\u{8}", to: target), .separator(),
                item("Close", #selector(NSWindow.performClose(_:)), "w"),
            ]))
    bar.addItem(
        menu(
            "Edit",
            [
                item("Undo", Selector(("undo:")), "z"), item("Redo", Selector(("redo:")), "z", [.command, .shift]),
                .separator(), item("Cut", #selector(NSText.cut(_:)), "x"), item("Copy", #selector(NSText.copy(_:)), "c"),
                item("Paste", #selector(NSText.paste(_:)), "v"),
                item("Select All", #selector(NSText.selectAll(_:)), "a"),
            ]))
    bar.addItem(
        menu(
            "Machine",
            [
                item("Start", #selector(PtolemyApp.primaryAction), "r", to: target), .separator(),
                item("Web Pages…", #selector(PtolemyApp.aboutWeb), to: target),
            ]))
    bar.addItem(
        menu(
            "View",
            [
                item("Show Sidebar", #selector(NSSplitViewController.toggleSidebar(_:)), "s", [.command, .control]),
                item("Show Toolbar", #selector(NSWindow.toggleToolbarShown(_:)), "t", [.command, .option]), .separator(),
                item("Enter Full Screen", #selector(NSWindow.toggleFullScreen(_:)), "f", [.command, .control]),
            ]))
    let windows = menu(
        "Window",
        [item("Minimize", #selector(NSWindow.performMiniaturize(_:)), "m"), item("Zoom", #selector(NSWindow.performZoom(_:)))])
    bar.addItem(windows)
    NSApp.windowsMenu = windows.submenu
    return bar
}
