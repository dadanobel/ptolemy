// SPDX-License-Identifier: GPL-2.0-or-later
// The launcher's look ("Tiger Glass"). Classic AppKit drawing (no layers, no system
// glass), so the same view renders off screen for checks (--render-launcher). Lucida Grande, the Puma/Tiger system font,
// 13 pt text and 11 pt bold labels with engraved shadows; art from Resources (fur, brushed metal, the Ptolemy mark in glass).
import AppKit

typealias Machine=(name: String,detail: String,plate: String,running: Bool,dashed: Bool)
enum Art {
    static func image(_ name: String)->NSImage? {Bundle.main.url(forResource:name,withExtension:nil).flatMap{NSImage(contentsOf:$0)}}
    static let fur=image("launcher-fur.jpg"),metal=image("launcher-metal.jpg"),mark=image("mark-glass.png")
    static func font(_ size: CGFloat,bold: Bool=false)->NSFont {NSFont(name:bold ? "LucidaGrande-Bold":"LucidaGrande",size:size) ?? (bold ? .boldSystemFont(ofSize:size):.systemFont(ofSize:size))}
    static func rgb(_ v: UInt32,_ a: CGFloat=1)->NSColor {NSColor(srgbRed:CGFloat(v>>16&255)/255,green:CGFloat(v>>8&255)/255,blue:CGFloat(v&255)/255,alpha:a)}
    static let text=rgb(0xefebe4),dim=rgb(0xa9a39a),amber=rgb(0xffb35c)
    /* Engraved text: on the dark ground a black shadow one pixel above, as Tiger's metal windows. */
    static func attrs(_ size: CGFloat,bold: Bool=false,color: NSColor=text,light: Bool=false)->[NSAttributedString.Key:Any] {
        let s=NSShadow();s.shadowColor=light ? NSColor(white:1,alpha:0.45):.black;s.shadowOffset=NSSize(width:0,height:light ? -1:1);s.shadowBlurRadius=0
        return [.font:font(size,bold:bold),.foregroundColor:color,.shadow:s]
    }
    static func label(_ s: String,_ size: CGFloat,bold: Bool=false,color: NSColor=text)->NSTextField {
        let l=NSTextField(labelWithAttributedString:NSAttributedString(string:s,attributes:attrs(size,bold:bold,color:color)));l.lineBreakMode = .byTruncatingTail;return l
    }
    static func set(_ l: NSTextField,_ s: String,_ size: CGFloat,bold: Bool=false,color: NSColor=text){l.attributedStringValue=NSAttributedString(string:s,attributes:attrs(size,bold:bold,color:color))}
    static func draw(_ image: NSImage?,in r: NSRect,fraction: CGFloat=1){image?.draw(in:r,from:.zero,operation:.sourceOver,fraction:fraction,respectFlipped:true,hints:nil)}
    /* Aspect-fill an image into r (cropped by the current clip). */
    static func fill(_ image: NSImage?,_ r: NSRect,fraction: CGFloat=1){
        guard let i=image,i.size.width>0,i.size.height>0 else{return};let k=max(r.width/i.size.width,r.height/i.size.height)
        let w=i.size.width*k,h=i.size.height*k;draw(i,in:NSRect(x:r.midX-w/2,y:r.midY-h/2,width:w,height:h),fraction:fraction)
    }
}

/* Gel capsule: amber for the one action that matters, dark for the rest. */
final class Capsule: NSButton {
    enum Kind {case amber,dark}
    let kind: Kind
    init(_ title: String,_ kind: Kind,target: AnyObject?,action: Selector?){self.kind=kind;super.init(frame:.zero);self.title=title;self.target=target;self.action=action;isBordered=false;font=Art.font(13);setButtonType(.momentaryPushIn)}
    required init?(coder: NSCoder){fatalError("init(coder:) is not used")}
    override var intrinsicContentSize: NSSize {let w=(title as NSString).size(withAttributes:[.font:Art.font(13)]).width;return NSSize(width:ceil(w)+(kind == .amber ? 56:34),height:kind == .amber ? 40:32)}
    override func draw(_ dirty: NSRect){
        let b=bounds.insetBy(dx:0.5,dy:0.5),p=NSBezierPath(roundedRect:b,xRadius:b.height/2,yRadius:b.height/2)
        let stops: [UInt32]=kind == .amber ? [0xffd29b,0xf6a64c,0xe6821e,0xf7b064]:[0x5a5752,0x3a3835,0x2a2826,0x3c3a36]
        NSGraphicsContext.saveGraphicsState();p.addClip()
        NSGradient(colors:stops.map{Art.rgb($0)},atLocations:[0,0.48,0.52,1],colorSpace:.sRGB)?.draw(in:b,angle:isFlipped ? 90:-90)
        if isHighlighted{NSColor(white:0,alpha:0.18).setFill();b.fill(using:.sourceOver)}
        NSColor(white:1,alpha:kind == .amber ? 0.7:0.25).setFill();NSRect(x:b.minX+b.height/2,y:isFlipped ? b.minY+1:b.maxY-2,width:b.width-b.height,height:1).fill(using:.sourceOver)
        NSGraphicsContext.restoreGraphicsState()
        (kind == .amber ? Art.rgb(0x8a4a0c):NSColor.black).setStroke();p.lineWidth=1;p.stroke()
        let a=Art.attrs(13,color:kind == .amber ? Art.rgb(0x1e1206):Art.text,light:kind == .amber),s=(title as NSString).size(withAttributes:a)
        (title as NSString).draw(at:NSPoint(x:bounds.midX-s.width/2,y:bounds.midY-s.height/2),withAttributes:a)
        if !isEnabled{NSColor(white:0,alpha:0.5).setFill();p.fill()}
    }
}

