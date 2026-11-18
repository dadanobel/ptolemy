// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

if CommandLine.arguments.contains("--self-test") {
    do { try productSelfTest() } catch {
        fputs("Product check failed: \(error)\n", stderr)
        exit(1)
    }
} else if let i = CommandLine.arguments.firstIndex(of: "--snapshot"), i + 1 < CommandLine.arguments.count {
    do { try productSnapshot(CommandLine.arguments[i + 1]) } catch {
        fputs("Snapshots failed: \(error)\n", stderr)
        exit(1)
    }
} else if let i = CommandLine.arguments.firstIndex(of: "--update-tools"), i + 1 < CommandLine.arguments.count {
    /* The window's silent Tools update for one stopped machine folder (tests; also by hand). */
    do {
        let folder = URL(fileURLWithPath: CommandLine.arguments[i + 1])
        var item = try Installation.load(folder)
        let changed = try updateTools(folder, &item, ProductResources(root: Bundle.main.resourceURL!))
        print(changed ? "Ptolemy Tools put on the disk; the guest installs them at its next login" : "Ptolemy Tools already current")
    } catch {
        fputs("Tools update failed: \(error)\n", stderr)
        exit(1)
    }
} else {
    PtolemyApp.main()
}
