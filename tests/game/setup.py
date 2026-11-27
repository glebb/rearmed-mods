#!/usr/bin/env python3
"""The game tests' installs of both editions (tests/game/DESIGN.md), made once and refreshed from the builds.

    tests/game/setup.py [--edition mac|lp|both] [--builds main|installed] [--root DIR] [--games-dir DIR]
                        [--fresh-saves] [--refresh-game] [--warm-shaders]

<root> is --root, else $RS_GAMETEST_ROOT, else ~/rs-gametest. What it puts there:

    mac/home/                     the Mac test install (RS_NATIVE_HOME), by rs-rearmed's tools/play-native/install.sh
                                  with RS_NATIVE_HOME, RS_NATIVE_LAUNCHER, RS_DLC_FETCH=0 and RS_GAME_COPY_DIR:
      game -> <games>/mac/game    its copy of the game files: APFS clones of the Steam download the player's install
                                  is made from (on that drive; a link, as install.sh makes for a game on another
                                  drive), the build's app with rsmsl-corpus.bin from the player's app, re-signed
      steam/, mods/, logs/, libnetguard.dylib
    mac/RS Re-ARMed Test.app      the test install's launcher (bundle id ...launcher.gametest; runs start the game's
                                  executable directly)
    mac/steam-seed/               the player's saves as copied (no crd): steam/ is made from it once, and again with
                                  --fresh-saves
    lp/                           the Learn & Play test folder:
      game -> <games>/lp/game     clones of the player's learn-play/game (the files its manifest lists, .rearmed/
                                  with steam.env, the title logo, Update/patcharchives.psarc; not the player's
                                  RSMods.ini, saves backups or songs put in dlc by hand)
      userdata/, steam/           copies of the player's learn-play/userdata and learn-play/steam (and -seed/)
      out/                        the build's rs2014_lp, lp_game.exe, dlls/, libnetguard.dylib, libRSOpenGL.dylib,
                                  rsmsl-corpus.bin
      mods/
    tools/libkeyfifo.dylib        rs-rearmed's tools/ws8/keyfifo, built
    build/librscompanion.dylib    the companion's in-game library (--builds main), built as its app/build.sh does
    builds.json                   each part: source, where it went, repo commit, sha256; and per edition how a run
                                  starts it (executable, folder, environment)
    profiles.json                 per edition the test saves' account and profiles, the two for co-op

<games> is --games-dir, else $RS_GAMETEST_GAMES, else rs-gametest on the drive the games are on (here
/Volumes/Storage): clones take no room only on a game's own volume, and both drives are nearly full.

--builds main (default): the Mac app from rs-rearmed's build/m4/app, Learn & Play from build/win-x86/lp/out, the
three mods from their repositories' builds. --builds installed: the player's game app, Learn & Play app and mods.

Every mod is replaced by rm + copy (never written in place). Each edition's dlc/gametest/ is made (empty). Nothing
of the player's install is written: every place written is checked to be under <root> or <games> first, and the
player's saves are only read (and copied). --fresh-saves copies them again (a test changed or broke the saves).
A second run with nothing changed takes seconds: install.sh runs only when the app or install.sh changed, the game
copy is incomplete, or --refresh-game is given; other files are compared by size and time.
--warm-shaders lets install.sh compile the Metal shaders (it starts the game's executable with --warm-shaders, no
window; a few minutes the first time); without it the game compiles them as it needs them.
"""

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import time
import zlib

HOME = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))
MODS_REPO = os.path.dirname(os.path.dirname(HERE))
REARMED = os.path.expanduser(os.environ.get("RS_REARMED_DIR") or "~/src/rs-rearmed")
SONGBRIDGE = os.path.expanduser(os.environ.get("RS_SONGBRIDGE_DIR") or "~/src/rearmed-songbridge")
COMPANION = os.path.expanduser(os.environ.get("RS_COMPANION_DIR") or "~/rs-mac-companion")
PLAYER = os.path.join(HOME, "Library", "Application Support", "RS Re-ARMed")
PLAYER_LP = os.path.join(PLAYER, "learn-play")
PLAYER_LAUNCHER = os.path.join(HOME, "Applications", "RS Re-ARMed.app")
PLAYER_LP_APP = os.path.join(HOME, "Applications", "RS Re-ARMed Learn & Play.app")
APPLICATIONS = os.path.join(HOME, "Applications")
INSTALL_SH = os.path.join(REARMED, "tools", "play-native", "install.sh")
GAMEFILES = os.path.join(REARMED, "tools", "play-native", "gamefiles.py")
KEYFIFO_DIR = os.path.join(REARMED, "tools", "ws8", "keyfifo")
STEAMID64_BASE = 76561197960265728
PROFILE_KEY = "728B369E24ED0134768511021812AFC0A3C25D02065F166B4BCC58CD2644F29E"  # RSMods' PCSaveKey
MIN_FREE = 3 << 30
LAUNCHER_ID = "io.github.bbelgg.rs-rearmed.launcher.gametest"
MOD_FILES = ("rearmed-mods.dylib", "songbridge.dylib", "librscompanion.dylib")


class Fail(Exception):
    pass


def say(*a):
    print(*a, flush=True)


# --- small helpers -------------------------------------------------------------------------------------

def real(p):
    return os.path.realpath(os.path.expanduser(p))


