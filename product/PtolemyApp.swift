// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import UniformTypeIdentifiers

final class InstallerDropView: NSView {
    var selected: ((URL)->Void)?
    override init(frame: NSRect){super.init(frame:frame);registerForDraggedTypes([.fileURL])}
    required init?(coder: NSCoder){fatalError("init(coder:) is not used")}
    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {sender.draggingPasteboard.canReadObject(forClasses:[NSURL.self],options:[.urlReadingFileURLsOnly:true]) ? .copy:[]}
    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard let urls=sender.draggingPasteboard.readObjects(forClasses:[NSURL.self],options:[.urlReadingFileURLsOnly:true]) as? [URL],urls.count==1 else{return false};selected?(urls[0]);return true
    }
}
/* The machines are always listed; the selected one has one large button whose words say what it does now (Start Tiger, Show Tiger's Window,
 * Continue Installation, Finish Setup); a new machine is made in its own sheet. Look: Launcher.swift. */
final class PtolemyApp: NSObject,NSApplicationDelegate,NSWindowDelegate {
    private var window: NSWindow!,library: ProductLibrary!,view: LauncherView!,sheet: NSWindow?,clock: Timer?
    private var urls=[URL](),items=[Installation?](),index=0,installer: URL?,identity: InstallerIdentity?,message=""
    private let name=NSTextField(string:"Tiger"),chosen=NSTextField(wrappingLabelWithString:"Choose the Tiger install DVD image (or drop it here)."),diskLabel=NSTextField(labelWithString:"40 GB")
    private let slider=NSSlider(value:40,minValue:8,maxValue:120,target:nil,action:nil)
    private var links=[NSButton](),busy=false,quitting=false,running: URL?,started: Date?
    private let lock=NSLock();private var process: Process?,cancelled=false
    private var resources: ProductResources {ProductResources(root:Bundle.main.resourceURL!)}
    func applicationDidFinishLaunching(_ n: Notification){
        do{library=try ProductLibrary()}catch{NSAlert(error:error).runModal();NSApp.terminate(nil);return}
        NSApp.setActivationPolicy(.regular);NSApp.appearance=NSAppearance(named:.darkAqua)
        window=NSWindow(contentRect:NSRect(x:0,y:0,width:1120,height:720),styleMask:[.titled,.closable,.miniaturizable,.resizable,.fullSizeContentView],backing:.buffered,defer:false)
        window.title="Ptolemy";window.titleVisibility = .hidden;window.titlebarAppearsTransparent=true;window.backgroundColor = .black
        window.isReleasedWhenClosed=false;window.delegate=self;window.minSize=NSSize(width:900,height:600);window.center()
        let drop=InstallerDropView(frame:NSRect(x:0,y:0,width:1120,height:720));drop.selected={[weak self] url in self?.newMachine();self?.choose(url)}
        view=makeView();view.frame=drop.bounds;view.autoresizingMask=[.width,.height];drop.addSubview(view);window.contentView=drop
        let menu=NSMenu(),app=NSMenuItem();app.submenu=NSMenu();app.submenu?.addItem(withTitle:"Quit Ptolemy",action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q");menu.addItem(app);NSApp.mainMenu=menu
        clock=Timer.scheduledTimer(withTimeInterval:30,repeats:true){[weak self] _ in self?.updateCard()}
        reload();window.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true)
    }
    func makeView()->LauncherView {
        func link(_ t: String,_ a: Selector)->NSButton {let b=linkButton(t,target:self,action:a);links.append(b);return b}
        let tiles: [[NSControl]]=[[Art.label("Shared with Tiger",11,bold:true,color:Art.dim),Art.label("Clipboard, dropped files, sound",12)],
            [Art.label("Network",11,bold:true,color:Art.dim),Art.label("On",12),linkButton("Web Pages in Tiger…",target:self,action:#selector(aboutWeb))],
            [Art.label("Machine",11,bold:true,color:Art.dim),link("Settings…",#selector(settings)),linkButton("Show in Finder",target:self,action:#selector(reveal)),link("Move to Trash…",#selector(remove))]]
        return LauncherView(target:self,primary:#selector(primaryAction),newMachine:#selector(newMachine),select:#selector(selectMachine(_:)),tiles:tiles)
    }
    /* Off screen, for checks: the launcher as it would show the library now, to a PNG. */
    static func render(to path: String) throws {
        _=NSApplication.shared;NSApp.appearance=NSAppearance(named:.darkAqua)
        let app=PtolemyApp();app.library=try ProductLibrary();app.view=app.makeView();app.reload()
        let v=app.view!;v.frame=NSRect(x:0,y:0,width:1120,height:720);v.layoutSubtreeIfNeeded()
        guard let rep=v.bitmapImageRepForCachingDisplay(in:v.bounds) else{throw InstallError("No bitmap for the launcher")}
        v.cacheDisplay(in:v.bounds,to:rep);try check(rep.representation(using:.png,properties:[:])?.write(to:URL(fileURLWithPath:path)) != nil,"Launcher PNG not written")
    }
    private func reload(){
        do{urls=try library.list();items=urls.map{try? Installation.load($0)}}catch{show(error)}
        if !urls.indices.contains(index){index=max(0,urls.count-1)};refresh()
    }
    private func refresh(){
        guard view != nil else{return}
        view.setMachines(urls.indices.map{i->Machine in let item=items[i],live=running==urls[i]
            let detail=live ? "Running":item?.phase == .ready ? "Stopped":item?.phase == .toolsPending ? "Setup: step 2 of 3":"Installing: step 1 of 3"
            return (item?.name ?? "Tiger",detail,item?.installerIdentity.version ?? "?",live,item?.phase == .installer)},selected:index)
        updateCard()
    }
    private func updateCard(){
        guard view != nil else{return};let v=view!;let has=urls.indices.contains(index),live=has && running==urls[index];let item: Installation?=has ? items[index]:nil
        let cpu=["750":"G3 750","7400":"G4 7400","7447a":"G4 7447A"]
        if let item=item {
            let minutes=started.map{Int(Date().timeIntervalSince($0)/60)} ?? 0
            Art.set(v.status,live ? (minutes<1 ? "Running":"Running for \(minutes) minute\(minutes==1 ? "":"s")"):item.phase == .ready ? "Ready":item.phase == .toolsPending ? "Setup: Ptolemy Tools are next":"Installation not finished",11,bold:true,color:Art.amber)
            Art.set(v.name,item.name,34,bold:true)
            Art.set(v.specs,"Mac OS X \(item.installerIdentity.version) · PowerPC \(cpu[item.cpu] ?? item.cpu)\n\(item.ram>=1024 && item.ram%1024==0 ? "\(item.ram/1024) GB":"\(item.ram) MB") memory · \(item.diskGiB) GB disk",12,color:Art.rgb(0xcfc9bf))
            v.primary.title=live ? "Show Tiger’s Window":item.phase == .ready ? "Start Tiger":item.phase == .toolsPending ? "Finish Setup":"Continue Installation"
            v.primary.isEnabled=live ? process != nil:!busy
            Art.set(v.note,live ? "To stop, shut down from Tiger’s Apple menu.":busy && running != nil ? "Another machine is running.":message,11,color:Art.dim)
        } else if has {
            Art.set(v.status,"Unreadable",11,bold:true,color:Art.amber);Art.set(v.name,"Tiger",34,bold:true);Art.set(v.specs,"This machine’s settings could not be read.",12,color:Art.rgb(0xcfc9bf))
            v.primary.title="Start Tiger";v.primary.isEnabled=false;Art.set(v.note,message,11,color:Art.dim)
        } else {
            Art.set(v.status,"Welcome",11,bold:true,color:Art.amber);Art.set(v.name,"No machines yet",34,bold:true)
            Art.set(v.specs,"Drop a Tiger install DVD image onto this window,\nor choose New Machine…",12,color:Art.rgb(0xcfc9bf))
            v.primary.title="New Machine…";v.primary.isEnabled = !busy;Art.set(v.note,message,11,color:Art.dim)
        }
        v.newMachine.isEnabled = !busy;links.forEach{$0.isEnabled = has && !busy}
        if busy && running==nil{v.spinner.startAnimation(nil)}else{v.spinner.stopAnimation(nil)}
        v.needsLayout=true;v.needsDisplay=true
    }
    private var selected: URL? {urls.indices.contains(index) ? urls[index]:nil}
    private func state(_ text: String){DispatchQueue.main.async{self.message=text;self.updateCard()}}
    private func show(_ error: Error){message=String(describing:error);updateCard();if let w=window{NSAlert(error:error).beginSheetModal(for:w)}}
    private func work(_ body: @escaping ()throws->Void){
        guard !busy else{return};busy=true;lock.lock();cancelled=false;lock.unlock();refresh()
        DispatchQueue.global(qos:.userInitiated).async{
            let result=Result{try body()}
            DispatchQueue.main.async{self.busy=false;self.running=nil;self.started=nil;self.reload();if case .failure(let e)=result{self.show(e)};if self.quitting{NSApp.terminate(nil)}}
        }
    }
    @objc private func selectMachine(_ sender: NSControl){index=sender.tag;message="";refresh()}
    @objc private func primaryAction(){
        guard let folder=selected else{newMachine();return}
        if running==folder{lock.lock();let pid=process?.processIdentifier;lock.unlock();if let pid{_=NSRunningApplication(processIdentifier:pid)?.activate(options:[.activateAllWindows])};return}
        work{try self.resources.validate();try self.run(folder)}
    }
    @objc private func aboutWeb(){
        let a=NSAlert();a.messageText="Web pages in Tiger"
        a.informativeText="Tiger’s own browsers can no longer open most of today’s secure sites. To help them, your Mac fetches http:// pages for Tiger over a secure HTTPS connection whenever the site offers one, and adjusts them a little (links, newer picture formats) so Tiger can show them. All of this happens on this Mac and nothing is sent anywhere else: the sites are reached the same way as when you browse on your Mac. Browsers that handle https:// themselves, such as PowerFox, connect directly."
        a.beginSheetModal(for:window)
    }
    @objc private func reveal(){if let f=selected{NSWorkspace.shared.activateFileViewerSelecting([f])}}
    /* New machine: its own sheet (install image, name, disk size). */
    @objc private func newMachine(){
        guard !busy,sheet==nil else{return};installer=nil;identity=nil;chosen.stringValue="Choose the Tiger install DVD image (or drop it onto the window)."
        let s=NSWindow(contentRect:NSRect(x:0,y:0,width:480,height:236),styleMask:[.titled],backing:.buffered,defer:false);s.appearance=NSAppearance(named:.darkAqua)
        let title=Art.label("New Machine",13,bold:true),choose=NSButton(title:"Choose Install DVD…",target:self,action:#selector(pick))
        slider.target=self;slider.action=#selector(diskChanged);diskLabel.stringValue="\(Int(slider.doubleValue)) GB";[chosen,diskLabel].forEach{$0.font=Art.font(11)};name.font=Art.font(13)
        let cancel=NSButton(title:"Cancel",target:self,action:#selector(closeSheet)),create=NSButton(title:"Create and Install",target:self,action:#selector(create));create.keyEquivalent="\r";cancel.keyEquivalent="\u{1b}"
        [choose,cancel,create].forEach{$0.font=Art.font(13)}
        let row=NSStackView(views:[Art.label("Name",13),name]);row.spacing=10;name.widthAnchor.constraint(equalToConstant:240).isActive=true
        let disk=NSStackView(views:[Art.label("Disk",13),slider,diskLabel]);disk.spacing=10;slider.widthAnchor.constraint(equalToConstant:240).isActive=true
        let buttons=NSStackView(views:[cancel,create]);buttons.spacing=10
        let stack=NSStackView(views:[title,NSStackView(views:[choose,chosen]),row,disk,buttons]);stack.orientation = .vertical;stack.alignment = .leading;stack.spacing=14
        stack.edgeInsets=NSEdgeInsets(top:20,left:22,bottom:20,right:22);stack.translatesAutoresizingMaskIntoConstraints=false;s.contentView=NSView();s.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo:s.contentView!.leadingAnchor),stack.trailingAnchor.constraint(equalTo:s.contentView!.trailingAnchor),stack.topAnchor.constraint(equalTo:s.contentView!.topAnchor),buttons.trailingAnchor.constraint(equalTo:stack.trailingAnchor,constant:-22)])
        chosen.preferredMaxLayoutWidth=280;sheet=s;window.beginSheet(s)
    }
    @objc private func closeSheet(){if let s=sheet{window.endSheet(s);sheet=nil}}
    @objc private func pick(){guard !busy else{return};let p=NSOpenPanel();p.title="Choose a PowerPC Tiger Install DVD Image";p.allowedContentTypes=[.diskImage,UTType(filenameExtension:"iso") ?? .data];p.allowsMultipleSelection=false
        p.beginSheetModal(for:sheet ?? window){if $0 == .OK,let url=p.url{self.choose(url)}}}
    private func choose(_ url: URL){guard !busy else{return};chosen.stringValue="Checking \(url.lastPathComponent)…"
        DispatchQueue.global(qos:.userInitiated).async{
            do{try regular(url);let image=try ImageMount(image:url,readOnly:true)
                do{let identity=try validateTigerVolume(image.volume);try image.close();DispatchQueue.main.async{self.installer=url;self.identity=identity;self.chosen.stringValue="Mac OS X \(identity.version) for PowerPC · \(url.lastPathComponent)"}}
                catch{try? image.close();throw error}
            }catch{DispatchQueue.main.async{self.chosen.stringValue="Not a PowerPC Tiger install DVD: \(error)"}}
        }}
    @objc private func diskChanged(){slider.doubleValue=slider.doubleValue.rounded();diskLabel.stringValue="\(Int(slider.doubleValue)) GB"}
    @objc private func create(){
        guard let url=installer,let identity=identity else{pick();return};var item=Installation(installer:url.path,installerIdentity:identity);item.name=name.stringValue;item.diskGiB=Int(slider.doubleValue)
        closeSheet();work{try self.resources.validate();self.state("Creating the Tiger disk…");let folder=try self.library.create(item)
            DispatchQueue.main.async{self.reload();if let i=self.urls.firstIndex(of:folder){self.index=i;self.refresh()}};try self.run(folder)}
    }
    private func run(_ folder: URL) throws {
        let lease=try ProductLock(folder);defer{withExtendedLifetime(lease){}}
        var item=try Installation.load(folder)
        while true {
            lock.lock();let cancel=cancelled;lock.unlock();if cancel{return}
            if item.phase == .toolsPending {
                state("Installing Ptolemy Tools…")
                let medium=try ImageMount(image:resources.tools,readOnly:true)
                do {let target=try ImageMount(image:folder.appendingPathComponent("Tiger.sparsebundle"),readOnly:false)
                    do{try ToolsPreinstaller.install(volume:target.volume,medium:medium.volume);try target.close()}catch{try? target.close();throw error}
                    try medium.close()
                }catch{try? medium.close();throw InstallError("Ptolemy Tools could not be installed: \(error). Finish Setup tries this step again; Settings can resume an unfinished installation; the disk is kept.")}
                item.phase = .ready;try item.save(folder);continue
            }
            var optical: ImageMount?
            if item.phase == .installer {state("Opening the Tiger installer…");optical=try ImageMount(image:URL(fileURLWithPath:item.installer),readOnly:true);_=try validateTigerVolume(optical!.volume)}
            defer{try? optical?.close()}
            try ImageMount.requireDetached(folder.appendingPathComponent("Tiger.sparsebundle"))
            let child=Process();child.executableURL=resources.engine;child.arguments=resources.arguments(item,folder,cd:optical.map{$0.device.replacingOccurrences(of:"/dev/disk",with:"/dev/rdisk")});child.environment=resources.environment(item.phase)
            let logURL=folder.appendingPathComponent("session-\(UUID().uuidString).log");FileManager.default.createFile(atPath:logURL.path,contents:nil,attributes:[.posixPermissions:0o600]);let log=try FileHandle(forWritingTo:logURL)
            child.standardOutput=log;child.standardError=log;child.standardInput=FileHandle.nullDevice
            lock.lock();if cancelled{lock.unlock();try? log.close();return}
            do{try child.run();process=child;lock.unlock()}catch{process=nil;lock.unlock();try? log.close();throw error}
            DispatchQueue.main.async{self.running=folder;self.started=Date();self.message="";self.refresh()}
            state(item.phase == .installer ? "Complete the installation in Tiger; setup continues after Restart.":"")
            child.waitUntilExit();try log.close();lock.lock();process=nil;let stop=cancelled;lock.unlock();try optical?.close()
            DispatchQueue.main.async{self.running=nil;self.started=nil;self.refresh()}
            if stop{return}
            if child.terminationReason == .exit && child.terminationStatus==95 && item.phase == .installer {item.phase = .toolsPending;try item.save(folder);continue}
            try check(child.terminationReason == .exit && child.terminationStatus==0,"Tiger stopped unexpectedly. The disk is kept; see \(logURL.lastPathComponent).")
            state(item.phase == .ready ? "":"Installation paused. Continue Installation resumes it.");return
        }
    }
    @objc private func settings(){
        guard !busy,let folder=selected else{return}
        do {var item=try Installation.load(folder);let alert=NSAlert();alert.messageText="Settings for \(item.name)";alert.addButton(withTitle:"Save");alert.addButton(withTitle:"Cancel");if item.phase != .ready {alert.addButton(withTitle:"Resume Installer")}
            let cpu=NSPopUpButton();cpu.addItems(withTitles:["G3 750","G4 7400","G4 7447A"]);cpu.selectItem(at:["750","7400","7447a"].firstIndex(of:item.cpu)!)
            let ram=NSPopUpButton();ram.addItems(withTitles:stride(from:512,through:2048,by:64).map(String.init));ram.selectItem(withTitle:String(item.ram))
            let disk=NSTextField(string:String(item.diskGiB));let stack=NSStackView(views:[NSTextField(labelWithString:"Processor"),cpu,NSTextField(labelWithString:"Memory (MB)"),ram,NSTextField(labelWithString:"Disk (GB, increase only)"),disk]);stack.orientation = .vertical;stack.alignment = .leading;stack.spacing=8;stack.frame=NSRect(x:0,y:0,width:360,height:220);alert.accessoryView=stack
            alert.beginSheetModal(for:window){response in
                if response == .alertThirdButtonReturn {self.work{do{let lease=try ProductLock(folder);defer{withExtendedLifetime(lease){}};item.phase = .installer;try item.save(folder)};try self.resources.validate();try self.run(folder)};return}
                guard response == .alertFirstButtonReturn else{return}
                item.cpu=["750","7400","7447a"][cpu.indexOfSelectedItem];item.ram=Int(ram.titleOfSelectedItem!)!
                guard let size=Int(disk.stringValue),size>=item.diskGiB && size<=120 else{self.show(InstallError("Disk size must stay the same or increase, up to 120 GB"));return}
                self.work{if size>item.diskGiB {self.state("Growing a cloned disk; keeping a recovery copy…");try self.library.resize(folder,to:size,settings:item)}
                    else{let lease=try ProductLock(folder);defer{withExtendedLifetime(lease){}};try item.save(folder)};self.state("Settings saved.")}
            }
        }catch{show(error)}
    }
    @objc private func remove(){guard !busy,let folder=selected else{return};let a=NSAlert();a.messageText="Move this Tiger machine to the Trash?";a.informativeText="Its disk and saved files go to the Mac’s Trash. Your install DVD image is kept.";a.addButton(withTitle:"Move to Trash");a.addButton(withTitle:"Cancel");a.beginSheetModal(for:window){if $0 == .alertFirstButtonReturn{self.work{try self.library.trash(folder);self.state("Moved to the Trash.")}}}}
    func applicationShouldTerminate(_ sender: NSApplication)->NSApplication.TerminateReply {
        guard busy else{return .terminateNow};let a=NSAlert();a.messageText="Shut down Tiger and quit Ptolemy?";a.informativeText="Tiger shuts down as from its Apple menu; its applications may ask you to save first. Ptolemy quits when Tiger has stopped.";a.addButton(withTitle:"Shut Down and Quit");a.addButton(withTitle:"Cancel")
        // SIGHUP asks Tiger to shut down (stand_window_macos.m); SIGTERM would stop it at once and lose its unwritten data.
        if a.runModal() == .alertFirstButtonReturn{quitting=true;lock.lock();cancelled=true;let child=process;lock.unlock();if child?.isRunning==true{kill(child!.processIdentifier,SIGHUP)};state("Waiting for Tiger to shut down…")};return .terminateCancel
    }
    func windowShouldClose(_ sender: NSWindow)->Bool {if busy{NSApp.terminate(nil);return false};NSApp.terminate(nil);return true}
    static func main(){let app=NSApplication.shared,delegate=PtolemyApp();app.delegate=delegate;withExtendedLifetime(delegate){app.run()}}
}