/* Text link in amber (Settings…, Show in Finder). */
func linkButton(_ title: String,target: AnyObject?,action: Selector?)->NSButton {
    let b=NSButton(title:title,target:target,action:action);b.isBordered=false
    b.attributedTitle=NSAttributedString(string:title,attributes:[.font:Art.font(12),.foregroundColor:Art.amber,.underlineStyle:NSUnderlineStyle.single.rawValue]);return b
}

/* Matte glass: a translucent fill over the dark ground, a light hairline edge and a highlight along the top. */
class Glass: NSView {
    var radius: CGFloat=14,fill=NSColor(white:0.07,alpha:0.80),edge: CGFloat=0.10,metal: CGFloat=0
    override var isFlipped: Bool {true}
    override func draw(_ dirty: NSRect){
        let b=bounds.insetBy(dx:0.5,dy:0.5),p=NSBezierPath(roundedRect:b,xRadius:radius,yRadius:radius)
        NSGraphicsContext.saveGraphicsState();p.addClip()
        if metal>0{Art.fill(Art.metal,bounds,fraction:metal)}
        fill.setFill();bounds.fill(using:.sourceOver)
        NSColor(white:1,alpha:edge+0.06).setFill();NSRect(x:b.minX+radius,y:b.minY+1,width:b.width-2*radius,height:1).fill(using:.sourceOver)
        NSGraphicsContext.restoreGraphicsState()
        NSColor(white:1,alpha:edge).setStroke();p.lineWidth=1;p.stroke()
    }
}

/* One machine in the sidebar: a brushed-metal plate with its system version, name, state. */
final class MachineRow: NSControl {
    var name="",detail="",plate="",selected=false,running=false,dashed=false
    override var isFlipped: Bool {true}
    override func mouseDown(with event: NSEvent){sendAction(action,to:target)}
    override func draw(_ dirty: NSRect){
        let b=bounds.insetBy(dx:0.5,dy:0.5)
        if selected{let p=NSBezierPath(roundedRect:b,xRadius:10,yRadius:10)
            NSGraphicsContext.saveGraphicsState();p.addClip()
            NSGradient(colors:[0xffc27a,0xf39a3a,0xe07f1c,0xf2a24e].map{Art.rgb($0)},atLocations:[0,0.5,0.51,1],colorSpace:.sRGB)?.draw(in:b,angle:90)
            NSColor(white:1,alpha:0.55).setFill();NSRect(x:b.minX+10,y:b.minY+1,width:b.width-20,height:1).fill(using:.sourceOver)
            NSGraphicsContext.restoreGraphicsState();Art.rgb(0xa85a10).setStroke();p.lineWidth=1;p.stroke()}
        let plateRect=NSRect(x:10,y:(bounds.height-32)/2,width:44,height:32),pp=NSBezierPath(roundedRect:plateRect,xRadius:5,yRadius:5)
        if dashed{Art.rgb(0x77716a).setStroke();pp.setLineDash([3,2],count:2,phase:0);pp.stroke()}
        else{NSGraphicsContext.saveGraphicsState();pp.addClip();Art.fill(Art.metal,plateRect);NSGraphicsContext.restoreGraphicsState()
            (selected ? Art.rgb(0x6b4a24):NSColor.black).setStroke();pp.stroke()}
        let pa=dashed ? Art.attrs(10,color:Art.dim):Art.attrs(plate.count>4 ? 10:11,bold:true,color:Art.rgb(0x2a2a2a),light:true),ps=(plate as NSString).size(withAttributes:pa)
        (plate as NSString).draw(at:NSPoint(x:plateRect.midX-ps.width/2,y:plateRect.midY-ps.height/2),withAttributes:pa)
        let ink=selected ? Art.rgb(0x1e1206):Art.text,w=bounds.width-64-(running ? 22:8)
        (name as NSString).draw(with:NSRect(x:64,y:9,width:w,height:17),options:[.usesLineFragmentOrigin,.truncatesLastVisibleLine],attributes:Art.attrs(13,bold:true,color:ink,light:selected))
        (detail as NSString).draw(with:NSRect(x:64,y:27,width:w,height:15),options:[.usesLineFragmentOrigin,.truncatesLastVisibleLine],attributes:Art.attrs(11,color:selected ? ink:Art.dim,light:selected))
        if running{let d=NSRect(x:bounds.width-20,y:bounds.midY-4,width:8,height:8);Art.rgb(0x3fbf2a).setFill();NSBezierPath(ovalIn:d).fill();Art.rgb(0x1d5a12).setStroke();NSBezierPath(ovalIn:d).stroke()}
    }
}

