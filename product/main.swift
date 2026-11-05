// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

if CommandLine.arguments.contains("--self-test") {
    do { try productSelfTest() } catch {
        fputs("Product check failed: \(error)\n", stderr)
        exit(1)
    }
} else {
    PtolemyApp.main()
}
