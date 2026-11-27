"""The game tests' log side: a log read as it grows, the checks over a stretch of it, and the patterns.

A log here is a list of (time, text) lines: the time is when the runner read the line (the game's lines carry no
time of their own), so a slice of a case is the lines between two times or two indexes. The companion's log has
its own HH:MM:SS.mmm times; its lines are kept as they are.

Every pattern cites the code that prints it (file:line in the repository named), so a changed message is found by
the test that fails and the comment that says where to look.
"""
import json
import os
import re
import time

# ---------------------------------------------------------------------------------------------------------------
# What the game, the mods and the runtime print. rearmed-mods = ~/src/rearmed-mods, rs-rearmed = ~/src/rs-rearmed,
# songbridge = ~/src/rearmed-songbridge, companion = ~/rs-mac-companion.

# rearmed-mods src/rsmods.c:327  rsm_log("menu: %s", m)  (on every change of the game's screen)
MENU = re.compile(r"^\[rsmods\] menu: (\S+)")
# rearmed-mods src/rsmods.c:336  rsm_log("song: %s", g_key)  (the highlighted song in a list)
SONG = re.compile(r"^\[rsmods\] song: (\S+)")
# rearmed-mods src/rsmods.c:386  rsm_log("key down 0x%x%s%s", ...)  (every key the game got, before the mods)
KEY_DOWN = re.compile(r"^\[rsmods\] key down 0x([0-9a-f]+)")
# rearmed-mods src/rsmods.c:473  rsm_log("rearmed-mods %s loaded on %s, %d mods", ...)
MODS_LOADED = re.compile(r"^\[rsmods\] rearmed-mods (\S+) loaded on (\S+), (\d+) mods")
# rearmed-mods src/rsmods.c:324
INI_RELOADED = re.compile(r"^\[rsmods\] RSMods\.ini \(re\)loaded")
# rs-rearmed tools/ws7/recomp/overrides/mods/mods_shared.c:544,547
DLC_MOUNTED = re.compile(r"^\[rs-mods\] DLC: (\d+) archives mounted in (\d+) frames, ([\d.]+) s")
# libnetguard's first line (in its own log on the Mac, in the game's log in Learn & Play)
NETGUARD_BLOCK = re.compile(r"netguard pid=\d+ mode=block")
# rearmed-mods src/mod_autoload.c (the profile is entered by the mod: the runner leaves the keys to it)
AUTOLOAD = re.compile(r"^\[rsmods\] auto load profile: Enter")
# rs-rearmed win-x86/app/lp_main.c (lp: the window is hidden: keyfifo can't give keys, docs/windows/TESTING.md)
LP_HIDDEN = re.compile(r"^lp: the window is hidden")

# What no case may log. rs-rearmed win-x86/app/lp_main.c:364-375 (on_fault: "lp: signal %d: ..." and
# "lp: stack (atos -l ...)"), rs-rearmed tools/ws7/recomp/runtime/rc32_hook.c:39,224 (a pointer outside guest
# memory: "the game gets a null pointer"), and any [rc] line that says fault or crash.
FAULTS = [
    ("lp fault", r"^lp: signal \d+"),
    ("lp stack", r"^lp: stack"),
    ("rc fault", r"^\[rc\] .*\b(fault|crash)"),
    ("rc null pointer", r"^\[rc\] .*the game gets a null pointer"),
]
# rearmed-mods src/rsmods.c:445 ("no hook point %s on %s: its mod stays off"), mod_camera.c:152, mod_loft.c:230,
# mod_extended_range.c:205, mod_song_stems.c:267: a mod that could not start on this game.
STAYS_OFF = r"^\[rsmods\] .*stays off"


def strip_companion_time(text):
    """The companion's lines start with HH:MM:SS.mmm (companion native/rscore.c:65)."""
    return re.sub(r"^\d\d:\d\d:\d\d\.\d{3} ", "", text)


class Log:
    """A file read as it grows. lines: [(t, text)]."""

    def __init__(self, path, strip=None):
        self.path, self.strip = path, strip
        self.lines = []
        self._pos, self._rest = 0, b""

    def poll(self):
        """Reads what was added; -> the number of new lines."""
        try:
            with open(self.path, "rb") as f:
                f.seek(self._pos)
                data = f.read()
        except OSError:
            return 0
        if not data:
            return 0
        self._pos += len(data)
        data = self._rest + data
        parts = data.split(b"\n")
        self._rest = parts.pop()
        now = time.time()
        for p in parts:
            text = p.decode("utf-8", "replace").rstrip("\r")
            if self.strip:
                text = self.strip(text)
            self.lines.append((now, text))
        return len(parts)

    def text(self, start=0, end=None):
        return [t for _, t in self.lines[start:end]]