/* The selected machine: black ground, fur at the right edge, the mark in its own light. */
final class Hero: NSView {
    override var isFlipped: Bool {true}
    override func draw(_ dirty: NSRect){
        let b=bounds.insetBy(dx:0.5,dy:0.5),p=NSBezierPath(roundedRect:b,xRadius:14,yRadius:14)
        NSGraphicsContext.saveGraphicsState();p.addClip()
        Art.rgb(0x030303).setFill();bounds.fill()
        let fur=NSRect(x:bounds.width*0.54,y:0,width:bounds.width*0.46,height:bounds.height);Art.fill(Art.fur,fur)
        NSGradient(colors:[Art.rgb(0x030303),Art.rgb(0x030303),Art.rgb(0x030303,0.86),Art.rgb(0x030303,0.35),Art.rgb(0x030303,0.12)],atLocations:[0,0.54,0.70,0.88,1],colorSpace:.sRGB)?.draw(in:bounds,angle:0)
        let h=min(bounds.height*0.62,300),markRect=NSRect(x:bounds.width-140-h*0.9,y:bounds.midY-h/2,width:h*0.9,height:h)
        NSGradient(colors:[NSColor(white:1,alpha:0.09),NSColor(white:1,alpha:0.03),NSColor(white:1,alpha:0)],atLocations:[0,0.4,1],colorSpace:.sRGB)?
            .draw(in:NSBezierPath(ovalIn:markRect.insetBy(dx:-h*0.45,dy:-h*0.35)),relativeCenterPosition:.zero)
        let glow=NSShadow();glow.shadowColor=NSColor(white:1,alpha:0.18);glow.shadowBlurRadius=18;glow.set();Art.draw(Art.mark,in:markRect)
        NSGraphicsContext.restoreGraphicsState()
        NSColor(white:1,alpha:0.10).setStroke();p.lineWidth=1;p.stroke()
    }
}

