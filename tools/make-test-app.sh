#!/bin/sh
# A separate bundle lets accessibility tools address the test window.
set -eu
if test "$#" -gt 2;then echo 'usage: make-test-app.sh [firmware-binary [output.app]]' >&2;exit 2;fi
binary=${1:-build/ptolemy-engine}
app=${2:-build/PtolemyRadeonTest.app}
mkdir -p "$app/Contents/MacOS"
cp "$binary" "$app/Contents/MacOS/PtolemyRadeonTest"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.ptolemy.experimental.radeon-test</string>
<key>CFBundleExecutable</key><string>PtolemyRadeonTest</string>
<key>CFBundleName</key><string>Ptolemy Radeon Test</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><true/>
<key>CFBundleIconFile</key><string>Ptolemy</string>
</dict></plist>
PLIST
icons=assets; [ -d "$icons" ] || icons=../../assets   # assets/app-icon-macos-1024.png, the icon in the macOS grid
if test -f "$icons/app-icon-macos-1024.png";then
  tmp=$(mktemp -d);set="$tmp/Ptolemy.iconset";mkdir -p "$set" "$app/Contents/Resources"
  for s in 16 32 128 256 512;do
    sips -z $s $s "$icons/app-icon-macos-1024.png" --out "$set/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) "$icons/app-icon-macos-1024.png" --out "$set/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$set" -o "$app/Contents/Resources/Ptolemy.icns";rm -rf "$tmp"
fi
codesign --force --sign - --entitlements epicycle/hv.entitlements "$app"
touch "$app"   # Finder/Dock cache icons per bundle; a new date makes them read Ptolemy.icns again
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$app" 2>/dev/null || true
