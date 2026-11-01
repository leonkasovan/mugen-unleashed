#!/usr/bin/env python3
"""Offline tests for Android tool bootstrap/build orchestration; no real SDK/APK."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
ANDROID = ROOT / "port/android"
WORK = ROOT / "build/android-bootstrap-tests"
WORK.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("metadata", ANDROID / "tools/toolchain_metadata.py")
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(0o755)


JAVA = """#!/usr/bin/env python3
import os, subprocess, sys
if '-version' in sys.argv or '-XshowSettings:properties' in sys.argv:
    print('openjdk version "17.0.99"')
else:
    sys.exit(subprocess.call([sys.executable, os.environ['MOCK_INSTALLER']] + sys.argv[1:]))
"""
INSTALLER = """import json, os, pathlib, sys
if os.environ.get('MOCK_SDK_FAIL'):
    print('SDK installation/license failure', file=sys.stderr); sys.exit(1)
args=sys.argv[1:]
sdk=pathlib.Path(next(a.split('=',1)[1] for a in args if a.startswith('--sdk_root=')))
with open(os.environ['MOCK_SDK_LOG'],'a') as f: f.write(json.dumps(args)+'\\n')
def put(rel, text='', executable=False):
    p=sdk/rel; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(text)
    if executable: p.chmod(0o755)
for package in args:
    if package.startswith('platforms;'):
        put('platforms/'+package.split(';')[1]+'/android.jar')
    elif package.startswith('build-tools;'):
        put('build-tools/'+package.split(';')[1]+'/aapt', '#!/bin/sh\\nexit 0\\n', True)
    elif package=='platform-tools':
        put('platform-tools/adb', '#!/bin/sh\\nexit 0\\n', True)
    elif package.startswith('cmake;'):
        for name in ('cmake','ninja'):
            put('cmake/'+package.split(';')[1]+'/bin/'+name, '#!/bin/sh\\nexit 0\\n', True)
    elif package.startswith('ndk;'):
        version=package.split(';')[1]; base='ndk/'+version+'/'
        put(base+'source.properties', 'Pkg.Revision = '+version+'\\n')
        put(base+'build/cmake/android.toolchain.cmake')
        put(base+'toolchains/llvm/prebuilt/linux-x86_64/bin/clang', '#!/bin/sh\\nexit 0\\n', True)
