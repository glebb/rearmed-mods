"""The game tests' game side: the one-game-at-a-time lock, a game started and always ended, and the route engine
that drives the menus by the log.

The engine follows rs-rearmed's tools/perf/run.py (keyfifo, the mods' `menu:` and `song:` lines), made fast: a key
goes as soon as its screen is up and settled, a key the game did not get (no `key down` line) is sent again after
`front`, and a screen that does not come is a Stuck with where the route was.
"""
import errno
import json
import os
import shutil
import signal
import socket
import subprocess
import threading
import time

import gt_log as L

LOCK_DIR = os.path.expanduser("~/rs-gametest/.game-lock")
GAMES = ("Rocksmith2014", "rs2014_lp")

# keyfifo's key names (rs-rearmed tools/ws8/keyfifo/keyfifo.m:24-36) -> the macOS key code rearmed-mods logs for
# the key (`key down 0x..`, rsmods.c:386). Modifier keys go as flagsChanged on the Mac and are not logged there.
KEYCODES = {"return": 0x24, "esc": 0x35, "delete": 0x33, "fwddelete": 0x75, "tab": 0x30, "space": 0x31,
            "up": 0x7e, "down": 0x7d, "left": 0x7b, "right": 0x7c, "/": 0x2c, "-": 0x1b,
            "lpshift": 0x38, "lpctrl": 0x3b,
            "a": 0x00, "b": 0x0b, "c": 0x08, "d": 0x02, "e": 0x0e, "f": 0x03, "g": 0x05, "h": 0x04, "i": 0x22,
            "j": 0x26, "k": 0x28, "l": 0x25, "m": 0x2e, "n": 0x2d, "o": 0x1f, "p": 0x23, "q": 0x0c, "r": 0x0f,
            "s": 0x01, "t": 0x11, "u": 0x20, "v": 0x09, "w": 0x0d, "x": 0x07, "y": 0x10, "z": 0x06,
            "0": 0x1d, "1": 0x12, "2": 0x13, "3": 0x14, "4": 0x15, "5": 0x17, "6": 0x16, "7": 0x1a, "8": 0x1c,
            "9": 0x19}

# The editions' own keys (docs/mac/TESTING.md and docs/windows/TESTING.md, Unattended runs)
EDITION_KEYS = {"mac": {"search": "/", "ctrl": "ctrl", "shift": "shift"},
                "lp": {"search": "lpshift", "ctrl": "lpctrl", "shift": "lpshift"}}

# Screens (the mods' names, menu: lines)
TITLE, PROFILES, DIALOG, MAIN = "TitleScreen", "ProfileSelect", "SelectionListDialog", "MainMenu"
SONGS, SONG_PAGE, TUNER, HIGHWAY, PAUSE = ("LearnASong_Songs", "LearnASong_SongOptions", "LearnASong_PreSongTuner",
                                          "LearnASong_Game", "LearnASong_Pause")
RIFF = "LearnASong_RiffRepeater"
IN_SONG = (HIGHWAY, PAUSE, RIFF)


class Stuck(Exception):
    """A route that did not get where it should: `where` is the screen it was on."""

    def __init__(self, what, where=None):
        Exception.__init__(self, what)
        self.what, self.where = what, where


class GameEnded(Stuck):
    pass


class Interrupted(BaseException):
    """A signal: the run stops, the game is quit, the lock removed."""


# ---------------------------------------------------------------------------------------------------------------
# The lock: one game on this Mac at a time (other agents run games too)

def game_running():
    out = subprocess.run(["pgrep", "-x", "|".join(GAMES)], capture_output=True, text=True).stdout.split()
    return [int(p) for p in out if p.strip().isdigit()]


class Lock:
    def __init__(self, path=LOCK_DIR, say=print):
        self.path, self.held, self.say = path, False, say

    def acquire(self, wait=1800, every=30):
        end = time.time() + wait
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        while True:
            try:
                os.mkdir(self.path)
            except FileExistsError:
                if self._stale():
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue
                if time.time() > end:
                    raise Stuck("the game lock %s was held for %d s: another game run" % (self.path, wait))
                self.say("  the game lock is held (%s); waiting" % self._owner())
                time.sleep(every)
                continue
            self.held = True
            with open(os.path.join(self.path, "owner.json"), "w") as f:
                json.dump({"pid": os.getpid(), "by": "rearmed-mods tests/game/run.py", "since": time.time()}, f)
            running = game_running()
            if running:  # a game outside the lock (the player's, say): wait for it as for the lock
                self.release()
                if time.time() > end:
                    raise Stuck("a game is running (pid %s) outside the lock" % running)
                self.say("  a game is running (pid %s); waiting" % running)
                time.sleep(every)
                continue
            return

    def wait_free(self, until, every=30):
        """Until no one holds the lock (a lock of a run that is gone counts as free), at most until `until`."""
        while os.path.isdir(self.path) and not self._stale():
            if time.time() > until:
                raise Stuck("the game lock %s is still held" % self.path)
            self.say("  the game lock is held (%s); waiting" % self._owner())
            time.sleep(every)

    def _owner(self):
        try:
            with open(os.path.join(self.path, "owner.json")) as f:
                return json.load(f).get("by", "?")
        except (OSError, ValueError):
            return "no owner file"

    def _stale(self):
        """A lock of ours whose process is gone."""
        try:
            with open(os.path.join(self.path, "owner.json")) as f:
                o = json.load(f)
        except (OSError, ValueError):
            return False
        if "run.py" not in o.get("by", ""):
            return False
        try:
            os.kill(int(o["pid"]), 0)
            return False
        except (OSError, ValueError, KeyError):
            return True

    def release(self):
        if self.held:
            shutil.rmtree(self.path, ignore_errors=True)
            self.held = False


