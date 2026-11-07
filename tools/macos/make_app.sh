#!/bin/bash
# Builds "MechWarrior 3.app" and its DMG for Mac players (docs/macos.md):
# a package build (MW3_PACKAGE: no development copy of the game in it), the
# libraries it loads copied into Contents/Frameworks with their paths
# rewritten to @rpath, the icon, Info.plist, every binary signed ad hoc
# (no identity, so no name in the signature: the releases are not Developer
# ID signed or notarized, docs/macos.md), then the DMG.
#
#   tools/macos/make_app.sh                          run from the repo root
#   tools/macos/make_app.sh --identity "<IDENTITY>"  opt-in, never for releases:
#        signs with that keychain identity and the hardened runtime instead
#   tools/macos/make_app.sh --deps                   the release form: universal
#        (arm64, x86_64), macOS 12 and later, against the libraries
#        tools/deps/build_deps.sh mac built (build-deps/mac/prefix), not Homebrew's
#
# Out: build-mac/dist/MechWarrior 3.app and
#      build-mac/dist/MW3-Rebuild-<git describe>-mac.dmg
set -euo pipefail
cd "$(dirname "$0")/../.."
ID="-"  # ad hoc
DEPS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --identity) ID="${2:?--identity needs a name}"; shift 2 ;;
    --deps) DEPS="$PWD/build-deps/mac/prefix"; shift ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
if [ -n "$DEPS" ] && [ ! -d "$DEPS/lib" ]; then echo "no $DEPS: run tools/deps/build_deps.sh mac first" >&2; exit 1; fi
if [ -z "$DEPS" ]; then
  echo "note: Homebrew's libraries (this Mac's macOS, arm64 only); THIRD-PARTY.txt describes the --deps build: not for release" >&2
fi
NAME="MechWarrior 3"  # the app as installed (CFBundleName, the display name); the DMG keeps MW3-Rebuild-
BUNDLE_ID="com.owenmorgan.mw3rebuild"
VER="$(git describe --tags --match 'v[0-9]*' --always --dirty)"
OUT=build-mac/dist
APP="$OUT/$NAME.app"

# The build, from scratch, as package.cmd does on Windows.
rm -rf build-mac-pkg
CONF=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DMW3_PACKAGE=ON)
if [ -n "$DEPS" ]; then
  CONF+=(-DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0)
  export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig"  # its FFmpeg, not Homebrew's