"""
CURL = """#!/usr/bin/env python3
import json, os, pathlib, shutil, sys
args=sys.argv[1:]; url=args[-1]; output=args[args.index('--output')+1]
with open(os.environ['MOCK_CURL_LOG'],'a') as f: f.write(url+'\\n')
if os.environ.get('MOCK_CURL_FAIL'): sys.exit(22)
sources=json.loads(os.environ['MOCK_DOWNLOAD_MAP'])
if url not in sources: print('Unexpected network URL: '+url,file=sys.stderr); sys.exit(2)
shutil.copyfile(sources[url], output)
"""
GRADLE = """#!/usr/bin/env python3
import json, os, pathlib, sys
with open(os.environ['MOCK_GRADLE_LOG'],'w') as f: json.dump(sys.argv[1:],f)
variant='debug' if sys.argv[-1]=='assembleDebug' else 'release'
out=pathlib.Path('app/build/outputs/apk')/variant
out.mkdir(parents=True,exist_ok=True); (out/('app-'+variant+'.apk')).write_text(variant+' fixture')
assets=pathlib.Path('app/src/main/assets'); assets.mkdir(parents=True,exist_ok=True)
(assets/'manifest.txt').write_text('')
"""


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="case with spaces ", dir=WORK)
        self.base = Path(self.temp.name)
        self.project = self.base / "project"
        self.android = self.project / "port/android"
        for rel in ("build.sh", "tools/bootstrap_toolchains.sh", "tools/toolchain_metadata.py", "app/build.gradle"):
            dest = self.android / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ANDROID / rel, dest)
        (self.android / "gradle/wrapper").mkdir(parents=True)
        (self.android / "gradle/wrapper/gradle-wrapper.jar").write_text("fixture")
        gradle_stub = self.android / "mock_gradle.py"
        gradle_stub.write_text(GRADLE)
        executable(self.android / "gradlew", "#!/usr/bin/env bash\nexec " +
                   shlex.quote(sys.executable) + " " + shlex.quote(str(gradle_stub)) + ' "$@"\n')
        self.tools = self.project / "build/android/toolchains"
        self.sdk = self.tools / "sdk"
        self.bin = self.base / "bin"
        executable(self.bin / "curl", CURL)
        executable(self.bin / "java", '#!/bin/sh\necho \'openjdk version "1.8.0"\' >&2\n')
        executable(self.bin / "uname",
                   '#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo "${MOCK_ARCH:-x86_64}";; esac\n')
        self.installer = self.base / "installer.py"
        self.installer.write_text(INSTALLER)
        self.curl_log = self.base / "curl.log"
        self.sdk_log = self.base / "sdk.log"
        self.gradle_log = self.base / "gradle.json"
        self.env = dict(os.environ)
        for key in ("JAVA_HOME", "MUGEN_JAVA_HOME", "ANDROID_HOME", "ANDROID_HOME_DIR",
                    "ANDROID_SDK_ROOT", "ANDROID_NDK_HOME", "MUGEN_ANDROID_TOOLS_DIR",
                    "SDL2_DEPS_DIR", "SDL2_COMPANION_DIR", "GAME_DATA_DIR", "ANDROID_ABI"):
            self.env.pop(key, None)
        self.env.update(PATH=str(self.bin)+os.pathsep+os.environ["PATH"],
                        MUGEN_PYTHON=sys.executable, MOCK_INSTALLER=str(self.installer),
                        MOCK_CURL_LOG=str(self.curl_log), MOCK_SDK_LOG=str(self.sdk_log),
                        MOCK_GRADLE_LOG=str(self.gradle_log), PYTHONDONTWRITEBYTECODE="1",
                        GRADLE_USER_HOME=str(self.base/"gradle cache"),
                        ANDROID_USER_HOME=str(self.base/"android user"))
        self.make_archives()

    def tearDown(self):
        self.temp.cleanup()

    def make_archives(self):
        fixtures = self.base / "fixtures"
        fixtures.mkdir()
        jdk = fixtures / "jdk17.tar.gz"
        with tarfile.open(jdk, "w:gz") as archive:
            for name, data in (("jdk/bin/java", JAVA), ("jdk/bin/javac", "#!/bin/sh\nexit 0\n")):
                payload = data.encode()
                info = tarfile.TarInfo(name); info.mode=0o755; info.size=len(payload)
                archive.addfile(info, io.BytesIO(payload))
        cli = fixtures / "cli.zip"
        with zipfile.ZipFile(cli, "w") as archive:
            archive.writestr("cmdline-tools/lib/sdkmanager-classpath.jar", "fixture")
        jdk_url = "https://github.com/adoptium/test/releases/download/17/jdk17.tar.gz"
        cli_url = "https://dl.google.com/android/repository/commandlinetools-linux-test.zip"
        self.jdk_json = fixtures / "jdk.json"
        self.jdk_json.write_text(json.dumps([{"binary": {"image_type": "jdk", "package": {
            "link": jdk_url, "checksum": hashlib.sha256(jdk.read_bytes()).hexdigest()}}}]))
        self.sdk_xml = fixtures / "sdk.xml"
        self.sdk_xml.write_text('<repository><remotePackage path="cmdline-tools;19.0"><archives>'
            '<archive><host-os>linux</host-os><complete><url>commandlinetools-linux-test.zip</url>'
            '<checksum type="sha1">'+hashlib.sha1(cli.read_bytes()).hexdigest()+
            '</checksum></complete></archive></archives></remotePackage></repository>')
        self.env["MOCK_DOWNLOAD_MAP"] = json.dumps({
            "https://api.adoptium.net/v3/assets/latest/17/hotspot?architecture=x64&image_type=jdk&os=linux&vendor=eclipse": str(self.jdk_json),
            "https://dl.google.com/android/repository/repository2-3.xml": str(self.sdk_xml),
            jdk_url: str(jdk), cli_url: str(cli)})

    def run_build(self, *args, ok=True):
        result = subprocess.run(["bash", str(self.android/"build.sh"), *args],
                                cwd=self.project, env=self.env, input="",
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def setup_tools(self):
        self.run_build("--setup")
        self.assertTrue((self.tools/"jdk-17/bin/java").exists())
        self.assertTrue((self.sdk/"ndk/27.1.12297006/source.properties").exists())

    def native_fixtures(self):
        prefix=self.project/"build/android/sdl2/arm64-v8a"
        (prefix/"include/SDL2").mkdir(parents=True,exist_ok=True)
        (prefix/"include/SDL2/SDL.h").write_text("fixture")
        (prefix/"lib").mkdir(exist_ok=True)
        (prefix/"lib/libSDL2.so").write_text("fixture")
        lib=self.project/"build/android/android-deps/arm64-v8a/lib"
        lib.mkdir(parents=True,exist_ok=True)
        for name in ("mixer","ttf"): (lib/("libSDL2_"+name+".so")).write_text("fixture")

    def test_help_no_tool_or_network_requirement(self):
        result=self.run_build("--help")
        self.assertIn("--setup",result.stdout)
        self.assertNotIn("set -euo",result.stdout)
        self.assertFalse(self.curl_log.exists())

    def test_missing_tools_fail_without_network(self):
        result=self.run_build("--check-tools",ok=False)
        self.assertIn("--setup",result.stdout)
        self.assertFalse(self.curl_log.exists())

    def test_setup_downloads_only_pins_and_is_idempotent(self):
        self.setup_tools()
        packages=json.loads(self.sdk_log.read_text().splitlines()[0])
        for expected in ("ndk;27.1.12297006","cmake;3.22.1","platforms;android-34","build-tools;33.0.1","platform-tools"):
            self.assertIn(expected,packages)
        self.assertNotIn("--licenses",packages)
        before=self.curl_log.read_text(), self.sdk_log.read_text()
        self.run_build("--setup")
        self.assertEqual(before,(self.curl_log.read_text(),self.sdk_log.read_text()))
        self.assertTrue((self.tools/"env.sh").is_file())

    def test_download_and_build_passes_selected_tool_paths(self):
        self.native_fixtures()
        self.run_build("--download-tools","--debug","--no-assets")
        args=json.loads(self.gradle_log.read_text())
        self.assertIn("-PmugenNdkPath="+str(self.sdk/"ndk/27.1.12297006"),args)
        self.assertEqual(args[-1],"assembleDebug")
        apk=self.project/"build/android/mugen-unleashed-arm64-v8a.apk"
        self.assertEqual(apk.read_text(),"debug fixture")

    def test_incompatible_latest_cli_is_not_reused(self):
        latest=self.sdk/"cmdline-tools/latest"
        (latest/"lib").mkdir(parents=True)
        (latest/"lib/sdkmanager-classpath.jar").write_text("new JVM required")
        (latest/"source.properties").write_text("Pkg.Revision = 23.0\n")
        self.setup_tools()
        args=json.loads(self.sdk_log.read_text().splitlines()[0])
        self.assertIn(str(self.sdk/"cmdline-tools/19.0/lib/sdkmanager-classpath.jar"),args)

    def test_matching_latest_cli_is_reused_without_downloading_it(self):
        latest=self.sdk/"cmdline-tools/latest"
        (latest/"lib").mkdir(parents=True)
        (latest/"lib/sdkmanager-classpath.jar").write_text("compatible fixture")
        (latest/"source.properties").write_text("Pkg.Revision = 19.0\n")
        self.setup_tools()
        self.assertNotIn("repository2-3.xml",self.curl_log.read_text())

    def test_no_assets_removes_stale_payload_and_collects_requested_variant(self):
        self.setup_tools(); self.native_fixtures()
        assets=self.android/"app/src/main/assets"
        assets.mkdir(parents=True); (assets/"stale.sff").write_text("old")
        stale=self.android/"app/build/outputs/apk/debug"
        stale.mkdir(parents=True); (stale/"app-debug.apk").write_text("stale debug")
        self.run_build("--no-assets")
        self.assertFalse((assets/"stale.sff").exists())
        self.assertEqual((self.project/"build/android/mugen-unleashed-arm64-v8a.apk").read_text(),"release fixture")

    def test_bad_jdk_checksum_never_extracts(self):
        data=json.loads(self.jdk_json.read_text())
        data[0]["binary"]["package"]["checksum"]="0"*64
        self.jdk_json.write_text(json.dumps(data))
        result=self.run_build("--setup",ok=False)
        self.assertIn("checksum",result.stdout)
        self.assertFalse((self.tools/"jdk-17").exists())

    def test_game_assets_are_copied_without_modifying_the_reference(self):
        self.setup_tools(); self.native_fixtures()
        data=self.base/"game data"
        (data/"data").mkdir(parents=True)
        cfg='[Config]\nGameWidth=640\nGameHeight=480\n[Video]\nRenderMode=System\n'
        (data/"data/mugen.cfg").write_text(cfg)
        (data/"character file.sff").write_text("sprite")
        (data/"original.exe").write_text("exclude")
        (data/"original.dll").write_text("exclude")
        self.env["GAME_DATA_DIR"]=str(data)
        self.run_build()
        assets=self.android/"app/src/main/assets"
        self.assertEqual((assets/"data/mugen.cfg").read_text(),cfg)
        self.assertTrue((assets/"character file.sff").exists())
        self.assertFalse((assets/"original.exe").exists())
        self.assertFalse((assets/"original.dll").exists())
        self.assertEqual((data/"data/mugen.cfg").read_text(),cfg)

    def test_bad_sdk_checksum_never_installs_cli(self):
        old=self.sdk_xml.read_text()
        checksum=old.split('<checksum type="sha1">')[1].split('</checksum>')[0]
        self.sdk_xml.write_text(old.replace(checksum,"0"*40))
        result=self.run_build("--setup",ok=False)
        self.assertIn("checksum",result.stdout)
        self.assertFalse((self.sdk/"cmdline-tools/19.0").exists())

    def test_download_failure_and_retry(self):
        self.env["MOCK_CURL_FAIL"]="1"
        self.run_build("--setup",ok=False)
        self.assertFalse(list(self.tools.rglob("*.part")))
        self.env.pop("MOCK_CURL_FAIL")
        self.setup_tools()

    def test_sdk_failure_is_not_reported_as_success(self):
        self.env["MOCK_SDK_FAIL"]="1"
        result=self.run_build("--setup",ok=False)
        self.assertIn("sdkmanager failed",result.stdout)

    def test_wrong_ndk_revision_is_rejected(self):
        self.setup_tools()
        (self.sdk/"ndk/27.1.12297006/source.properties").write_text("Pkg.Revision = 27.3.13750724\n")
        result=self.run_build("--check-tools",ok=False)
        self.assertIn("does not match",result.stdout)

    def test_external_ndk_path_is_forwarded_to_gradle(self):
        self.setup_tools(); self.native_fixtures()
        custom=self.base/"custom ndk"
        shutil.copytree(self.sdk/"ndk/27.1.12297006",custom)
        self.env["ANDROID_NDK_HOME"]=str(custom)
        self.run_build("--no-assets")
        self.assertIn("-PmugenNdkPath="+str(custom),json.loads(self.gradle_log.read_text()))

    def test_explicit_bad_java_is_not_silently_replaced(self):
        self.env["MUGEN_JAVA_HOME"]=str(self.base/"missing jdk")
        result=self.run_build("--setup",ok=False)
        self.assertIn("MUGEN_JAVA_HOME",result.stdout)
        self.assertFalse(self.curl_log.exists())

    def test_unsupported_abi_and_host(self):
        self.env["ANDROID_ABI"]="armeabi-v7a"
        self.assertIn("arm64-v8a only",self.run_build("--setup",ok=False).stdout)
        self.env.pop("ANDROID_ABI")
        self.env["MOCK_ARCH"]="aarch64"
        self.assertIn("Unsupported Android build host",self.run_build("--setup",ok=False).stdout)
        self.assertFalse(self.curl_log.exists())

    def test_offline_refuses_download_flags_and_uncached_wrapper(self):
        self.assertIn("cannot be combined",self.run_build("--setup","--offline",ok=False).stdout)
        self.setup_tools(); self.native_fixtures()
        self.assertIn("distribution is not cached",self.run_build("--no-assets","--offline",ok=False).stdout)

    def test_offline_rejects_incomplete_wrapper_cache(self):
        self.setup_tools(); self.native_fixtures()
        cached=Path(self.env["GRADLE_USER_HOME"])/"wrapper/dists/gradle-8.1.1-bin/hash/gradle-8.1.1/bin"
        cached.mkdir(parents=True); (cached/"gradle").write_text("partial")
        self.assertIn("incomplete",self.run_build("--no-assets","--offline",ok=False).stdout)

    def test_offline_cached_build_makes_no_bootstrap_network_calls(self):
        self.setup_tools(); self.native_fixtures()
        cached=Path(self.env["GRADLE_USER_HOME"])/"wrapper/dists/gradle-8.1.1-bin/hash/gradle-8.1.1/bin"
        cached.mkdir(parents=True)
        shutil.copyfile(self.android/"gradlew",cached/"gradle")
        (cached.parent/"lib").mkdir()
        (cached.parent/"lib/gradle-launcher-8.1.1.jar").write_text("fixture")
        (cached.parent.parent/"gradle-8.1.1-bin.zip.ok").write_text("")
        before=self.curl_log.read_text()
        self.run_build("--no-assets","--offline")
        self.assertIn("--offline",json.loads(self.gradle_log.read_text()))
        self.assertEqual(self.curl_log.read_text(),before)


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=WORK)
        self.root=Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_checksum_rejects_changed_payload(self):
        p=self.root/"data"; p.write_bytes(b"payload")
        metadata.verify(p,"sha256",hashlib.sha256(b"payload").hexdigest())
        p.write_bytes(b"changed")
        with self.assertRaises(ValueError): metadata.verify(p,"sha256",hashlib.sha256(b"payload").hexdigest())

    def test_zip_traversal_is_rejected(self):
        p=self.root/"bad.zip"
        with zipfile.ZipFile(p,"w") as archive: archive.writestr("../escaped","bad")
        with self.assertRaises(ValueError): metadata.extract(p,self.root/"out")
        self.assertFalse((self.root/"escaped").exists())

    def test_valid_tar_relative_symlink_and_escaping_link(self):
        p=self.root/"valid.tar.gz"
        with tarfile.open(p,"w:gz") as archive:
            info=tarfile.TarInfo("jdk/lib/data"); info.size=1
            archive.addfile(info,io.BytesIO(b"x"))
            link=tarfile.TarInfo("jdk/bin/link"); link.type=tarfile.SYMTYPE; link.linkname="../lib/data"
            archive.addfile(link)
        metadata.extract(p,self.root/"out")
        self.assertEqual((self.root/"out/jdk/bin/link").read_bytes(),b"x")
        bad=self.root/"bad.tar.gz"
        with tarfile.open(bad,"w:gz") as archive:
            link=tarfile.TarInfo("link"); link.type=tarfile.SYMTYPE; link.linkname="../outside"
            archive.addfile(link)
        with self.assertRaises(ValueError): metadata.extract(bad,self.root/"bad-out")

    def test_metadata_rejects_missing_package_and_bad_jdk_url(self):
        p=self.root/"sdk.xml"; p.write_text("<repository/>")
        with self.assertRaises(ValueError): metadata.sdk_record(p,"linux","19.0")
        p=self.root/"jdk.json"
        p.write_text(json.dumps([{"binary":{"package":{"link":"http://example.com/a.zip","checksum":"0"*64}}}]))
        with self.assertRaises(ValueError): metadata.jdk_record(p)


if __name__=="__main__":
    unittest.main(verbosity=2)