# ---------------------------------------------------------------------------------------------------------------
# The companion's events: the runner binds its socket, as the companion app does (companion tools/event_listener.py);
# without a listener the companion pauses its memory scans (companion native/rscore.c:231 note_listener).

class EventSink(threading.Thread):
    def __init__(self, sock_path, out_path):
        threading.Thread.__init__(self, daemon=True)
        self.sock_path, self.out_path, self.stop_ = sock_path, out_path, False
        if len(sock_path) >= 104:
            raise Stuck("the companion's socket path is too long for a Unix socket: %s" % sock_path)
        if os.path.exists(sock_path):
            os.unlink(sock_path)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 18)
        self.s.bind(sock_path)
        self.s.settimeout(0.5)

    def run(self):
        with open(self.out_path, "a") as out:
            while not self.stop_:
                try:
                    data = self.s.recv(65536)
                except socket.timeout:
                    continue
                except OSError:
                    break
                try:
                    ev = json.loads(data.decode(errors="replace"))
                except ValueError:
                    continue
                ev["received"] = round(time.time(), 3)
                out.write(json.dumps(ev) + "\n")
                out.flush()

    def close(self):
        self.stop_ = True
        try:
            self.s.close()
        finally:
            try:
                os.unlink(self.sock_path)
            except OSError:
                pass


# ---------------------------------------------------------------------------------------------------------------
# A game

class Game:
    """spec: {exe, args, cwd, env (complete), fifo, log, snap_dir, netguard_log, edition}."""

    def __init__(self, spec, say=print):
        self.spec, self.say = spec, say
        self.p = None
        self.t_start = None
        self.log = L.Log(spec["log"])
        self.exit_code = None
        self.quit_how = None

    def start(self):
        s = self.spec
        if not os.path.exists(s["fifo"]):
            os.mkfifo(s["fifo"])
        with open(s["log"], "ab") as log:
            self.t_start = time.time()
            # a session of its own: a Ctrl-C in the runner's terminal reaches the runner, which quits the game
            self.p = subprocess.Popen([s["exe"]] + s.get("args", []), cwd=s.get("cwd") or None, env=s["env"],
                                      stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                      start_new_session=True)

    def alive(self):
        if self.p is None:
            return False
        if self.p.poll() is None:
            return True
        self.exit_code = self.p.returncode
        return False

    def send(self, *cmds):
        if not self.alive():
            raise GameEnded("the game ended (exit %s)" % self.exit_code)
        try:
            fd = os.open(self.spec["fifo"], os.O_WRONLY | os.O_NONBLOCK)
            with os.fdopen(fd, "w") as f:
                f.write("".join(c + "\n" for c in cmds))
        except OSError as err:
            if err.errno == errno.ENXIO:
                raise Stuck("keyfifo is not reading: no `keyfifo:` line? (DYLD_INSERT_LIBRARIES lost?)")
            raise Stuck("the keys can't be sent (%s)" % err)

    def quit(self, wait_menu=15):
        """keyfifo's quit (the app menu's Command-Q), then SIGTERM, then SIGKILL. -> how it ended."""
        if self.p is None:
            return self.quit_how
        if not self.alive():
            self.quit_how = self.quit_how or "ended by itself (exit %s)" % self.exit_code
            return self.quit_how
        how = None
        try:
            self.send("quit")
            self.p.wait(wait_menu)
            how = "quit (Command-Q)"
        except (Stuck, subprocess.TimeoutExpired):
            try:
                os.killpg(self.p.pid, signal.SIGTERM)
            except OSError:
                self.p.send_signal(signal.SIGTERM)
            try:
                self.p.wait(10)
                how = "SIGTERM"
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.p.pid, signal.SIGKILL)
                except OSError:
                    self.p.kill()
                self.p.wait(10)
                how = "SIGKILL"
        self.exit_code = self.p.returncode
        self.quit_how = self.quit_how or how
        return self.quit_how