def lines_of(path_or_text):
    """A recorded log as lines: a file's path or the text."""
    if "\n" not in path_or_text and os.path.exists(path_or_text):
        with open(path_or_text, errors="replace") as f:
            path_or_text = f.read()
    return path_or_text.splitlines()


# ---------------------------------------------------------------------------------------------------------------
# Checks

PASS, FAIL, SKIP, PENDING = "PASS", "FAIL", "SKIP", "PENDING"


class Check:
    """One check over a case's lines.

    expect (forbid=False): at least `min` (and at most `max`) lines of `source` match `pattern`; with `groups`
    ({group: value or a function of the text}) only the matches whose groups agree count. forbid: no line matches.
    `needs`: what the check needs that a run may not have ("fake_input", "companion", ...): without it, SKIP.
    `src`: where the line is printed (file:line), for the report.
    """

    def __init__(self, name, pattern, source="game", min=1, max=None, groups=None, forbid=False, needs=(),
                 src="", note="", scope="case", optional=None):
        self.name, self.source, self.min, self.max, self.scope = name, source, min, max, scope
        self.optional = optional  # a reason the line may not come in a run: missing, SKIP with it, not FAIL
        self.pattern = re.compile(pattern)
        self.groups, self.forbid, self.needs, self.src, self.note = groups or {}, forbid, tuple(needs), src, note

    def at(self, scope):
        """scope "case" (the case's lines) or "session" (the game's log from its start)."""
        self.scope = scope
        return self

    def _ok_groups(self, m):
        for g, want in self.groups.items():
            got = m.group(g)
            if callable(want):
                try:
                    if not want(got):
                        return False
                except (TypeError, ValueError):
                    return False
            elif got != str(want):
                return False
        return True

    def matches(self, lines):
        """-> [(index, text)] of the lines that count."""
        out = []
        for i, t in enumerate(lines):
            m = self.pattern.search(t)
            if m and self._ok_groups(m):
                out.append((i, t))
        return out

    def evaluate(self, sources, missing=(), final=True):
        """sources: {source: [text]}. missing: what this run lacks. -> a result dict. Before the end (final=False)
        an expect check that has not got its lines yet is PENDING, a forbid check PASS until a line comes."""
        lacking = [n for n in self.needs if n in missing]
        r = {"name": self.name, "pattern": self.pattern.pattern, "source": self.source, "src": self.src,
             "scope": self.scope}
        if lacking:
            r.update(status=SKIP, why="needs " + ", ".join(lacking))
            return r
        lines = sources.get(self.source)
        if lines is None:
            r.update(status=SKIP, why="no %s log in this run" % self.source)
            return r
        hits = self.matches(lines)
        r["count"] = len(hits)
        r["lines"] = [i for i, _ in hits[:20]]
        if self.forbid:
            r["status"] = FAIL if hits else PASS
            if hits:
                r["why"] = "%d line(s) that must not be there" % len(hits)
            return r
        if self.max is not None and len(hits) > self.max:
            r.update(status=FAIL, why="%d lines, at most %d wanted" % (len(hits), self.max))
        elif len(hits) >= self.min:
            r["status"] = PASS
        elif not final:
            r["status"] = PENDING
        elif self.optional:
            r.update(status=SKIP, why="not seen: " + self.optional)
        else:
            r.update(status=FAIL, why="%d line(s), %d wanted" % (len(hits), self.min) if hits or self.min > 1
                     else "no line")
        return r


def evaluate(checks, sources, missing=(), final=True):
    return [c.evaluate(sources, missing, final) for c in checks]


def summary(results):
    """-> PASS, FAIL, SKIP (all skipped) or PENDING."""
    st = [r["status"] for r in results]
    if FAIL in st:
        return FAIL
    if PENDING in st:
        return PENDING
    if st and all(s == SKIP for s in st):
        return SKIP
    return PASS


def context(lines, index, before=5, after=3):
    lo = max(0, index - before)
    return [(i, lines[i]) for i in range(lo, min(len(lines), index + after + 1))]


# ---------------------------------------------------------------------------------------------------------------
# The route's view of a log: screens and songs as they came

def screens(lines):
    """-> [(index, screen)] of the menu: lines."""
    out = []
    for i, t in enumerate(lines):
        m = MENU.match(t)
        if m:
            out.append((i, m.group(1)))
    return out


def songs(lines):
    out = []
    for i, t in enumerate(lines):
        m = SONG.match(t)
        if m:
            out.append((i, m.group(1)))
    return out


def read_events(path, start_offset=0):
    """The companion's datagrams as the runner wrote them (a JSON line each) -> ([event], end offset)."""
    out = []
    try:
        with open(path, "rb") as f:
            f.seek(start_offset)
            data = f.read()
    except OSError:
        return out, start_offset
    for line in data.splitlines():
        try:
            out.append(json.loads(line))
        except ValueError:
            pass
    return out, start_offset + len(data)