/* The whole launcher: header strip, machines, the selected machine's card, three tiles. Built by PtolemyApp. */
final class LauncherView: NSView {
    let sidebar=Glass(),hero=Hero(),card=Glass(),tiles=[Glass(),Glass(),Glass()]
    let status=Art.label("",11,bold:true,color:Art.amber),name=Art.label("",34,bold:true),specs=Art.label("",12,color:Art.rgb(0xcfc9bf)),note=Art.label("",11,color:Art.dim)
    let primary: Capsule,newMachine: Capsule,spinner=NSProgressIndicator(),title=Art.label("Ptolemy",13),netState=Art.label("Network on · Web gateway on",11,color:Art.dim)
    var rows=[MachineRow](),tileViews=[[NSControl]]()
    private let select: Selector
    private weak var target: AnyObject?
    init(target: AnyObject,primary: Selector,newMachine: Selector,select: Selector,tiles content: [[NSControl]]){
        self.primary=Capsule("Start Tiger",.amber,target:target,action:primary);self.newMachine=Capsule("New Machine…",.dark,target:target,action:newMachine);tileViews=content
        self.select=select;self.target=target
        super.init(frame:NSRect(x:0,y:0,width:1120,height:720))
        sidebar.metal=0.10;sidebar.fill=NSColor(srgbRed:0.063,green:0.059,blue:0.055,alpha:0.80)
        card.fill=NSColor(srgbRed:0.086,green:0.082,blue:0.078,alpha:0.80);card.edge=0.14;card.radius=16
        for t in tiles{t.fill=NSColor(white:1,alpha:0.05);t.radius=12;addSubview(t)}
        for (t,views) in zip(tiles,content){views.forEach{t.addSubview($0)}}
        [sidebar,hero,title,netState].forEach{addSubview($0)};hero.addSubview(card)
        [status,name,specs,self.primary,note,spinner].forEach{card.addSubview($0)}
        sidebar.addSubview(Art.label("Machines",11,bold:true,color:Art.dim));sidebar.addSubview(self.newMachine)
        let hint=Art.label("or drop a Tiger install DVD image onto this window",11,color:Art.rgb(0x8f8980));hint.alignment = .center;hint.maximumNumberOfLines=2;hint.lineBreakMode = .byWordWrapping;sidebar.addSubview(hint)
        spinner.style = .spinning;spinner.controlSize = .small;spinner.isDisplayedWhenStopped=false;spinner.appearance=NSAppearance(named:.darkAqua)
    }
    required init?(coder: NSCoder){fatalError("init(coder:) is not used")}
    override var isFlipped: Bool {true}
    func setMachines(_ list: [Machine],selected: Int){
        while rows.count<list.count{let r=MachineRow();r.target=target;r.action=select;rows.append(r);sidebar.addSubview(r)}
        while rows.count>list.count{rows.removeLast().removeFromSuperview()}
        for (i,(r,m)) in zip(rows,list).enumerated(){r.tag=i;r.name=m.name;r.detail=m.detail;r.plate=m.plate;r.running=m.running;r.dashed=m.dashed;r.selected=i==selected;r.needsDisplay=true}
        needsLayout=true
    }
    override func draw(_ dirty: NSRect){
        Art.rgb(0x040404).setFill();bounds.fill()
        let header=NSRect(x:0,y:0,width:bounds.width,height:50);NSGraphicsContext.saveGraphicsState();NSBezierPath(rect:header).addClip();Art.fill(Art.metal,header,fraction:0.30);NSGraphicsContext.restoreGraphicsState()
        NSGradient(colors:[Art.rgb(0x282624,0.55),Art.rgb(0x080808,0.85)])?.draw(in:header,angle:90)
        NSColor.black.setFill();NSRect(x:0,y:49,width:bounds.width,height:1).fill();NSColor(white:1,alpha:0.06).setFill();NSRect(x:0,y:50,width:bounds.width,height:1).fill()
        if let m=Art.mark{let w=18*m.size.width/max(m.size.height,1);Art.draw(m,in:NSRect(x:title.frame.minX-w-6,y:16,width:w,height:18))}
    }
    override func layout(){
        super.layout();let W=bounds.width,H=bounds.height
        title.sizeToFit();title.setFrameOrigin(NSPoint(x:(W-title.frame.width)/2+12,y:17))
        netState.sizeToFit();netState.setFrameOrigin(NSPoint(x:W-netState.frame.width-16,y:18))
        sidebar.frame=NSRect(x:12,y:62,width:252,height:H-74)
        let s=sidebar.subviews;s[0].frame=NSRect(x:18,y:12,width:200,height:16)
        for (i,r) in rows.enumerated(){r.frame=NSRect(x:8,y:34+CGFloat(i)*52,width:236,height:48)}
        newMachine.frame=NSRect(x:14,y:sidebar.bounds.height-88,width:224,height:32);s[2].frame=NSRect(x:14,y:sidebar.bounds.height-50,width:224,height:34)
        let tilesY=H-12-62,mainX: CGFloat=276,mainW=W-288
        hero.frame=NSRect(x:mainX,y:62,width:mainW,height:tilesY-12-62)
        let share: [CGFloat]=[0.30,0.30,0.40];var tx=mainX   /* the Machine tile holds three links */
        for (i,t) in tiles.enumerated(){let tw=(mainW-24)*share[i];t.frame=NSRect(x:tx,y:tilesY,width:tw,height:62);tx+=tw+12
            var x: CGFloat=14;for (k,v) in tileViews[i].enumerated(){v.sizeToFit();if k==0{v.setFrameOrigin(NSPoint(x:14,y:11))}else{v.setFrameOrigin(NSPoint(x:x,y:31));x+=v.frame.width+10}}}
        card.frame=NSRect(x:32,y:max(16,(hero.bounds.height-212)/2),width:min(420,hero.bounds.width*0.52),height:212)
        status.frame=NSRect(x:26,y:22,width:card.bounds.width-52,height:16);name.frame=NSRect(x:26,y:42,width:card.bounds.width-52,height:42)
        specs.frame=NSRect(x:26,y:88,width:card.bounds.width-52,height:34);specs.maximumNumberOfLines=2;specs.lineBreakMode = .byWordWrapping
        let ps=primary.intrinsicContentSize;primary.frame=NSRect(x:26,y:132,width:ps.width,height:ps.height)
        spinner.frame=NSRect(x:primary.frame.maxX+12,y:144,width:16,height:16)
        note.frame=NSRect(x:26,y:182,width:card.bounds.width-52,height:16)
    }
}