# ---------------------------------------------------------------------------------------------------------------
# Snapshots: the renderer writes one every RS_GL_SNAPSHOT_SECS (rs-rearmed shims/opengl/src/metal/mtl_context.m:684,
# metal_NNNNNN.png); a route keeps the next one after a moment it names and the rest are removed as they age.

def png_complete(path):
    """A PNG written whole: it ends with its IEND chunk (the renderer writes it on a queue after the frame)."""
    try:
        with open(path, "rb") as f:
            f.seek(-12, os.SEEK_END)
            return f.read(12)[4:8] == b"IEND"
    except OSError:
        return False


class Snaps:
    def __init__(self, folder, every):
        self.folder, self.every = folder, every
        self.kept = set()
        self._pruned = 0.0

    def files(self):
        try:
            return sorted(f for f in os.listdir(self.folder) if f.endswith(".png"))
        except OSError:
            return []

    def take(self, dest, label, poll, timeout=None):
        """The first snapshot written after now -> its copy dest/label.png, or None. poll(): the engine's step."""
        t0 = time.time()
        end = t0 + (timeout or self.every * 2 + 3)
        while time.time() < end:
            for f in reversed(self.files()):
                p = os.path.join(self.folder, f)
                try:
                    st = os.stat(p)
                except OSError:
                    continue
                if st.st_mtime >= t0 and st.st_size > 0 and png_complete(p):
                    os.makedirs(dest, exist_ok=True)
                    out = os.path.join(dest, label + ".png")
                    shutil.copyfile(p, out)
                    return out
                if st.st_mtime < t0:
                    break
            poll(0.25)
        return None

    def prune(self, keep_s=30):
        if time.time() - self._pruned < 5:
            return
        self._pruned = time.time()
        now = time.time()
        for f in self.files():
            p = os.path.join(self.folder, f)
            try:
                if now - os.stat(p).st_mtime > keep_s:
                    os.remove(p)
            except OSError:
                pass


# ---------------------------------------------------------------------------------------------------------------
# The route engine