fi
cmake -S . -B build-mac-pkg "${CONF[@]}" >/dev/null
cmake --build build-mac-pkg --target mw3 mw3launcher >/dev/null
# The movie importer beside mw3 (the first start converts the movies with it,
# first_run_import.h), on the FFmpeg build_deps.sh built; Homebrew's FFmpeg
# would bring dozens of libraries, so without --deps there is none.
if [ -n "$DEPS" ]; then cmake --build build-mac-pkg --target mw3import >/dev/null; fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
# The launcher is the app's own executable; the game beside it, started by it
# and on its own (docs/games-layout.md).
cp build-mac-pkg/mw3launcher "$APP/Contents/MacOS/mw3launcher"
cp build-mac-pkg/mw3 "$APP/Contents/MacOS/mw3"
if [ -n "$DEPS" ]; then cp build-mac-pkg/mw3import "$APP/Contents/MacOS/mw3import"; fi
# The launcher's art (ours, assets/launcher), where SDL's base path is: Resources.
mkdir -p "$APP/Contents/Resources/assets/launcher"
cp assets/launcher/*.png "$APP/Contents/Resources/assets/launcher/"

# Every library outside the system, followed through the ones they load:
# copied by their real file, named by their install name's file, and every
# reference to them rewritten to @rpath/<that name>.
python3 - "$APP" "$DEPS" <<'EOF'
import glob, os, shutil, subprocess, sys
app = sys.argv[1]
prefix = sys.argv[2] if len(sys.argv) > 2 else ''
exes = [os.path.join(app, 'Contents/MacOS', n) for n in ('mw3launcher', 'mw3', 'mw3import')]
exes = [e for e in exes if os.path.exists(e)]
fw = os.path.join(app, 'Contents/Frameworks')
def deps(f):
    out = subprocess.run(['otool', '-L', f], capture_output=True, text=True, check=True).stdout.splitlines()
    # A universal file's list has a "<file> (architecture x):" header per arch.
    return sorted({l.strip().split(' (')[0] for l in out if l.startswith(('\t', ' '))})
def foreign(p):
    if prefix and p.startswith('@rpath/'):  # build_deps.sh's libraries are named @rpath/<file>
        return True
    return not (p.startswith('/System/') or p.startswith('/usr/lib/') or p.startswith('@'))
def licences(real, name):
    dst = os.path.join(app, 'Contents/Resources/licenses', name)
    if prefix:
        # From the source tree it was built from (build-deps/mac/work/src/<name>-<version>).
        stem = name.split('.')[0].removeprefix('lib')
        srcs = [d for d in glob.glob(os.path.join(os.path.dirname(prefix), 'work/src/*'))
                if os.path.basename(d).lower().startswith(stem.lower() + '-')]
        if stem.startswith('av') or stem.startswith('sw'):
            srcs = glob.glob(os.path.join(os.path.dirname(prefix), 'work/src/ffmpeg-*'))
        if stem == 'SDL3_ttf':  # FreeType is built into it
            srcs += glob.glob(os.path.join(os.path.dirname(prefix), 'work/src/freetype-*'))
        for d in srcs:
            for n in os.listdir(d):
                if n.upper().startswith(('LICEN', 'COPYING')) and os.path.isfile(os.path.join(d, n)):
                    sub = os.path.join(dst, os.path.basename(d))
                    os.makedirs(sub, exist_ok=True)
                    shutil.copy2(os.path.join(d, n), sub)
            # FreeType's LICENSE.TXT points to the FTL it is used under.
            ftl = os.path.join(d, 'docs', 'FTL.TXT')
            if os.path.isfile(ftl):
                shutil.copy2(ftl, os.path.join(dst, os.path.basename(d)))
        return
    # Its licence files, from its keg (Cellar/<formula>/<version>/).
    keg = os.path.dirname(os.path.dirname(real))
    lic = os.path.join(app, 'Contents/Resources/licenses', os.path.basename(os.path.dirname(keg)))
    os.makedirs(lic, exist_ok=True)
    for n in os.listdir(keg):
        if n.upper().startswith(('LICEN', 'COPYING')):
            shutil.copy2(os.path.join(keg, n), lic)
copied, todo = {}, list(exes)
while todo:
    f = todo.pop()
    for d in deps(f):
        if foreign(d) and d not in copied:
            name = os.path.basename(d)
            dst = os.path.join(fw, name)
            if not os.path.exists(dst):
                src = os.path.join(prefix, 'lib', name) if d.startswith('@rpath/') else d
                if not os.path.exists(src):
                    sys.exit(f'{d}: not in {prefix}/lib')
                real = os.path.realpath(src)
                shutil.copy2(real, dst)
                os.chmod(dst, 0o644)
                licences(real, name)
            copied[d] = name
            todo.append(dst)
for f in exes + [os.path.join(fw, n) for n in set(copied.values())]:
    args = []
    for d in deps(f):
        if d in copied:
            args += ['-change', d, '@rpath/' + copied[d]]
    if f not in exes:
        args += ['-id', '@rpath/' + os.path.basename(f)]
    if args:
        subprocess.run(['install_name_tool'] + args + [f], check=True, stderr=subprocess.DEVNULL)
# Only the bundle's rpath: a build against a prefix keeps its absolute lib path.
for exe in exes:
    lc = subprocess.run(['otool', '-l', exe], capture_output=True, text=True, check=True).stdout.split('\n')
    stale = {lc[i + 2].strip().split(' ')[1] for i, l in enumerate(lc) if 'cmd LC_RPATH' in l}  # each arch lists it
    for path in sorted(p for p in stale):
        subprocess.run(['install_name_tool', '-delete_rpath', path, exe], check=True)
    subprocess.run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', exe], check=True)
def outside(p):
    return not (p.startswith('/System/') or p.startswith('/usr/lib/') or p.startswith('@'))
left = [d for f in exes + [os.path.join(fw, n) for n in set(copied.values())] for d in deps(f) if outside(d)]
if left:
    sys.exit('still outside the bundle: ' + ', '.join(sorted(set(left))))
print(f'{len(set(copied.values()))} libraries bundled:', ' '.join(sorted(set(copied.values()))))
EOF

# The icon: make_icon.py's drawing at each iconset size.
ICONSET="build-mac/mw3.iconset"
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
python3 - "$ICONSET" <<'EOF'
import sys, pathlib, importlib.util
spec = importlib.util.spec_from_file_location('make_icon', 'tools/windows/make_icon.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
out = pathlib.Path(sys.argv[1])
for n in (16, 32, 128, 256, 512):
    (out / f'icon_{n}x{n}.png').write_bytes(m.png(n))
    (out / f'icon_{n}x{n}@2x.png').write_bytes(m.png(2 * n))
EOF
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/mw3.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleExecutable</key><string>mw3launcher</string>
  <key>CFBundleIconFile</key><string>mw3</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VER</string>
  <key>CFBundleVersion</key><string>$VER</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>$(otool -l "$APP/Contents/MacOS/mw3" | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print $2; exit}')</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHumanReadableCopyright</key><string>A fan rebuild; MechWarrior is a trademark of its owners. Needs your own copy of MechWarrior 3.</string>
</dict>
</plist>
EOF

# The read-me and the notices beside the program.
cp tools/macos/README.txt "$APP/Contents/Resources/README.txt"
if [ -f LICENSE ]; then cp LICENSE "$APP/Contents/Resources/LICENSE.txt"; fi
cp tools/macos/THIRD-PARTY.txt "$APP/Contents/Resources/THIRD-PARTY.txt"
# libarchive, zlib and liblzma: linked in with --deps (build_deps.sh keeps
# their licences in the prefix); Homebrew's libarchive is a dylib, its
# licences copied with it above.
if [ -n "$DEPS" ]; then
  [ -d "$DEPS/share/licenses/libarchive-3.7.9" ] || { echo "no licences in $DEPS/share/licenses: run tools/deps/build_deps.sh mac again" >&2; exit 1; }
  cp -R "$DEPS/share/licenses/." "$APP/Contents/Resources/licenses/"
fi

# Signing: the libraries first, then the app. Ad hoc by default (the
# release form: macOS asks once before the first open, README.txt). With
# --identity: the hardened runtime and a secure timestamp too.
# tools/macos/mw3.entitlements grants nothing: the app needs no exception.
if [ "$ID" = "-" ]; then SIGN=(--force --sign -); else SIGN=(--force --timestamp --options runtime --sign "$ID"); fi
for f in "$APP"/Contents/Frameworks/*.dylib; do
  codesign "${SIGN[@]}" "$f"
done
# The game and the importer, second programs in Contents/MacOS, before the app that seals them.
codesign "${SIGN[@]}" --entitlements tools/macos/mw3.entitlements "$APP/Contents/MacOS/mw3"
if [ -f "$APP/Contents/MacOS/mw3import" ]; then
  codesign "${SIGN[@]}" --entitlements tools/macos/mw3.entitlements "$APP/Contents/MacOS/mw3import"
fi
codesign "${SIGN[@]}" --entitlements tools/macos/mw3.entitlements "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
# The package carries both programs, the launcher as the app's executable.
for p in mw3launcher mw3; do [ -x "$APP/Contents/MacOS/$p" ] || { echo "no $p in the app" >&2; exit 1; }; done
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist")" = mw3launcher ] || { echo "the app does not start the launcher" >&2; exit 1; }

# The DMG: the app and a link to Applications, compressed, signed.
DMG="$OUT/MW3-Rebuild-$VER-mac.dmg"
STAGE="build-mac/dmg"
rm -rf "$STAGE" "$DMG" && mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp tools/macos/README.txt "$STAGE/Read me.txt"
# No local path or user name in anything shipped (release.md, Privacy).
python3 -I tools/deps/check_paths.py "$STAGE" || exit 1
hdiutil create -volname "$NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
if [ "$ID" != "-" ]; then codesign --force --timestamp --sign "$ID" "$DMG"; fi
echo "App: $APP"
echo "DMG: $DMG"
