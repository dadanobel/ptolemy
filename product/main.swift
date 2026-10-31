// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
let arguments=CommandLine.arguments
if let i=arguments.firstIndex(of:"--render-launcher"),i+1<arguments.count{do{try PtolemyApp.render(to:arguments[i+1])}catch{fputs("Launcher render failed: \(error)\n",stderr);exit(1)}}   /* checks */
else if CommandLine.arguments.contains("--self-test"){do{try productSelfTest()}catch{fputs("Product check failed: \(error)\n",stderr);exit(1)}}else{PtolemyApp.main()}