class Driver:
    """Drives one game by its log. Everything waits by polling the log (0.1 s); nothing sleeps a fixed time but
    the gaps between keys."""

    def __init__(self, game, edition, snaps=None, say=print, key_gap=0.35, on_poll=None, ack_s=6.0):
        self.g, self.edition, self.snaps, self.say = game, edition, snaps, say
        self.key_gap, self.ack_s = key_gap, ack_s
        self.on_poll = on_poll
        self.screen, self.screen_at, self.screen_index = None, time.time(), -1
        self.history = []  # (t, screen)
        self.song, self.song_at = None, 0.0
        self._scanned = 0
        self.keys = EDITION_KEYS[edition]
        self.trace = []  # what the route did: (t, text)

    # -- the log
    @property
    def lines(self):
        return self.g.log.lines

    def poll(self, sleep=0.1):
        if sleep:
            time.sleep(sleep)
        self.g.log.poll()
        lines = self.g.log.lines
        for i in range(self._scanned, len(lines)):
            t, text = lines[i]
            m = L.MENU.match(text)
            if m:
                self.screen, self.screen_at, self.screen_index = m.group(1), t, i
                self.history.append((t, self.screen))
                continue
            m = L.SONG.match(text)
            if m:
                self.song, self.song_at = m.group(1), t
        self._scanned = len(lines)
        if self.snaps:
            self.snaps.prune()
        if self.on_poll:
            self.on_poll()
        if not self.g.alive():
            raise GameEnded("the game ended (exit %s) on %s" % (self.g.exit_code, self.screen), self.screen)

    def note(self, text):
        self.trace.append((time.time(), text))
        self.say("    %6.1f s  %s" % (time.time() - self.g.t_start, text))

    def mark(self):
        return len(self.lines)

    def since(self, mark):
        return [t for _, t in self.lines[mark:]]

    def find(self, regex, mark=0):
        """The first line from mark that matches -> (index, match) or None."""
        import re
        rx = re.compile(regex) if isinstance(regex, str) else regex
        for i in range(mark, len(self.lines)):
            m = rx.search(self.lines[i][1])
            if m:
                return i, m
        return None

    def wait_for(self, regex, timeout, mark=None, what=None):
        mark = self.mark() if mark is None else mark
        end = time.time() + timeout
        while True:
            hit = self.find(regex, mark)
            if hit:
                return hit
            if time.time() > end:
                raise Stuck("%s: not in %d s" % (what or regex, timeout), self.screen)
            self.poll()

    def wait_screen(self, names, timeout, what=None, since=None):
        """Until the screen is one of names (and came after `since`, a time) -> the screen."""
        names = (names,) if isinstance(names, str) else tuple(names)
        end = time.time() + timeout
        while True:
            if self.screen in names and (since is None or self.screen_at >= since):
                return self.screen
            if time.time() > end:
                raise Stuck("%s: %s did not come in %d s (on %s)" % (what or "wait", "/".join(names), timeout,
                                                                    self.screen), self.screen)
            self.poll()

    def hold(self, seconds, until=None):
        """Up to `seconds`, or until until() is true. -> True if until() came."""
        end = time.time() + seconds
        while time.time() < end:
            if until and until():
                return True
            self.poll(0.2)
        return bool(until and until())

    # -- keys
    def front(self):
        self.g.send("front")

    def key(self, name, ack=True, hold_ms=None):
        """One key, in front. ack: wait for the game's `key down` line, and send it again after `front` once if it
        did not come (the game takes keys only while it has the focus)."""
        name = self.keys.get(name, name)
        code = KEYCODES.get(name)
        for attempt in range(3):
            mark = self.mark()
            self.g.send("front", "key %s%s" % (name, " %d" % hold_ms if hold_ms else ""))
            if not ack or code is None or (self.edition == "mac" and name in ("shift", "ctrl", "alt", "cmd")):
                self.poll(self.key_gap)
                return
            # a busy game (a profile saved, a song loading) takes a queued key seconds late: wait well before
            # sending it again, or it arrives twice
            try:
                self.wait_for(r"^\[rsmods\] key down 0x%x\b" % code, self.ack_s, mark=mark)
                self.poll(self.key_gap)
                return
            except Stuck:
                self.note("no `key down` for %s in %.0f s: front, and again" % (name, self.ack_s))
        raise Stuck("the game takes no keys (%s): is its window in front? (%s)" % (
            name, "lp: the window is hidden" if self.find(L.LP_HIDDEN) else "no key down line"), self.screen)

    def keys_(self, *names, gap=None):
        for n in names:
            self.key(n)
            if gap:
                self.poll(gap)

    def text(self, s, gap=1.0):
        for c in s.lower():
            self.key("space" if c == " " else c)
            self.poll(max(0.0, gap - self.key_gap))

    def press(self, key, to, settle=1.0, retry=4.0, tries=4, timeout=None, progress=None, started=None,
              started_wait=60.0, stray=False):
        """`key` on the current screen until the screen is one of `to` (a name or names), or `to` is a function that
        says so. settle: seconds the screen must have been up first (a menu that moves in drops keys). retry:
        seconds without a change before the key is sent again; a line matching `progress` (a regex) meanwhile
        resets that wait (a load under way). started: a line that says the key was taken (a song loading): from
        then on no key again, only up to started_wait seconds for `to`. stray: another screen than `to` is a Stuck at
        once (a menu whose cursor was elsewhere). -> the screen reached."""
        if isinstance(to, str):
            to = (to,)
        start_screen, start_at = self.screen, self.screen_at
        if callable(to):
            done = to
        else:
            names = tuple(to)

            def done():
                moved = self.screen != start_screen or self.screen_at > start_at
                if stray and moved and self.screen not in names:  # another screen than the key's: no more keys
                    raise Stuck("%s on %s led to %s, not %s" % (key, start_screen, self.screen, "/".join(names)),
                                self.screen)
                return self.screen in names and moved
        end = time.time() + (timeout or retry * tries + settle + 10)
        sent = 0
        while True:
            if done():
                return self.screen
            wait = settle - (time.time() - self.screen_at)
            if wait > 0:
                self.poll(min(wait, 0.25))
                continue
            if sent >= tries or time.time() > end:
                raise Stuck("%s on %s: no %s after %d tries" % (
                    key, start_screen, "/".join(to) if not callable(to) else "change", sent), self.screen)
            mark0 = self.mark()
            self.key(key)
            sent += 1
            mark, t_sent = self.mark(), time.time()
            while time.time() - t_sent < retry:
                if done():
                    return self.screen
                if started and self.find(started, mark0):
                    self.note("%s taken (%s)" % (key, started if isinstance(started, str) else started.pattern))
                    t_end = time.time() + started_wait
                    while not done():
                        if time.time() > t_end:
                            raise Stuck("%s on %s was taken, but no %s in %d s" % (
                                key, start_screen, "/".join(to) if not callable(to) else "change", started_wait),
                                self.screen)
                        self.poll()
                    return self.screen
                if progress and self.find(progress, mark):
                    mark, t_sent = self.mark(), time.time()
                self.poll()
            if done():
                return self.screen
            self.note("no change after %s on %s: again" % (key, self.screen))

    def snap(self, dest, label):
        if not self.snaps:
            return None
        p = self.snaps.take(dest, label, self.poll)
        self.note("snapshot %s%s" % (label, "" if p else ": none came"))
        return p
