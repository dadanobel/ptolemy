// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import CryptoKit
func productSelfTest() throws {
    try check(InstallError("Specific recovery instruction").localizedDescription=="Specific recovery instruction","Native alerts must preserve actionable errors")
    let fm=FileManager.default,root=fm.temporaryDirectory.appendingPathComponent("ptolemy-product-test-"+UUID().uuidString)
    try fm.createDirectory(at:root,withIntermediateDirectories:false);defer{try? fm.removeItem(at:root)}
    func reject(_ body: ()throws->Void) throws {var rejected=false;do{try body()}catch{rejected=true};try check(rejected,"Negative control unexpectedly passed")}
    let volume=root.appendingPathComponent("Tiger"),medium=root.appendingPathComponent("Tools")
    try fm.createDirectory(at:volume.appendingPathComponent("System/Library/CoreServices"),withIntermediateDirectories:true)
    try fm.createDirectory(at:volume.appendingPathComponent("System/Installation/Packages"),withIntermediateDirectories:true)
    try fm.createDirectory(at:volume.appendingPathComponent("Library/Preferences"),withIntermediateDirectories:true)
    try plistData(["ProductVersion":"10.4.11","ProductBuildVersion":"8S165"]).write(to:volume.appendingPathComponent("System/Library/CoreServices/SystemVersion.plist"))
    var kernel=Data([0xfe,0xed,0xfa,0xce,0,0,0,18]);kernel.append(Data(repeating:0,count:24));try kernel.write(to:volume.appendingPathComponent("mach_kernel"))
    try check(try validateTigerVolume(volume).version=="10.4.11","PPC Tiger validation")
    try check(!ppc32Kernel(Data([0xfe,0xed,0xfa,0xce,0,0,0,7])) && !ppc32Kernel(Data("random".utf8)),"Intel/random negative control")
    kernel[7]=7;try kernel.write(to:volume.appendingPathComponent("mach_kernel"));try reject{_=try validateTigerVolume(volume)};kernel[7]=18;try kernel.write(to:volume.appendingPathComponent("mach_kernel"))
    try fm.createDirectory(at:medium.appendingPathComponent("payload/bin"),withIntermediateDirectories:true)
    let file=Data("our worker".utf8);try file.write(to:medium.appendingPathComponent("payload/bin/worker"));try Data("print 'fixture'\n".utf8).write(to:medium.appendingPathComponent("install.py"))
    let hash=Insecure.SHA1.hash(data:file).map{String(format:"%02x",$0)}.joined();try Data("\(hash) 0755 bin/worker\n".utf8).write(to:medium.appendingPathComponent("manifest.sha1"))
    let prefs=volume.appendingPathComponent("Library/Preferences/loginwindow.plist");try plistData(["Preserve":17]).write(to:prefs)
    try ToolsPreinstaller.install(volume:volume,medium:medium);try ToolsPreinstaller.install(volume:volume,medium:medium)
    let value=try plist(Data(contentsOf:prefs));try check(value["Preserve"] as? Int==17 && (value["AutoLaunchedApplicationDictionary"] as? [[String:Any]])?.count==1,"Idempotent Tools and preserved preferences")
    try Data("tampered".utf8).write(to:medium.appendingPathComponent("payload/bin/worker"));try reject{try ToolsPreinstaller.install(volume:volume,medium:medium)}
    try reject{_=try checkedChild(root,"../outside")};try reject{_=try checkedChild(root,"/absolute")}
    let identity=InstallerIdentity(version:"10.4",build:"8A428"),installer=root.appendingPathComponent("installer.iso")
    var item=Installation(installer:installer.path,installerIdentity:identity)
    var commands=[(String,[String])](),failResize=false
    let library=try ProductLibrary(root:root.appendingPathComponent("Library")){tool,args in
        commands.append((tool,args))
        if args.first=="create"{try fm.createDirectory(at:URL(fileURLWithPath:args.last!),withIntermediateDirectories:false)}
        if tool=="/bin/cp" {try fm.copyItem(at:URL(fileURLWithPath:args[2]),to:URL(fileURLWithPath:args[3]))}
        if args.first=="resize" && failResize {throw InstallError("Injected resize failure")}
        return try plistData(["images":[]])
    }
    let folder=try library.create(item);try check(try library.list().map{$0.path}==[folder.path],"Library publication")
    let recipe=commands[0].1;try check(recipe.contains("SPARSEBUNDLE") && recipe.contains("HFS+J") && recipe.contains("SPUD"),"Sparse HFS/APM recipe")
    do{let lease=try ProductLock(folder);try reject{_=try ProductLock(folder)};withExtendedLifetime(lease){}}
    failResize=true;try reject{try library.resize(folder,to:80)};try check(try Installation.load(folder).diskGiB==40,"Failed resize preserved configuration")
    failResize=false;try library.resize(folder,to:80);try check(try Installation.load(folder).diskGiB==80,"Atomic resize publication")
    try reject{try library.resize(folder,to:20)}
    item.ram=513;try reject{try item.validate()};item.ram=2048;item.cpu="G5";try reject{try item.validate()}
    let resources=ProductResources(root:root)
    item.cpu="7447a";let a=resources.arguments(item,folder,cd:"/dev/rdisk20")
    var g3=item;g3.cpu="750";try check(!a.contains("--cpu") && resources.arguments(g3,folder,cd:nil).contains("750"),"7447A uses the engine's window defaults")
    try check(a.contains("--network-user"),"Tiger gets the rootless network")
    try check(a.contains("--boot-cd") && !a.contains("--overlay") && resources.environment(.installer)["PT_RESTART"]=="exit" && resources.environment(.ready)["PT_FULLSCREEN"]=="1","Installer/ready launch contract")
    let mounted=try plistData(["images":[["image-path":installer.path]]])
    try reject{try ImageMount.requireDetached(installer,execute:{_,_ in mounted})}
    var detached=[String]();let attachment=try plistData(["system-entities":[["dev-entry":"/dev/disk25"],["dev-entry":"/dev/disk25s3","mount-point":volume.path]]])
    var mediaID: UInt64=55
    let mount=try ImageMount(image:installer,readOnly:true,execute:{_,args in
        if args.first=="info"{return try plistData(["images":[]])};if args.first=="attach"{try check(args.contains("-readonly"),"Installer must mount read-only");return attachment};detached=args;return Data()
    },identity:{_ in mediaID})
    mediaID=56;try reject{try mount.close()};try check(detached.isEmpty,"Recycled disk number was not ejected");mediaID=55;try mount.close();try check(detached==["detach","/dev/disk25"],"Exact owned image detach")
    let previous=ProcessInfo.processInfo.environment["PT_R1_UX"];setenv("PT_R1_UX","0",1);try check(resources.environment(.ready)["PT_R1_UX"]=="0","Explicit UX opt-out must survive the product wrapper");if let previous=previous{setenv("PT_R1_UX",previous,1)}else{unsetenv("PT_R1_UX")}
    print("PASS: PPC/Intel image controls, sparse disk recipe, locks, atomic create/resize rollback, native Tools checksums/idempotence and launch policy")
}