def inside(p, top):
    p, top = real(p), real(top)
    return p == top or p.startswith(top.rstrip(os.sep) + os.sep)


def free_bytes(p):
    while not os.path.exists(p):
        p = os.path.dirname(p)
    st = os.statvfs(p)
    return st.f_bavail * st.f_frsize


def need_room(path, nbytes, what):
    free = free_bytes(path)
    if free - nbytes < MIN_FREE:
        raise Fail("%s needs %.1f GB on %s, which has %.1f GB free: that would leave under 3 GB"
                   % (what, nbytes / 1e9, path, free / 1e9))


def load_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_json(path, data):
    tmp = path + ".part"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, path)


class Hashes:
    """sha256 of files, remembered by (path, size, mtime) in <root>/.setup-hashes.json."""

    def __init__(self, path):
        self.path = path
        self.d = load_json(path) or {}

    def __call__(self, p):
        st = os.stat(p)
        key = "%s|%d|%d" % (real(p), st.st_size, st.st_mtime_ns)
        if key not in self.d:
            h = hashlib.sha256()
            with open(p, "rb") as f:
                for b in iter(lambda: f.read(1 << 20), b""):
                    h.update(b)
            self.d[key] = h.hexdigest()
        return self.d[key]

    def save(self):
        live = {}
        for k, v in self.d.items():
            p = k.rsplit("|", 2)[0]
            try:
                st = os.stat(p)
            except OSError:
                continue
            if k == "%s|%d|%d" % (p, st.st_size, st.st_mtime_ns):
                live[k] = v
        write_json(self.path, live)


_libc = ctypes.CDLL("libc.dylib", use_errno=True)
_libc.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]


def clone(src, dst):
    """An APFS clone of SRC at DST (which must not exist); False when the volumes differ or don't clone."""
    if _libc.clonefile(os.fsencode(src), os.fsencode(dst), 0) == 0:
        return True
    err = ctypes.get_errno()
    if err in (18, 45, 78):  # EXDEV, ENOTSUP, ENOSYS
        return False
    raise OSError(err, os.strerror(err), dst)


def same_file(src, dst):
    try:
        a, b = os.stat(src), os.lstat(dst)
    except OSError:
        return False
    return stat.S_ISREG(b.st_mode) and a.st_size == b.st_size and int(a.st_mtime) == int(b.st_mtime)


def place_file(src, dst, allow_copy=True):
    """DST as SRC: left alone when the size and time are SRC's, else removed and made again (a clone where the
    volume allows, else a copy that keeps the time; never written in place). -> 'kept', 'cloned' or 'copied'."""
    if same_file(src, dst):
        return "kept"
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.lexists(dst):
        os.remove(dst)
    if clone(src, dst):
        return "cloned"
    if not allow_copy:
        raise Fail("%s: cannot clone it to %s (another volume), and a game is never copied" % (src, dst))
    shutil.copy2(src, dst)
    return "copied"


def git_info(repo):
    def g(*a):
        r = subprocess.run(["git", "-C", repo] + list(a), capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None
    commit = g("rev-parse", "HEAD")
    if not commit:
        return {"repo": repo, "commit": None}
    return {"repo": repo, "commit": commit, "branch": g("rev-parse", "--abbrev-ref", "HEAD"),
            "dirty": bool(g("status", "--porcelain", "--untracked-files=no")),
            "commit_time": g("log", "-1", "--format=%cI")}


def run(cmd, env=None, cwd=None, prefix="  | "):
    """Runs CMD, its output shown (install.sh's progress lines left out). -> exit status."""
    p = subprocess.Popen(cmd, env=env, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in p.stdout:
        line = line.rstrip("\n")
        if line.startswith("progress ") or not line.strip():
            continue
        say(prefix + line)
    return p.wait()


def codesign(path):
    r = subprocess.run(["codesign", "--force", "-s", "-", path], capture_output=True, text=True)
    if r.returncode:
        raise Fail("codesign %s: %s" % (path, r.stderr.strip()))


def game_running():
    return any(subprocess.run(["/usr/bin/pgrep", "-x", n], capture_output=True).returncode == 0
               for n in ("Rocksmith2014", "rs2014_lp"))


def copy_saves(src, dst):
    """A copy of the saves folder SRC at DST (made whole, then renamed in): no crd (stored credentials, as
    tools/perf/run.py), no Finder files; links copied as links."""
    if not os.path.isdir(src):
        raise Fail("no saves at %s" % src)
    tmp = dst + ".part"
    if os.path.lexists(tmp):
        shutil.rmtree(tmp)
    shutil.copytree(src, tmp, symlinks=True, ignore=shutil.ignore_patterns("crd", ".DS_Store"))
    if os.path.lexists(dst):
        shutil.rmtree(dst)
    os.rename(tmp, dst)


# --- the setup -----------------------------------------------------------------------------------------

class Setup:
    def __init__(self, a):
        self.a = a
        self.root = real(a.root or os.environ.get("RS_GAMETEST_ROOT") or "~/rs-gametest")
        self.games = real(a.games_dir or os.environ.get("RS_GAMETEST_GAMES") or self.default_games())
        self.done = []
        os.makedirs(self.root, exist_ok=True)
        self.hash = Hashes(os.path.join(self.root, ".setup-hashes.json"))
        self.state_path = os.path.join(self.root, ".setup-state.json")
        self.state = load_json(self.state_path) or {}
        self.builds = load_json(os.path.join(self.root, "builds.json")) or {}
        self.mac_home = os.path.join(self.root, "mac", "home")
        self.mac_launcher = os.path.join(self.root, "mac", "RS Re-ARMed Test.app")
        self.lp = os.path.join(self.root, "lp")
        for p in (self.root, self.games):
            for player in (PLAYER, APPLICATIONS):
                if inside(p, player) or inside(player, p):
                    raise Fail("%s overlaps the player's %s: choose another --root / --games-dir" % (p, player))

    @staticmethod
    def default_games():
        """rs-gametest at the top of the drive that holds the Mac game's source (the Steam download the player's
        install is made from) and the player's Learn & Play copy."""
        src = None
        try:
            with open(os.path.join(PLAYER, "game-source")) as f:
                src = f.read().strip()
        except OSError:
            pass
        for p in (src, os.path.join(PLAYER_LP, "game")):
            if p and os.path.exists(p):
                rp = real(p)
                if rp.startswith("/Volumes/"):
                    return os.path.join("/Volumes", rp.split("/")[2], "rs-gametest")
        return os.path.join(HOME, "rs-gametest-games")

    def guard(self, p, what):
        """Refuses a place to write that is not the test's own."""
        if not (inside(p, self.root) or inside(p, self.games)):
            raise Fail("%s (%s) is outside %s and %s: refused" % (what, p, self.root, self.games))
        for player in (PLAYER, PLAYER_LAUNCHER, PLAYER_LP_APP):
            if inside(p, player):
                raise Fail("%s (%s) is in the player's %s: refused" % (what, p, player))
        if inside(p, APPLICATIONS):
            raise Fail("%s (%s) is in ~/Applications: refused" % (what, p))

    # --- the parts -------------------------------------------------------------------------------------

    def parts(self, edition):
        """{part: (source path, repo or None)} for an edition and --builds."""
        installed = self.a.builds == "installed"
        if edition == "mac":
            app = (os.path.join(PLAYER, "game", "Rocksmith2014.app") if installed
                   else os.path.join(REARMED, "build", "m4", "app", "Rocksmith2014.app"))
            p = {"game": (app, None if installed else REARMED)}
            mods = os.path.join(PLAYER, "mods")
        else:
            out = PLAYER_LP_APP if installed else os.path.join(REARMED, "build", "win-x86", "lp", "out")
            p = {"game": (out, None if installed else REARMED)}
            mods = os.path.join(PLAYER_LP, "mods")
        if installed:
            for f in MOD_FILES:
                p[f] = (os.path.join(mods, f), None)
        else:
            p["rearmed-mods.dylib"] = (os.path.join(MODS_REPO, "build", "rearmed-mods.dylib"), MODS_REPO)
            p["songbridge.dylib"] = (os.path.join(SONGBRIDGE, "build", "songbridge.dylib"), SONGBRIDGE)
            p["librscompanion.dylib"] = (self.companion(), COMPANION)
        for name, (path, _) in p.items():
            if not os.path.exists(path):
                raise Fail("%s for %s (--builds %s) is not there: %s" % (name, edition, self.a.builds, path))
        return p

    def companion(self):
        """The companion's in-game library as its app/build.sh builds it (arm64 + x86_64), into <root>/build,
        again only when its sources changed."""
        out = os.path.join(self.root, "build", "librscompanion.dylib")
        if getattr(self, "_companion", None):
            return out
        srcs = [os.path.join(COMPANION, "native", n) for n in ("rscompanion.c", "rscore.c", "rsfile.c", "rscore.h",
                                                               "rsfile.h")]
        key = hashlib.sha256(b"".join(open(s, "rb").read() for s in srcs if os.path.exists(s))).hexdigest()
        if self.state.get("companion_src") != key or not os.path.isfile(out):
            os.makedirs(os.path.dirname(out), exist_ok=True)
            tmp = out + ".new"
            cmd = ["clang", "-arch", "x86_64", "-arch", "arm64", "-O2", "-Wall", "-dynamiclib", "-install_name",
                   "@executable_path/librscompanion.dylib", "-o", tmp] + srcs[:3] + ["-lz"]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                raise Fail("the companion's library did not build: %s" % r.stderr.strip()[-500:])
            codesign(tmp)
            if os.path.lexists(out):
                os.remove(out)
            os.rename(tmp, out)
            self.state["companion_src"] = key
            self.done.append("built %s" % out)
        self._companion = True
        return out

    def keyfifo(self):
        out_dir = os.path.join(self.root, "tools")
        out = os.path.join(out_dir, "libkeyfifo.dylib")
        src = os.path.join(KEYFIFO_DIR, "keyfifo.m")
        key = self.hash(src) + self.hash(os.path.join(KEYFIFO_DIR, "build.sh"))
        if self.state.get("keyfifo_src") != key or not os.path.isfile(out):
            if run(["bash", os.path.join(KEYFIFO_DIR, "build.sh"), out_dir]):
                raise Fail("keyfifo did not build")
            self.state["keyfifo_src"] = key
            self.done.append("built %s" % out)
        return {"path": out, "source": src, "sha256": self.hash(out), **git_info(REARMED)}

    def part_info(self, path, repo, installed_at=None, sha_of=None):
        info = {"source": path, "sha256": self.hash(sha_of or path),
                "built": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.stat(sha_of or path).st_mtime))}
        if installed_at:
            info["installed"] = installed_at
        if repo:
            info.update(git_info(repo))
            ct = info.get("commit_time")
            if ct and info["built"].replace(" ", "T") < ct[:19]:
                info["note"] = "built before the repository's last commit: rebuild it if that commit changed it"
        else:
            info["from"] = "the player's install (no repository commit)"
        return info

    def mods(self, edition, parts, mods_dir):
        self.guard(mods_dir, "mods folder")
        os.makedirs(mods_dir, exist_ok=True)
        info = {}
        for f in MOD_FILES:
            src, repo = parts[f]
            dst = os.path.join(mods_dir, f)
            if not (os.path.isfile(dst) and self.hash(dst) == self.hash(src)):
                if os.path.lexists(dst):
                    os.remove(dst)  # rm + copy: an overwrite in place gets the game killed by the signature cache
                shutil.copy2(src, dst)
                self.done.append("%s: %s" % (edition, dst))
            info[f] = self.part_info(src, repo, dst)
        for extra in os.listdir(mods_dir):
            if extra not in MOD_FILES and not extra.startswith("."):
                self.done.append("%s: note: %s in %s is not one of the three mods (left there)" % (edition, extra, mods_dir))
        return info

    # --- the Mac edition -------------------------------------------------------------------------------

    def mac_install_needed(self, app, why):
        st = self.state.get("mac") or {}
        exe = os.path.join(app, "Contents", "MacOS", "Rocksmith2014")
        game = os.path.join(self.mac_home, "game")
        if self.a.refresh_game:
            why.append("--refresh-game")
        if st.get("app") != real(app) or st.get("app_sha256") != self.hash(exe):
            why.append("a new app")
        if st.get("install_sh") != self.hash(INSTALL_SH):
            why.append("install.sh changed")
        if not os.path.isfile(os.path.join(game, "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014")):
            why.append("no app in the test install")
        if subprocess.run([sys.executable, "-I", GAMEFILES, "check", game, "--quiet"], capture_output=True).returncode:
            why.append("the game copy is missing or incomplete")
        return bool(why)

    def check_install_sh(self):
        """install.sh writes where RS_NATIVE_HOME and RS_NATIVE_LAUNCHER say, its game copy where RS_GAME_COPY_DIR
        says: refused when the script no longer reads them."""
        text = open(INSTALL_SH).read()
        for needed in ('home=${RS_NATIVE_HOME:-', 'launcher=${RS_NATIVE_LAUNCHER:-', 'RS_DLC_FETCH',
                       '"$here/warm-shaders.sh" "$home/game/Rocksmith2014.app"'):
            if needed not in text:
                raise Fail("install.sh no longer has %r: read it again before letting this script run it" % needed)
        for p in re.findall(r'"(\$HOME/[^"]*)"', text):
            if "RS_NATIVE_" not in text[max(0, text.find(p) - 60):text.find(p)]:
                raise Fail("install.sh writes to %s, not through RS_NATIVE_HOME/LAUNCHER: refused" % p)

    def mac(self):
        say("Mac edition (%s builds)" % self.a.builds)
        parts = self.parts("mac")
        app = parts["game"][0]
        home, launcher = self.mac_home, self.mac_launcher
        copy_dir = os.path.join(self.games, "mac")
        for p, what in ((home, "the test home"), (launcher, "the test launcher"), (copy_dir, "the game copy")):
            self.guard(p, what)
        game = os.path.join(home, "game")
        if os.path.lexists(game):
            self.guard(game, "the test home's game")
        why = []
        if self.mac_install_needed(app, why):
            if game_running():
                raise Fail("a Rocksmith 2014 is running: quit it first (install.sh and the game copy refuse)")
            self.check_install_sh()
            need_room(copy_dir, 400 << 20, "the Mac test install (the app, the overlay, the title logo)")
            netguard = os.path.join(REARMED, "build", "ws8", "libnetguard.dylib")
            if not os.path.isfile(netguard):  # install.sh needs it there; its own build script makes it
                if run(["bash", os.path.join(REARMED, "tools", "ws8", "netguard", "build.sh")], cwd=REARMED):
                    raise Fail("libnetguard did not build")
                self.done.append("mac: built %s (install.sh needs it)" % netguard)
            src = None
            try:
                with open(os.path.join(PLAYER, "game-source")) as f:
                    src = f.read().strip()
            except OSError:
                pass
            env = dict(os.environ, RS_NATIVE_HOME=home, RS_NATIVE_LAUNCHER=launcher, RS_DLC_FETCH="0",
                       RS_GAME_COPY_DIR=copy_dir, RS_REARMED="0",  # the title logo: the player's, below
                       RS_WARM_SHADERS="1" if self.a.warm_shaders else "0")
            if src and os.path.isdir(src):
                if inside(src, PLAYER):
                    raise Fail("the player's game source %s is inside the player's install: refused" % src)
                env["RS_GAME_DIR"] = src
            say("  install.sh (%s): %s -> %s" % (", ".join(why), app, home))
            os.makedirs(os.path.join(self.root, "mac"), exist_ok=True)
            if run(["bash", INSTALL_SH, app], env=env, cwd=REARMED):
                raise Fail("install.sh failed (see above)")
            self.guard(game, "the test home's game")  # where install.sh's copy went
            self.state["mac"] = {"app": real(app), "app_sha256": self.hash(os.path.join(app, "Contents", "MacOS",
                                                                                      "Rocksmith2014")),
                                 "install_sh": self.hash(INSTALL_SH), "corpus": None}
            self.done.append("mac: install.sh ran (%s)" % ", ".join(why))
        else:
            say("  install.sh not needed (the same app and install.sh, the game copy complete)")
        st = self.state["mac"]
        tapp = os.path.join(game, "Rocksmith2014.app")
        exe = os.path.join(tapp, "Contents", "MacOS", "Rocksmith2014")
        # rsmsl-corpus.bin: the build's app lacks it (the player's app has it); added, then the executable re-signed
        corpus_src = os.path.join(PLAYER, "game", "Rocksmith2014.app", "Contents", "Resources", "rsmsl-corpus.bin")
        corpus = os.path.join(tapp, "Contents", "Resources", "rsmsl-corpus.bin")
        if os.path.isfile(corpus_src) and not (os.path.isfile(corpus) and self.hash(corpus) == self.hash(corpus_src)):
            if os.path.lexists(corpus):
                os.remove(corpus)
            shutil.copy2(corpus_src, corpus)
            codesign(exe)
            self.done.append("mac: rsmsl-corpus.bin from the player's app, executable re-signed")
        # the title logo: the player's static.psarc.rearmed when it was made from the same static.psarc
        logo, original = os.path.join(game, "static.psarc.rearmed"), os.path.join(game, "static.psarc.original")
        p_logo = os.path.join(PLAYER, "game", "static.psarc.rearmed")
        p_orig = os.path.join(PLAYER, "game", "static.psarc.original")
        if not os.path.isfile(logo) and os.path.isfile(p_logo) and os.path.isfile(original) and \
                os.path.isfile(p_orig) and self.hash(p_orig) == self.hash(original):
            need_room(game, os.path.getsize(p_logo), "the title logo")
            place_file(p_logo, logo)
            self.done.append("mac: the title logo (static.psarc.rearmed) from the player's copy")
        self.static_link(game)
        # the launcher: a bundle id of its own, so nothing that opens the player's launcher by id finds this one
        plist = os.path.join(launcher, "Contents", "Info.plist")
        if os.path.isfile(plist):
            cur = subprocess.run(["/usr/libexec/PlistBuddy", "-c", "Print :CFBundleIdentifier", plist],
                                 capture_output=True, text=True).stdout.strip()
            if cur != LAUNCHER_ID:
                for k, v in (("CFBundleIdentifier", LAUNCHER_ID), ("CFBundleName", "RS Re-ARMed Test"),
                             ("CFBundleDisplayName", "RS Re-ARMed Test")):
                    subprocess.run(["plutil", "-replace", k, "-string", v, plist], check=True)
                codesign(launcher)
                ls = ("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/"
                      "Support/lsregister")
                if os.path.exists(ls):
                    subprocess.run([ls, "-f", launcher], capture_output=True)
                self.done.append("mac: the test launcher's bundle id is %s" % LAUNCHER_ID)
        self.rocksmith_ini(game)
        # saves: a copy of the player's (never a link), kept between runs; --fresh-saves copies them again
        seed, steam = os.path.join(self.root, "mac", "steam-seed"), os.path.join(home, "steam")
        self.saves(os.path.join(PLAYER, "steam"), seed, steam, "mac")
        mods = self.mods("mac", parts, os.path.join(home, "mods"))
        os.makedirs(os.path.join(game, "dlc", "gametest"), exist_ok=True)
        netguard = os.path.join(home, "libnetguard.dylib")
        info = {"game": self.part_info(app, parts["game"][1], tapp, os.path.join(app, "Contents", "MacOS",
                                                                                 "Rocksmith2014")),
                "netguard": self.part_info(os.path.join(REARMED, "build", "ws8", "libnetguard.dylib"), REARMED,
                                           netguard)}
        info["game"]["installed_sha256"] = self.hash(exe)
        info["game"]["copy"] = real(game)
        info["game"]["copy_source"] = (load_json(os.path.join(game, ".rearmed", "source.json")) or {}).get("source")
        info.update(mods)
        launch = {
            "exe": exe, "cwd": game, "home": home, "game": game, "steam": steam, "mods": os.path.join(home, "mods"),
            "dlc_test": os.path.join(game, "dlc", "gametest"), "rsmods_ini": os.path.join(game, "RSMods.ini"),
            "rocksmith_ini": os.path.join(game, "Rocksmith.ini"), "steam_env": os.path.join(game, ".rearmed",
                                                                                          "steam.env"),
            "insert": [netguard, os.path.join(self.root, "tools", "libkeyfifo.dylib")],
            "env": {"RS_STEAM_MODE": "stub", "RS_STEAM_DATA_DIR": steam, "RS_NATIVE_HOME": home,
                    "RS_MODS_PATH": os.path.join(home, "mods"), "RS_NETGUARD_LOG": os.path.join(home, "logs",
                                                                                                "netguard.log")},
            "note": "as tools/play-native/launch.sh starts it: also the steam_env file's RS_STEAM_* lines, "
                    "DYLD_INSERT_LIBRARIES = insert joined by ':' (the executable started directly, not by env or "
                    "nohup, which drop DYLD_ variables)",
        }
        return info, launch

    @staticmethod
    def static_link(game):
        """static.psarc -> static.psarc.rearmed when the copy has it, else static.psarc.original (the launchers do
        this at each start; runs start the executable directly)."""
        target = "static.psarc.rearmed" if os.path.isfile(os.path.join(game, "static.psarc.rearmed")) \
            else "static.psarc.original"
        link = os.path.join(game, "static.psarc")
        if os.path.islink(link) and os.readlink(link) == target:
            return
        if os.path.lexists(link):
            os.remove(link)
        os.symlink(target, link)

    def rocksmith_ini(self, game):
        """game/Rocksmith.ini, once, from the copy's .rearmed/Rocksmith.ini as launch.sh makes it: [Net]
        AutoLogin=0, [Renderer.MacOS] DisableBrowser=1 (offline, no Uplay). A run may pin its own keys later."""
        ini = os.path.join(game, "Rocksmith.ini")
        if os.path.isfile(ini):
            return
        src = os.path.join(game, ".rearmed", "Rocksmith.ini")
        lines = open(src, newline="").read().replace("\r\n", "\n").split("\n") if os.path.isfile(src) else []
        out, sec, net, ren = [], None, False, False
        for line in lines:
            if line.startswith("["):
                sec = line
            if (sec == "[Net]" and line.startswith("AutoLogin=")) or \
                    (sec == "[Renderer.MacOS]" and line.startswith("DisableBrowser=")):
                continue
            out.append(line)
            if line == "[Net]":
                out.append("AutoLogin=0")
                net = True
            if line == "[Renderer.MacOS]":
                out.append("DisableBrowser=1")
                ren = True
        while out and out[-1] == "":
            out.pop()
        if not net:
            out += ["[Net]", "AutoLogin=0"]
        if not ren:
            out += ["[Renderer.MacOS]", "DisableBrowser=1"]
        with open(ini, "w") as f:
            f.write("\n".join(out) + "\n")
        self.done.append("mac: %s made from .rearmed/Rocksmith.ini" % ini)

    def saves(self, player_dir, seed, work, edition, what="saves"):
        self.guard(seed, "the %s seed" % what)
        self.guard(work, "the test %s" % what)
        if self.a.fresh_saves or not os.path.isdir(seed):
            copy_saves(player_dir, seed)
            self.done.append("%s: %s copied from %s" % (edition, seed, player_dir))
            if os.path.isdir(work):
                shutil.rmtree(work)
        empty = not os.path.isdir(work) or not [n for n in os.listdir(work) if not n.startswith(".")]
        if empty:
            copy_saves(seed, work)
            self.done.append("%s: %s made from %s" % (edition, work, seed))

    # --- Learn & Play ----------------------------------------------------------------------------------

    def lp_game(self):
        """<root>/lp/game -> <games>/lp/game: clones of the player's copy, the files its manifest lists and the
        copy's own (.rearmed, the title logo, the menu overlay); files of an earlier run gone from the list removed."""
        src = real(os.path.join(PLAYER_LP, "game"))
        manifest = load_json(os.path.join(src, ".rearmed", "manifest.json"))
        if not manifest or not isinstance(manifest.get("files"), dict):
            raise Fail("the player's Learn & Play copy %s has no manifest (incomplete): refresh it first" % src)
        dest = os.path.join(self.games, "lp", "game")
        link = os.path.join(self.lp, "game")
        self.guard(dest, "the Learn & Play game copy")
        self.guard(link, "the Learn & Play game link")
        os.makedirs(dest, exist_ok=True)
        if os.stat(dest).st_dev != os.stat(src).st_dev:
            raise Fail("%s is not on the volume of %s: the clones would be copies of a game" % (dest, src))
        if real(link) != real(dest):
            if os.path.lexists(link):
                if os.path.islink(link) or (os.path.isdir(link) and not os.listdir(link)):
                    os.remove(link) if os.path.islink(link) else os.rmdir(link)
                else:
                    raise Fail("%s is there and is not the link to %s" % (link, dest))
            os.makedirs(self.lp, exist_ok=True)
            os.symlink(dest, link)
        files = sorted(manifest["files"])
        for n in os.listdir(os.path.join(src, ".rearmed")):
            if n not in ("link.json",) and os.path.isfile(os.path.join(src, ".rearmed", n)):
                files.append(os.path.join(".rearmed", n))
        for n in ("static.psarc.rearmed", os.path.join("Update", "patcharchives.psarc")):
            if os.path.isfile(os.path.join(src, n)):
                files.append(n)
        counts = {}
        for rel in files:
            how = place_file(os.path.join(src, rel), os.path.join(dest, rel), allow_copy=rel.startswith(".rearmed"))
            counts[how] = counts.get(how, 0) + 1
        listed = os.path.join(dest, ".gametest-files.json")
        before = (load_json(listed) or {}).get("files", [])
        gone = [r for r in before if r not in set(files)]
        for rel in gone:
            p = os.path.join(dest, rel)
            if os.path.isfile(p) and not os.path.islink(p):
                os.remove(p)
        write_json(listed, {"source": src, "files": files})
        self.static_link(dest)
        os.makedirs(os.path.join(dest, "dlc", "gametest"), exist_ok=True)
        if counts.get("cloned") or counts.get("copied") or gone:
            self.done.append("lp: game copy %s: %s%s" % (dest, ", ".join("%d %s" % (v, k) for k, v in counts.items()),
                                                         ", %d removed" % len(gone) if gone else ""))
        return dest, src

    def lp_out(self, source):
        """<root>/lp/out: the build's files (an installed app's, from its bundle), each replaced when it changed."""
        out = os.path.join(self.lp, "out")
        self.guard(out, "Learn & Play's out/")
        if source.endswith(".app"):
            m, r = os.path.join(source, "Contents", "MacOS"), os.path.join(source, "Contents", "Resources")
            want = {"rs2014_lp": m, "libRSOpenGL.dylib": m, "libnetguard.dylib": m, "lp_game.exe": r,
                    "rsmsl-corpus.bin": r}
            dlls = os.path.join(r, "dlls")
        else:
            want = {n: source for n in ("rs2014_lp", "libRSOpenGL.dylib", "libnetguard.dylib", "lp_game.exe",
                                        "rsmsl-corpus.bin")}
            dlls = os.path.join(source, "dlls")
        pairs = [(os.path.join(d, n), os.path.join(out, n)) for n, d in want.items()]
        pairs += [(os.path.join(dlls, n), os.path.join(out, "dlls", n)) for n in sorted(os.listdir(dlls))
                  if not n.startswith(".")]
        need = sum(os.path.getsize(s) for s, d in pairs if not same_file(s, d))
        if need:
            need_room(out, need, "Learn & Play's out/")
        changed = []
        for s, d in pairs:
            if not os.path.isfile(s):
                raise Fail("%s is missing" % s)
            if place_file(s, d) != "kept":
                changed.append(os.path.relpath(d, out))
        wanted = {os.path.relpath(d, out) for _, d in pairs}
        for base, _, names in os.walk(out):
            for n in names:
                rel = os.path.relpath(os.path.join(base, n), out)
                if rel not in wanted:
                    os.remove(os.path.join(base, n))
                    changed.append("-" + rel)
        if changed:
            self.done.append("lp: out/: %s" % ", ".join(changed))
        return out, {os.path.relpath(d, out): self.hash(d) for _, d in pairs}

    def lp_run(self):
        say("Learn & Play (%s builds)" % self.a.builds)
        parts = self.parts("lp")
        self.guard(self.lp, "the Learn & Play folder")
        os.makedirs(self.lp, exist_ok=True)
        game, src = self.lp_game()
        self.saves(os.path.join(PLAYER_LP, "userdata"), os.path.join(self.lp, "userdata-seed"),
                   os.path.join(self.lp, "userdata"), "lp", "user data")
        self.saves(os.path.join(PLAYER_LP, "steam"), os.path.join(self.lp, "steam-seed"),
                   os.path.join(self.lp, "steam"), "lp")
        out, out_files = self.lp_out(parts["game"][0])
        mods = self.mods("lp", parts, os.path.join(self.lp, "mods"))
        exe_src = parts["game"][0]
        exe_src = os.path.join(exe_src, "Contents", "MacOS", "rs2014_lp") if exe_src.endswith(".app") \
            else os.path.join(exe_src, "rs2014_lp")
        info = {"game": self.part_info(parts["game"][0], parts["game"][1], out, exe_src)}
        info["game"]["files"] = out_files
        info["game"]["copy"] = game
        info["game"]["copy_source"] = src
        info.update(mods)
        launch = {
            "exe": os.path.join(out, "rs2014_lp"), "cwd": game, "game": game,
            "steam": os.path.join(self.lp, "steam"), "userdata": os.path.join(self.lp, "userdata"),
            "mods": os.path.join(self.lp, "mods"), "dlc_test": os.path.join(game, "dlc", "gametest"),
            "rsmods_ini": os.path.join(game, "RSMods.ini"),
            "rocksmith_ini": os.path.join(self.lp, "userdata", "game", "Rocksmith.ini"),
            "steam_env": os.path.join(game, ".rearmed", "steam.env"),
            "insert": [os.path.join(self.root, "tools", "libkeyfifo.dylib")],
            "env": {"LP_IMAGE": os.path.join(out, "lp_game.exe"), "LP_DLL_DIR": os.path.join(out, "dlls"),
                    "LP_GAME_DIR": game, "LP_USER_DIR": os.path.join(self.lp, "userdata"),
                    "RS_STEAM_DATA_DIR": os.path.join(self.lp, "steam"), "RS_MODS_PATH": os.path.join(self.lp,
                                                                                                      "mods")},
            "note": "as /Volumes/Storage/Rocksmith/build/seven-lp/play-up.sh starts it: from the game folder, with "
                    "the steam_env file's lines exported",
        }
        return info, launch

    # --- the profiles ----------------------------------------------------------------------------------

    @staticmethod
    def profiles(edition, steam, steam_env):
        """The test saves' account (steam.env's RS_STEAM_ID) and its profiles, read from LocalProfiles.json
        (decrypted in memory, nothing written)."""
        sid = None
        try:
            for line in open(steam_env):
                if line.startswith("RS_STEAM_ID="):
                    sid = int(line.split("=", 1)[1].strip())
        except (OSError, ValueError):
            pass
        accounts = sorted(d for d in os.listdir(steam) if d.isdigit()) if os.path.isdir(steam) else []
        account = str(sid - STEAMID64_BASE) if sid else (accounts[0] if len(accounts) == 1 else None)
        out = {"steam": steam, "account": account, "steam_id": sid, "accounts": accounts, "profiles": []}
        if not account:
            out["note"] = "no account: steam.env has no RS_STEAM_ID and the saves have %d accounts" % len(accounts)
            return out
        remote = os.path.join(steam, account, "221680", "remote")
        lp = os.path.join(remote, "LocalProfiles.json")
        if os.path.isfile(lp):
            raw = open(lp, "rb").read()
            dec = subprocess.run(["openssl", "enc", "-d", "-aes-256-ecb", "-nopad", "-K", PROFILE_KEY],
                                 input=raw[20:], capture_output=True).stdout
            try:
                j = json.loads(zlib.decompressobj().decompress(dec).rstrip(b"\0").decode("utf-8"))
            except (zlib.error, ValueError) as e:
                out["note"] = "LocalProfiles.json not read: %s" % e
                return out
            for p in j.get("Profiles", []):
                pid = p.get("UniqueID")
                out["profiles"].append({"name": p.get("PlayerName"), "id": pid,
                                        "file": "%s_PRFLDB" % pid if pid else None,
                                        "file_there": bool(pid) and os.path.isfile(os.path.join(remote, "%s_PRFLDB" % pid))})
        names = [p["name"] for p in out["profiles"] if p["file_there"]]
        if len(names) >= 2:
            out["coop"] = {"player1": names[0], "player2": names[1]}
            out["note"] = ("two profiles or more: co-op uses player1 and player2 (the order of LocalProfiles.json; "
                           "the profile list may sort them otherwise)")
        else:
            out["coop"] = None
            out["note"] = ("fewer than two profiles: the runner must make the second one through the game's menus "
                           "(Multiplayer asks for player 2's profile; New profile there), then save by the menus")
        return out

    # --- all -------------------------------------------------------------------------------------------

    def run(self):
        t0 = time.time()
        for p in (self.root, self.games):
            os.makedirs(p, exist_ok=True)
        say("game tests' installs: root %s, games %s" % (self.root, self.games))
        owner = load_json(os.path.join(self.root, ".game-lock", "owner.json")) or {}
        if owner.get("pid"):  # run.py's lock: its game reads these files
            try:
                os.kill(int(owner["pid"]), 0)
                raise Fail("%s (pid %s) holds %s: wait for its run to end" % (
                    owner.get("by", "a run"), owner["pid"], os.path.join(self.root, ".game-lock")))
            except (ProcessLookupError, ValueError):
                pass
            except PermissionError:
                raise Fail("pid %s holds %s" % (owner["pid"], os.path.join(self.root, ".game-lock")))
        for p in (self.root, self.games):
            if free_bytes(p) < MIN_FREE:
                raise Fail("%s has under 3 GB free" % p)
        free0 = {p: free_bytes(p) for p in (self.root, self.games)}
        builds = {"format": 1, "made": time.strftime("%Y-%m-%d %H:%M:%S"), "builds": self.a.builds,
                  "root": self.root, "games": self.games, "parts": dict(self.builds.get("parts") or {}),
                  "editions": dict(self.builds.get("editions") or {})}
        builds["tools"] = {"keyfifo": self.keyfifo()}
        profiles = load_json(os.path.join(self.root, "profiles.json")) or {}
        editions = ["mac", "lp"] if self.a.edition == "both" else [self.a.edition]
        try:
            for e in editions:
                info, launch = self.mac() if e == "mac" else self.lp_run()
                builds["parts"][e] = info
                builds["parts"][e]["builds"] = self.a.builds
                builds["editions"][e] = launch
                profiles[e] = self.profiles(e, launch["steam"], launch["steam_env"])
        finally:
            write_json(self.state_path, self.state)
            self.hash.save()
        write_json(os.path.join(self.root, "builds.json"), builds)
        write_json(os.path.join(self.root, "profiles.json"), profiles)
        for line in self.done:
            say("  " + line)
        if not self.done:
            say("  nothing changed")
        for e in editions:
            pr = profiles[e]
            say("  %s: account %s, profiles %s; co-op %s" % (
                e, pr.get("account"), ", ".join(p["name"] for p in pr["profiles"]) or "none",
                "%s + %s" % (pr["coop"]["player1"], pr["coop"]["player2"]) if pr.get("coop") else "needs a second profile"))
            for k, v in builds["parts"][e].items():
                if isinstance(v, dict):
                    say("    %-22s %s%s" % (k, v.get("commit", "")[:10] if v.get("commit") else v.get("from", ""),
                                            "  NOTE: " + v["note"] if v.get("note") else ""))
        used = {p: (free0[p] - free_bytes(p)) / 1e6 for p in free0}
        say("done in %.1f s; disk used now: %s" % (time.time() - t0, ", ".join("%s %+.0f MB" % (p, u)
                                                                             for p, u in used.items())))
        say("builds: %s; profiles: %s" % (os.path.join(self.root, "builds.json"),
                                          os.path.join(self.root, "profiles.json")))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--edition", choices=("mac", "lp", "both"), default="both")
    ap.add_argument("--builds", choices=("main", "installed"), default="main")
    ap.add_argument("--root", default=None)
    ap.add_argument("--games-dir", default=None)
    ap.add_argument("--fresh-saves", action="store_true", help="copy the player's saves again")
    ap.add_argument("--refresh-game", action="store_true", help="run install.sh even when nothing changed")
    ap.add_argument("--warm-shaders", action="store_true", help="let install.sh compile the Metal shaders")
    a = ap.parse_args(argv)
    try:
        Setup(a).run()
    except Fail as e:
        say("setup: error: %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
