#!/usr/bin/env python3
"""The game tests' charts and sounds, both platforms' forms, into <root>/fixtures (tests/game/DESIGN.md).

    tests/game/fixtures.py [--root DIR] [--check] [CHART.psarc ...]

<root> is --root, else $RS_GAMETEST_ROOT, else ~/rs-gametest. What it makes:

    fixtures/pc/*.psarc       each chart as Learn & Play reads it (_p)
    fixtures/mac/*.psarc      the same as the Mac reads it (_m): made with rocksmithconvert's converter (app ID
                              kept), then checked: the same entries, charts and audio byte for byte (encrypted
                              SNGs: the same chart once decrypted), only the folder names and the aggregate
                              graph's platform tag changed
    fixtures/audio/*.wav      plucked tones (48 kHz, mono, 16-bit) at MIDI 40, 35, 30, 28, 23, 21 and an
                              E standard open strum, for the fake input (RS_AUDIO_FAKE_INPUT=FILE)
    fixtures/fixtures.json    per chart: the files, song key, title, artist, the search text that finds it, its
                              arrangements (name, instrument, strings, the manifest's Tuning, the open strings'
                              MIDI notes, the tuning's name as the game shows it, and whether that name is sure),
                              what it is for; the stock songs of the normal-flow cases, per edition; the sounds

The charts: the three RS Extended Range native packages in ~/Downloads (Nickelback: 7-string Lead, 6-string
Rhythm, 5-string Bass; Fear Factory: 8-string Lead; Jinjer: 5-string Bass with its low B down a tone), a stock
custom song for Song Bridge's case (Nirvana, Territorial Pissings, from the Mac CDLC backup: no custom song there
has both forms, so its PC form is converted here), a made-up stock song with seven arrangements for the "more than
five arrangements" case (GameTestAlternates, made here from Nickelback's Rhythm and the custom song's Bass), and
any more given as arguments or in $RS_GAMETEST_CHARTS (os.pathsep-separated; either form, the other is made).

Idempotent: a chart whose source and outputs are as fixtures.json recorded is left alone, so a second run takes a
second or two. --check changes nothing: it checks every file against fixtures.json and the two forms of each chart
against each other, and exits 1 if anything is off.

Runs under `python3 -I`; it needs rocksmithconvert (~/src/rocksmithconvert, or $RS_CONVERT_DIR) and starts itself
again under that repository's .venv-arm64 Python when the system Python lacks its packages.
"""

import argparse
import copy
import hashlib
import io
import json
import math
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import uuid
import wave
import zlib

CONVERT_DIR = os.path.expanduser(os.environ.get("RS_CONVERT_DIR") or "~/src/rocksmithconvert")
VENV_PY = os.path.join(CONVERT_DIR, ".venv-arm64", "bin", "python")
sys.path.insert(0, os.path.join(CONVERT_DIR, "src"))
try:
    from rocksmith.crypto import MAC_KEY, WIN_KEY, decrypt_sng, encrypt_sng
    from rocksmith.psarc import HEADER, PSARC, read_entry
    from rocksmithconvert import plain_psarc
    from rocksmithconvert.models import ProcessModel
    from rocksmithconvert.services import _Converter
except ImportError:
    if os.path.realpath(sys.executable) != os.path.realpath(VENV_PY) and os.path.exists(VENV_PY):
        os.execv(VENV_PY, [VENV_PY, "-I", os.path.abspath(__file__)] + sys.argv[1:])
    sys.exit("fixtures.py: rocksmithconvert's packages are missing (%s)" % VENV_PY)

FORMAT = 1
HOME = os.path.expanduser("~")
DOWNLOADS = os.path.join(HOME, "Downloads")
PLAYER = os.path.join(HOME, "Library", "Application Support", "RS Re-ARMed")
PLAYER_MAC_GAME = os.path.join(PLAYER, "game")
PLAYER_LP_GAME = os.path.join(PLAYER, "learn-play", "game")
CDLC_BACKUP = "/Volumes/Storage/Rocksmith/songs/backup-dlc"

NATIVE = [  # (id, file in ~/Downloads, what it is for)
    ("nickelback", "Nickelback_Burn-It-To-The-Ground_Dell_v2_3_rsxr_p.psarc",
     ["guitar7", "bass5", "line_off", "songbridge", "mods", "coop", "hearing", "smoke"]),
    ("fearfactory", "Fear-Factory_Soul-Hacker_Dell_v0_2_rsxr_p.psarc", ["guitar8", "songbridge", "hearing"]),
    ("jinjer", "Jinjer_Wallflower_Dell_v2_2_rsxr_p.psarc", ["bass5", "hearing"]),
]
STOCK_CDLC = ("stock_cdlc", os.path.join(CDLC_BACKUP, "cdlc", "Nirvana-TerritorialPissings_m.psarc"), ["songbridge"])
# The tuning names the game shows that are known from runs (the user's), by chart and arrangement file suffix
KNOWN_NAMES = {("nickelback", "lead"): "B STANDARD 7STR", ("fearfactory", "lead"): "F♯ STANDARD 8STR",
               ("nickelback", "bass"): "B STANDARD 5STR"}

# The stock songs of the normal-flow cases (song, bass, pause_menu, ...): disc songs of each edition's songs.psarc.
# Learn & Play's songs.psarc has only Brannan Street Blues (and the intro); the Mac's has the disc songs.
STOCK_SONGS = {"mac": "SixteenSaltines", "lp": "NoteBlue"}

ALT_KEY = "GameTestAlternates"
ALT_TITLE = "Game Test Alternates"
ALT_ARTIST = "RS Re-ARMed Game Test"
ALT_NS = uuid.UUID("5b7c3f0e-6a2d-4f1e-9a51-0c1d2e3f4a5b")  # the made-up song's ids: uuid5(this, ...), stable
# (file suffix, ArrangementName, represent, bonusArr, source): the seven arrangements of the made-up song
ALT_ARRANGEMENTS = [
    ("lead", "Lead", 1, 0, "rhythm"), ("lead2", "Lead", 0, 0, "rhythm"), ("lead3", "Lead", 0, 1, "rhythm"),
    ("rhythm", "Rhythm", 1, 0, "rhythm"), ("rhythm2", "Rhythm", 0, 0, "rhythm"), ("rhythm3", "Rhythm", 0, 1, "rhythm"),
    ("bass", "Bass", 1, 0, "bass"),
]
ROUTE = {"Lead": (0, 1), "Rhythm": (1, 2), "Bass": (3, 4)}  # ArrangementType, routeMask

RATE = 48000
SOUNDS = [  # (id, file, MIDI notes, seconds): plucked strings, one pluck a file (the fake input loops it)
    ("E2", "pluck_40_E2.wav", [40], 4.0), ("B1", "pluck_35_B1.wav", [35], 4.0),
    ("Fs1", "pluck_30_Fs1.wav", [30], 4.0), ("E1", "pluck_28_E1.wav", [28], 4.0),
    ("B0", "pluck_23_B0.wav", [23], 4.0), ("A0", "pluck_21_A0.wav", [21], 4.0),
    ("strum_e_standard", "strum_e_standard.wav", [40, 45, 50, 55, 59, 64], 4.0),
]
SOUND_VERSION = 1

LAYOUT = {"pc": ("generic", "windows", "dx9", "_p.psarc"), "mac": ("macos", "mac", "macos", "_m.psarc")}
SNG_KEY = {"pc": WIN_KEY, "mac": MAC_KEY}


class Fail(Exception):
    pass


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()


def say(*a):
    print(*a, flush=True)


# --- packages --------------------------------------------------------------------------------------------

def form_of(name):
    low = name.lower()
    if low.endswith("_p.psarc"):
        return "pc"
    if low.endswith("_m.psarc"):
        return "mac"
    raise Fail("%s: not a _p or _m .psarc" % name)


def other_name(name):
    form = form_of(name)
    return name[:-8] + ("_m.psarc" if form == "pc" else "_p.psarc")


def read_package(path):
    """(entries as stored, magic or None for a stock archive)."""
    with open(path, "rb") as f:
        head = f.read(32)
    if plain_psarc.plain_magic(head):
        with open(path, "rb") as f:
            return plain_psarc.read(f)
    with open(path, "rb") as f:
        return PSARC(False).parse_stream(f), None


def sng_body(name, data):
    """The chart of an SNG as stored: a plain one's body, an encrypted one decrypted with its folder's key."""
    if not data:
        return data
    if plain_psarc.sng_is_plain(data):
        return zlib.decompress(data[28:-56]) if len(data) > 84 else b""
    key = MAC_KEY if "/macos/" in name else WIN_KEY
    return decrypt_sng(data, key)


class _Signals:
    class _R:
        def __init__(self):
            self.calls = []

        def emit(self, v):
            self.calls.append(v)

    def __init__(self):
        self.info, self.update = self._R(), self._R()


def convert(src, out_dir):
    """The other platform's form of SRC into OUT_DIR (rocksmithconvert, app ID kept): its path."""
    target = "MAC" if form_of(src) == "pc" else "PC"
    os.makedirs(out_dir, exist_ok=True)
    result = _Converter(_Signals()).process(src, ProcessModel([src], out_dir, target, "Disabled", "Disabled", True))
    if not result:
        raise Fail("%s: rocksmithconvert made nothing" % src)
    return result


def mapped(name, to_pc):
    return _Converter(_Signals())._convert(name, to_pc)


def compare_forms(pc_path, mac_path):
    """[] when the two forms hold the same song, else what differs: the same entries (folders renamed), the same
    charts (plain SNGs byte for byte, encrypted ones once decrypted), audio and everything else byte for byte, the
    aggregate graph with only its folders and platform tag changed."""
    pc, pc_magic = read_package(pc_path)
    mac, mac_magic = read_package(mac_path)
    bad = []
    if pc_magic != mac_magic:
        bad.append("magic %r vs %r" % (pc_magic, mac_magic))
    want = {mapped(n, False): n for n in pc}
    if set(want) != set(mac):
        bad.append("entries differ: %s" % sorted(set(want) ^ set(mac))[:6])
    for mname, pname in want.items():
        if mname not in mac:
            continue
        a, b = pc[pname], mac[mname]
        if pname.endswith(".sng"):
            if plain_psarc.sng_is_plain(a) != plain_psarc.sng_is_plain(b):
                bad.append("%s: plain in one form only" % pname)
            elif plain_psarc.sng_is_plain(a) and a != b:
                bad.append("%s: plain SNG not byte for byte" % pname)
            elif sng_body(pname, a) != sng_body(mname, b):
                bad.append("%s: the chart differs" % pname)
        elif pname.endswith("aggregategraph.nt"):
            if mapped(a.decode("utf-8"), False).replace("dx9", "macos").encode("utf-8") != b:
                bad.append("%s: the graph differs beyond its folders and tag" % pname)
        elif a != b:
            bad.append("%s: differs" % pname)
    return bad


def manifests(content):
    """[(entry name, attributes)] of the package's arrangement manifests (.json), in archive order."""
    out = []
    for name, data in content.items():
        if name.startswith("manifests/") and name.endswith(".json"):
            j = json.loads(data.decode("utf-8-sig"))
            for _, v in j.get("Entries", {}).items():
                out.append((name, v.get("Attributes", {})))
    return out


# --- tuning names ----------------------------------------------------------------------------------------
# A port of rearmed-mods' src/seven_tuning_names.h (the 7/8-string guitar's and 5/6-string bass's names) and the
# stock names the game has for the usual 6-string tunings (the latter not read from the game: unsure).

FAM7 = [(1, "C Standard", "C Drop Bb"), (0, "B Standard", "Drop A"), (-1, "Bb Standard", "Bb Drop Ab"),
        (-2, "A Standard", "A Drop G"), (-3, "Ab Standard", "Ab Drop Gb"), (-4, "G Standard", "G Drop F"),
        (-5, "Gb Standard", "Gb Drop E"), (-6, "F Standard", "F Drop Eb"), (-7, "E Standard", "E Drop D"),
        (-8, "Eb Standard", "Eb Drop Db"), (-9, "D Standard", "D Drop C")]
FAM8 = [(1, "G Standard", "G Drop F"), (0, "F# Standard", "Drop E"), (-1, "F Standard", "F Drop Eb"),
        (-2, "E Standard", "E Drop D"), (-3, "Eb Standard", "Eb Drop Db"), (-4, "D Standard", "D Drop C"),
        (-5, "Db Standard", "Db Drop B"), (-6, "C Standard", "C Drop Bb"), (-7, "B Standard", "B Drop A"),
        (-8, "Bb Standard", "Bb Drop Ab"), (-9, "A Standard", "A Drop G")]
ALT7 = {(-2, 0, 0, 0, 0, 0, -2): "Extended Drop D", (-4, -2, -2, -2, -2, -2, -4): "Extended Drop C",
        (-5, -3, -3, -3, -3, -3, -5): "Extended Drop B", (5, 5, 5, 4, 3, -2, 3): "Double Drop D",
        (-2, -2, -3, -5, -6, -9, -4): "Open G7"}
NOTES = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
STOCK6 = {(0, 0, 0, 0, 0, 0): "E Standard", (-2, 0, 0, 0, 0, 0): "Drop D", (-1,) * 6: "Eb Standard",
          (-3, -1, -1, -1, -1, -1): "Eb Drop Db", (-2,) * 6: "D Standard", (-4, -2, -2, -2, -2, -2): "D Drop C",
          (-3,) * 6: "C# Standard", (-5, -3, -3, -3, -3, -3): "C# Drop B", (-4,) * 6: "C Standard",
          (-6, -4, -4, -4, -4, -4): "C Drop A#", (-5,) * 6: "B Standard", (-7, -5, -5, -5, -5, -5): "B Drop A",
          (-2, -2, 0, 0, 0, -2): "Open G", (-2, 0, 0, -1, -2, -2): "Open D", (0, 2, 2, 1, 0, 0): "Open E",
          (0, 0, 2, 2, 2, 0): "Open A", (-2, 0, 0, 0, -2, -2): "DADGAD"}


def _family(table, t):
    fams, low_pc = (FAM8, 6) if table else (FAM7, 11)
    if any(x != t[0] for x in t[1:-1]):
        return None
    for k, std, drop in fams:
        if t[0] != k:
            continue
        d = t[0] - t[-1]
        if d == 0:
            return std
        if d == 2:
            return drop
        if 3 <= d <= 5:
            low = NOTES[(low_pc + k - d) % 12]
            return "Drop %s" % low if k == 0 else "%s Drop %s" % (std.split()[0], low)
        return None
    return None


def display(name):
    words = []
    for w in name.split(" "):
        if len(w) == 2 and w[0] in "ABCDEFG" and w[1] in "b#":
            words.append(w[0] + ("♭" if w[1] == "b" else "♯"))
        else:
            words.append(w.upper())
    return " ".join(words)


def tuning_name(chart, suffix, offsets, strings, bass, extended):
    """(name as the game shows it, sure, where it comes from). OFFSETS: the manifest's string0.. in order (the
    native format's: lowest string first)."""
    if (chart, suffix) in KNOWN_NAMES:
        return KNOWN_NAMES[(chart, suffix)], True, "seen in the game"
    if extended:
        o = offsets
        if not bass and strings == 7:
            t = o[1:7] + [o[0]]
            base = ALT7.get(tuple(t)) or _family(0, t)
        elif not bass and strings == 8:
            base = _family(1, o[2:8] + [o[1], o[0]])
        elif bass and strings in (5, 6):
            k = o[1]
            played = o[1:5] + (o[5:6] if strings == 6 else [])
            base = None
            if all(x == k for x in played):
                t = [k] * 6 + [o[0]]
                base = ALT7.get(tuple(t)) or _family(0, t)
        else:
            base = None
        if base:
            return display("%s %dStr" % (base, strings)), False, "rearmed-mods' seven_tuning_names.h"
        return None, False, "no name in rearmed-mods' tables: the game's own with the %dSTR mark" % strings
    six = tuple((offsets + [0] * 6)[:6]) if not bass else tuple((offsets + [0] * 6)[:4]) + (None, None)
    for t, n in STOCK6.items():
        if (bass and t[:4] == six[:4]) or (not bass and t == six):
            return display(n), False, "the usual name of these offsets (not read from the game)"
    return None, False, "not a usual tuning"


# --- what a chart holds ----------------------------------------------------------------------------------

GUITAR_OPEN = [40, 45, 50, 55, 59, 64]  # E2 A2 D3 G3 B3 E4
BASS_OPEN = [28, 33, 38, 43]            # E1 A1 D2 G2


def open_notes(offsets, strings, bass):
    """MIDI notes of the open strings, lowest first: OFFSETS in the manifest's order (native: lowest first)."""
    if bass:
        base = {4: BASS_OPEN, 5: [23] + BASS_OPEN, 6: [23] + BASS_OPEN + [48]}[strings]
    else:
        base = {6: GUITAR_OPEN, 7: [35] + GUITAR_OPEN, 8: [30, 35] + GUITAR_OPEN}[strings]
    return [b + (offsets[i] if i < len(offsets) else 0) for i, b in enumerate(base)]


def describe(chart_id, path):
    """Song key, title, artist and arrangements of a package."""
    content, magic = read_package(path)
    song = {}
    arrs = []
    for name, a in manifests(content):
        stem = os.path.basename(name)[:-5]
        suffix = stem.split("_")[-1]
        arr = a.get("ArrangementName")
        song.setdefault("song_key", a.get("SongKey"))
        if a.get("SongName"):
            song.setdefault("title", a.get("SongName"))
            song.setdefault("artist", a.get("ArtistName"))
            song.setdefault("album", a.get("AlbumName"))
        if arr in ("Vocals", "JVocals", "ShowLights"):
            arrs.append({"name": arr, "file": suffix, "instrument": "vocals", "persistent_id": a.get("PersistentID")})
            continue
        props = a.get("ArrangementProperties") or {}
        bass = arr == "Bass" or props.get("pathBass") == 1
        tuning = a.get("Tuning") or {}
        offsets = [tuning.get("string%d" % i, 0) for i in range(len(tuning))]
        extended = "ExtendedRange" in a
        strings = len(tuning) if extended else (4 if bass else 6)
        sng = next((v for k, v in content.items() if k.endswith("/%s.sng" % stem)), b"")
        body = sng_body("songs/bin/%s/x.sng" % ("macos" if "/macos/" in "".join(content) else "generic"), sng)
        sng_strings = body[6] if body[:4] == b"SNXR" else (4 if bass else 6) if body else None
        name, sure, source = tuning_name(chart_id, suffix, offsets, strings, bass, extended)
        notes = open_notes(offsets, strings, bass)
        arrs.append({
            "name": arr, "file": suffix, "persistent_id": a.get("PersistentID"),
            "instrument": "bass" if bass else "guitar", "strings": strings, "sng_strings": sng_strings,
            "extended": extended, "represent": props.get("represent"), "bonus": props.get("bonusArr"),
            "tuning": tuning, "cent_offset": a.get("CentOffset", 0), "open_midi": notes, "lowest_midi": min(notes),
            "tuning_name": name, "tuning_name_sure": sure, "tuning_name_from": source,
        })
    order = ["Lead", "Rhythm", "Combo", "Bass"]
    arrs.sort(key=lambda x: (order.index(x["name"]) if x["name"] in order else 9, -(x.get("represent") or 0),
                             x.get("bonus") or 0, x["file"]))
    song["arrangements"] = arrs
    song["native"] = magic == plain_psarc.NATIVE_MAGIC
    song["magic"] = (magic or b"PSAR").decode()
    return song


# --- the songs each edition lists (for the search texts) ---------------------------------------------------

def hsan_songs(path):
    """{song key: (title, artist, album)} of every .hsan in an archive (stock or plain), reading only those."""
    out = {}
    with open(path, "rb") as f:
        head = f.read(32)
        f.seek(0)
        if plain_psarc.plain_magic(head):
            content, _ = plain_psarc.read(f)
            items = [(n, d) for n, d in content.items() if n.endswith(".hsan")]
        else:
            h = HEADER.parse_stream(f)
            names = read_entry(f, 0, h.bom).decode("utf-8", "replace").splitlines()
            items = [(n, read_entry(f, i, h.bom)) for i, n in enumerate(names, 1) if n.endswith(".hsan")]
    for _, data in items:
        try:
            j = json.loads(data.decode("utf-8-sig"))
        except ValueError:
            continue
        for v in j.get("Entries", {}).values():
            a = v.get("Attributes", {})
            if a.get("SongKey") and a.get("SongName") and not str(a.get("SongName")).startswith("$["):
                out[a["SongKey"]] = (a.get("SongName", ""), a.get("ArtistName", ""), a.get("AlbumName", ""))
    return out


def edition_songs(game, cache):
    """{song key: (title, artist, album)} of a game copy: songs.psarc and the copy's own dlc archives (its
    manifest's, not songs put in its dlc by hand)."""
    songs = {}
    if not os.path.isdir(game):
        return songs
    m = {}
    try:
        with open(os.path.join(game, ".rearmed", "manifest.json")) as f:
            m = json.load(f).get("files", {})
    except (OSError, ValueError):
        pass
    paths = [os.path.join(game, "songs.psarc")] + [os.path.join(game, n) for n in m if n.startswith("dlc/")]
    for p in paths:
        try:
            st = os.stat(p)
        except OSError:
            continue
        key = "%s|%d|%d" % (p, st.st_size, st.st_mtime_ns)
        if key not in cache:
            try:
                cache[key] = hsan_songs(p)
            except Exception as e:  # a damaged or unreadable archive: left out of the search check
                cache[key] = {}
                say("  note: %s not read (%s)" % (p, e))
        songs.update({k: tuple(v) for k, v in cache[key].items()})
    return songs


def matches(text, songs):
    t = text.lower()
    return sorted(k for k, (title, artist, album) in songs.items()
                  if t in (title or "").lower() or t in (artist or "").lower() or t in (album or "").lower())


def pick_search(key, title, songs):
    """The shortest piece of the title from a word's start, 4 to 10 characters (within one word if one does), that
    finds only this song in SONGS (by title, artist or album); else the whole title."""
    words = re.sub(r"[^a-z0-9 ]", " ", title.lower()).split()
    joined = " ".join(words)
    starts = [0] + [m.end() for m in re.finditer(" ", joined)]
    for spaces in (False, True):  # a piece of one word first: no Space to type
        for n in range(4, 11):
            for s in starts:
                piece = joined[s:s + n]
                if (len(piece) == n and not piece.endswith(" ") and (spaces or " " not in piece)
                        and matches(piece, songs) == [key]):
                    return piece
    return joined


def stock_song(edition, key, game):
    """The stock song of an edition's normal-flow cases, read from its songs.psarc."""
    path = os.path.join(game, "songs.psarc")
    with open(path, "rb") as f:
        h = HEADER.parse_stream(f)
        names = read_entry(f, 0, h.bom).decode().splitlines()
        arrs = []
        title = artist = None
        for i, n in enumerate(names, 1):
            if n.endswith(".json") and os.path.basename(n).lower().startswith(key.lower() + "_"):
                j = json.loads(read_entry(f, i, h.bom).decode("utf-8-sig"))
                for v in j["Entries"].values():
                    a = v["Attributes"]
                    if a.get("SongKey") != key or a.get("ArrangementName") in ("Vocals", "JVocals"):
                        continue
                    title, artist = a.get("SongName"), a.get("ArtistName")
                    props = a.get("ArrangementProperties") or {}
                    bass = a.get("ArrangementName") == "Bass"
                    t = a.get("Tuning") or {}
                    offsets = [t.get("string%d" % s, 0) for s in range(6)]
                    name, _, src = tuning_name(None, None, offsets, 4 if bass else 6, bass, False)
                    arrs.append({"name": a.get("ArrangementName"), "file": os.path.basename(n)[:-5].split("_")[-1],
                                 "instrument": "bass" if bass else "guitar", "strings": 4 if bass else 6,
                                 "represent": props.get("represent"), "tuning": t, "tuning_name": name,
                                 "tuning_name_sure": False, "tuning_name_from": src,
                                 "open_midi": open_notes(offsets, 4 if bass else 6, bass)})
    if not arrs:
        raise Fail("%s: no %s in %s" % (edition, key, path))
    arrs.sort(key=lambda x: (x["file"]))
    lead = next((x for x in arrs if x["name"] == "Lead" and x["represent"] == 1), None)
    bass = next((x for x in arrs if x["name"] == "Bass" and x["represent"] == 1), None)
    if not lead or not bass:
        raise Fail("%s: %s lacks a main Lead or Bass" % (edition, key))
    return {"song_key": key, "title": title, "artist": artist, "archive": "songs.psarc",
            "lead": lead["file"], "bass": bass["file"], "arrangements": arrs,
            "purpose": ["song", "bass", "pause_menu", "score_attack", "song_lists", "companion", "tonehost", "mods"]}


# --- the made-up song with seven arrangements -------------------------------------------------------------

def fnv1(s):
    h = 2166136261
    for b in s.lower().encode():
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= b
    return h


def stable_u32(label):
    return int.from_bytes(hashlib.sha256(("gametest/" + label).encode()).digest()[:4], "little") | 0x01000000


def replace_u32(data, old, new, label):
    o, n = struct.pack("<I", old), struct.pack("<I", new)
    if data.count(o) == 0:
        raise Fail("%s: %08x is not in the bank" % (label, old))
    if data.count(n):
        raise Fail("%s: the new id %08x is in the bank already" % (label, new))
    return data.replace(o, n)


def rebank(data, old_name, new_name, old_event, new_event, wems):
    """A Wwise bank (v91, as the toolkit's template) of the song renamed: its id (BKHD, STID) and the name STID
    gives it, its Play event's id, the ids of its media (WEMS {old: new}). Checked section by section."""
    out = bytearray()
    off = 0
    old_id = new_id = None
    sections = []
    while off < len(data):
        tag, size = data[off:off + 4], struct.unpack("<I", data[off + 4:off + 8])[0]
        sections.append((tag, data[off + 8:off + 8 + size]))
        off += 8 + size
    if off != len(data) or [t for t, _ in sections][:1] != [b"BKHD"]:
        raise Fail("bank %s: unexpected layout" % old_name)
    for tag, body in sections:
        if tag == b"BKHD":
            old_id = struct.unpack("<I", body[4:8])[0]
            new_id = stable_u32("bank/" + new_name)
            body = body[:4] + struct.pack("<I", new_id) + body[8:]
        elif tag == b"STID":
            kind, count = struct.unpack("<II", body[:8])
            bid, ln = struct.unpack("<IB", body[8:13])
            name = body[13:13 + ln].decode()
            if count != 1 or bid != old_id or name.lower() != old_name.lower() or len(body) != 13 + ln:
                raise Fail("bank %s: STID is not the one name expected (%s)" % (old_name, name))
            nm = new_name.encode()
            body = struct.pack("<IIIB", kind, 1, new_id, len(nm)) + nm
        out += tag + struct.pack("<I", len(body)) + body
    data = bytes(out)
    data = replace_u32(data, fnv1(old_event), fnv1(new_event), old_event)
    for old, new in wems.items():
        data = replace_u32(data, old, new, "media %d" % old)
    return data


def crlf_json(obj):
    return json.dumps(obj, indent=2, ensure_ascii=False).replace("\n", "\r\n").encode("utf-8")


def graph_block(name, tags, canonical, relpath, llid=False, logpath=None):
    u = uuid.uuid5(ALT_NS, "graph/" + relpath)
    lines = ['<urn:uuid:%s> <http://emergent.net/aweb/1.0/tag> "%s".' % (u, t) for t in tags]
    lines.append('<urn:uuid:%s> <http://emergent.net/aweb/1.0/canonical> "%s".' % (u, canonical))
    lines.append('<urn:uuid:%s> <http://emergent.net/aweb/1.0/name> "%s".' % (u, name))
    if llid:
        lines.append('<urn:uuid:%s> <http://emergent.net/aweb/1.0/llid> "%08x-0000-0000-0000-000000000000".'
                     % (u, stable_u32("llid/" + relpath)))
    if logpath:
        lines.append('<urn:uuid:%s> <http://emergent.net/aweb/1.0/logpath> "%s".' % (u, logpath))
    lines.append('<urn:uuid:%s> <http://emergent.net/aweb/1.0/relpath> "%s".' % (u, relpath))
    return lines


XBLOCK_PROPS = [("Header", "urn:database:hsan-db:songs_dlc_{k}"), ("Manifest", "urn:database:json-db:{k}_{s}"),
                ("SngAsset", "urn:application:musicgame-song:{k}_{s}"),
                ("AlbumArtSmall", "urn:image:dds:album_{k}_64"), ("AlbumArtMedium", "urn:image:dds:album_{k}_128"),
                ("AlbumArtLarge", "urn:image:dds:album_{k}_256"), ("LyricArt", ""),
                ("ShowLightsXMLAsset", "urn:application:xml:{k}_showlights"),
                ("SoundBank", "urn:audio:wwise-sound-bank:song_{k}"),
                ("PreviewSoundBank", "urn:audio:wwise-sound-bank:song_{k}_preview")]


def make_alternates(nickelback_pc, cdlc_mac, out_path):
    """GameTestAlternates_p.psarc: a stock package (encrypted table and SNGs) of a new song with seven arrangements:
    Lead, Lead (alternate), Lead (bonus), Rhythm and its two, from Nickelback's 6-string Rhythm (its chart, audio,
    art and show lights), and a Bass from the stock custom song (its 4-string chart). Every id is new (uuid5 of
    ALT_NS: the same every run), and every name the xblock, the manifests, the header and the graph use is there."""
    nb, _ = read_package(nickelback_pc)
    cd, _ = read_package(cdlc_mac)
    nb_man = {os.path.basename(n)[:-5].split("_")[-1]: (n, a) for n, a in manifests(nb)}
    cd_man = {os.path.basename(n)[:-5].split("_")[-1]: (n, a) for n, a in manifests(cd)}
    old_key = nb_man["rhythm"][1]["SongKey"]
    ok = old_key.lower()
    nb_hsan = json.loads(next(v for n, v in nb.items() if n.endswith(".hsan")).decode("utf-8-sig"))
    cd_hsan = json.loads(next(v for n, v in cd.items() if n.endswith(".hsan")).decode("utf-8-sig"))
    hsan_of = {}
    for src_hsan in (nb_hsan, cd_hsan):
        for pid, v in src_hsan["Entries"].items():
            hsan_of[pid] = v["Attributes"]
    k, K = ALT_KEY.lower(), ALT_KEY
    rhythm_sng = sng_body("songs/bin/generic/x.sng", nb["songs/bin/generic/%s_rhythm.sng" % ok])
    cd_bass_name = next(n for n in cd if n.endswith("_bass.sng"))
    bass_sng = sng_body(cd_bass_name, cd[cd_bass_name])
    if rhythm_sng[:4] == b"SNXR" or bass_sng[:4] == b"SNXR":
        raise Fail("the made-up song's sources must be stock 6/4-string charts")
    old_wems = sorted(int(os.path.basename(n)[:-4]) for n in nb if n.endswith(".wem"))
    wems = {w: stable_u32("wem/%d" % w) % 2000000000 + 100000000 for w in old_wems}

    song_fields = {
        "SongKey": K, "DLCKey": K, "SongName": ALT_TITLE, "SongNameSort": ALT_TITLE, "ArtistName": ALT_ARTIST,
        "ArtistNameSort": ALT_ARTIST, "AlbumName": "Game Test", "AlbumNameSort": "Game Test",
        "AlbumArt": "urn:image:dds:album_%s" % k, "BlockAsset": "urn:emergent-world:%s" % k,
        "ShowlightsXML": "urn:application:xml:%s_showlights" % k, "SongBank": "song_%s.bnk" % k,
        "PreviewBankPath": "song_%s_preview.bnk" % k, "SongEvent": "Play_%s" % K,
    }
    content = {}
    jsons, hsan_entries, ids = {}, {}, []
    for suffix, arr, represent, bonus, source in ALT_ARRANGEMENTS:
        pid = uuid.uuid5(ALT_NS, "arrangement/" + suffix).hex.upper()
        ids.append((suffix, pid))
        src_name, src_attr = (nb_man["rhythm"] if source == "rhythm" else cd_man["bass"])
        a = copy.deepcopy(src_attr)
        hs = copy.deepcopy(hsan_of[src_attr["PersistentID"]])
        a.update(song_fields)
        atype, route = ROUTE[arr]
        a.update({
            "ArrangementName": arr, "ArrangementType": atype, "ArrangementSort": 0, "PersistentID": pid,
            "FullName": "%s_%s" % (K, suffix.capitalize()), "ManifestUrn": "urn:database:json-db:%s_%s" % (k, suffix),
            "SongAsset": "urn:application:musicgame-song:%s_%s" % (k, suffix),
            "SongXml": "urn:application:xml:%s_%s" % (k, suffix),
            "MasterID_RDV": stable_u32("master/" + suffix) % 900000000 + 100000000,
            "SongLength": nb_man["rhythm"][1]["SongLength"],
        })
        props = a["ArrangementProperties"]
        props.update({"represent": represent, "bonusArr": bonus, "pathLead": int(arr == "Lead"),
                      "pathRhythm": int(arr == "Rhythm"), "pathBass": int(arr == "Bass"), "routeMask": route})
        for f in ("ExtendedRange",):
            a.pop(f, None)
            hs.pop(f, None)
        hs.update({x: a[x] for x in ("SongKey", "DLCKey", "SongName", "SongNameSort", "ArtistName", "ArtistNameSort",
                                      "AlbumName", "AlbumNameSort", "AlbumArt", "ArrangementName", "PersistentID",
                                      "ManifestUrn", "MasterID_RDV", "SongLength") if x in a})
        hs["Representative"] = represent
        hs["RouteMask"] = route
        jsons[suffix] = {"Entries": {pid: {"Attributes": a}}, "ModelName": "RSEnumerable_Song",
                         "IterationVersion": 2, "InsertRoot": "Static.Songs.Entries"}
        hsan_entries[pid] = {"Attributes": hs}
        body = rhythm_sng if source == "rhythm" else bass_sng
        content["songs/bin/generic/%s_%s.sng" % (k, suffix)] = encrypt_sng(body, WIN_KEY)
    content["songs/arr/%s_showlights.xml" % k] = nb["songs/arr/%s_showlights.xml" % ok]
    for suffix, _ in ids:
        content["manifests/songs_dlc_%s/%s_%s.json" % (k, k, suffix)] = crlf_json(jsons[suffix])
    content["manifests/songs_dlc_%s/songs_dlc_%s.hsan" % (k, k)] = crlf_json(
        {"Entries": hsan_entries, "InsertRoot": nb_hsan.get("InsertRoot", "Static.Songs.Headers")})
    for size in (64, 128, 256):
        content["gfxassets/album_art/album_%s_%d.dds" % (k, size)] = nb["gfxassets/album_art/album_%s_%d.dds" % (ok, size)]
    ents = []
    for suffix, pid in ids:
        props = "".join('\r\n        <property name="%s">\r\n          <set value="%s" />\r\n        </property>'
                        % (p, v.format(k=k, s=suffix)) for p, v in XBLOCK_PROPS)
        ents.append('    <entity id="%s" modelName="RSEnumerable_Song" name="%s_%s" iterations="0">\r\n'
                    '      <properties>%s\r\n      </properties>\r\n    </entity>'
                    % (pid.lower(), K, suffix.capitalize(), props))
    content["gamexblocks/nsongs/%s.xblock" % k] = (
        '﻿<?xml version="1.0" encoding="utf-8"?>\r\n<game>\r\n  <entitySet>\r\n' + "\r\n".join(ents)
        + "\r\n  </entitySet>\r\n</game>").encode("utf-8")
    content["flatmodels/rs/rsenumerable_song.flat"] = nb["flatmodels/rs/rsenumerable_song.flat"]
    content["flatmodels/rs/rsenumerable_root.flat"] = nb["flatmodels/rs/rsenumerable_root.flat"]
    g = graph_block(k, ["emergent-world", "x-world"], "/gamexblocks/nsongs", "/gamexblocks/nsongs/%s.xblock" % k)
    for suffix, _ in ids:
        g += graph_block("%s_%s" % (k, suffix), ["database", "json-db"], "/manifests/songs_dlc_%s" % k,
                         "/manifests/songs_dlc_%s/%s_%s.json" % (k, k, suffix))
        g += graph_block("%s_%s" % (k, suffix), ["application", "musicgame-song"], "/songs/bin/generic",
                         "/songs/bin/generic/%s_%s.sng" % (k, suffix), True, "/songs/bin/%s_%s.sng" % (k, suffix))
    g += graph_block("%s_showlights" % k, ["application", "xml"], "/songs/arr", "/songs/arr/%s_showlights.xml" % k,
                     True, "/songs/arr/%s_showlights.xml" % k)
    g += graph_block("songs_dlc_%s" % k, ["database", "hsan-db"], "/manifests/songs_dlc_%s" % k,
                     "/manifests/songs_dlc_%s/songs_dlc_%s.hsan" % (k, k))
    for size in (64, 128, 256):
        p = "/gfxassets/album_art/album_%s_%d.dds" % (k, size)
        g += graph_block("album_%s_%d" % (k, size), ["dds", "image"], "/gfxassets/album_art", p, True, p)
    for tail in ("", "_preview"):
        g += graph_block("song_%s%s" % (k, tail), ["audio", "wwise-sound-bank", "dx9"], "/audio/windows",
                         "/audio/windows/song_%s%s.bnk" % (k, tail), True, "/audio/song_%s%s.bnk" % (k, tail))
    content["%s_aggregategraph.nt" % k] = ("﻿" + "\n".join(g)).encode("utf-8")
    for tail in ("", "_preview"):
        old = nb["audio/windows/song_%s%s.bnk" % (ok, tail)]
        content["audio/windows/song_%s%s.bnk" % (k, tail)] = rebank(
            old, "Song_%s%s" % (old_key, tail), "Song_%s%s" % (K, tail), "Play_%s%s" % (old_key, tail),
            "Play_%s%s" % (K, tail), {w: n for w, n in wems.items()
                                     if struct.pack("<I", w) in old})
    for w, n in wems.items():
        content["audio/windows/%d.wem" % n] = nb["audio/windows/%d.wem" % w]
    content["toolkit.version"] = b"Toolkit version: rearmed-mods tests/game/fixtures.py\r\nPackage Author: game test\r\n" \
                                 b"Package Version: 1\r\nPackage Comment: made up for the game tests"
    content["appid.appid"] = b"248750"
    check_alternates(content)
    tmp = out_path + ".part"
    with open(tmp, "wb") as f:
        PSARC(False).build_stream(content, f)
    os.replace(tmp, out_path)
    return content


def check_alternates(content):
    """Every id unique, every reference of the xblock, manifests and header resolving, every file in the graph."""
    k = ALT_KEY.lower()
    graph = content["%s_aggregategraph.nt" % k].decode("utf-8-sig").split("\n")
    by = {}
    for line in graph:
        m = re.match(r'<urn:uuid:([0-9a-f-]+)> <http://emergent.net/aweb/1.0/(\w+)> "(.*)"\.$', line)
        if not m:
            raise Fail("graph line: %s" % line)
        by.setdefault(m.group(1), {}).setdefault(m.group(2), []).append(m.group(3))
    urns, llids, xblock_name = set(), [], None
    for u, f in by.items():
        tags = f["tag"]
        urns.add("urn:%s:%s:%s" % (tags[0], tags[1], f["name"][0]))  # the graph's order is not always the urn's
        urns.add("urn:%s:%s:%s" % (tags[1], tags[0], f["name"][0]))
        if tags[0] == "emergent-world":
            xblock_name = f["name"][0]
        llids += f.get("llid", [])
        rel = f["relpath"][0].lstrip("/")
        if rel not in content:
            raise Fail("graph names %s, not in the package" % rel)
    graphed = {f["relpath"][0].lstrip("/") for f in by.values()}
    for name in content:
        if name.endswith((".json", ".hsan", ".sng", ".xml", ".dds", ".bnk", ".xblock")) and name not in graphed:
            raise Fail("%s is not in the graph" % name)
    if len(set(llids)) != len(llids):
        raise Fail("graph llids repeat")
    pids, masters, fulls = [], [], []
    for name, a in manifests(content):
        pids.append(a["PersistentID"])
        masters.append(a["MasterID_RDV"])
        fulls.append(a["FullName"])
        for ref in ("ManifestUrn", "SongAsset", "ShowlightsXML"):
            if a[ref] not in urns:
                raise Fail("%s: %s %s does not resolve" % (name, ref, a[ref]))
        if a["BlockAsset"] != "urn:emergent-world:%s" % xblock_name:
            raise Fail("%s: BlockAsset %s is not the xblock's" % (name, a["BlockAsset"]))
        for f in ("SongBank", "PreviewBankPath"):
            if "audio/windows/" + a[f] not in content:
                raise Fail("%s: %s %s missing" % (name, f, a[f]))
    for lst, what in ((pids, "PersistentID"), (masters, "MasterID_RDV"), (fulls, "FullName")):
        if len(set(lst)) != len(lst) or len(lst) != len(ALT_ARRANGEMENTS):
            raise Fail("%s: not %d unique" % (what, len(ALT_ARRANGEMENTS)))
    hs = json.loads(content["manifests/songs_dlc_%s/songs_dlc_%s.hsan" % (k, k)].decode("utf-8-sig"))
    if sorted(hs["Entries"]) != sorted(pids):
        raise Fail("the header's entries are not the manifests'")
    for pid, v in hs["Entries"].items():
        if v["Attributes"]["PersistentID"] != pid or v["Attributes"]["ManifestUrn"] not in urns:
            raise Fail("header entry %s" % pid)
    xb = content["gamexblocks/nsongs/%s.xblock" % k].decode("utf-8-sig")
    ents = re.findall(r'<entity id="([0-9a-f]{32})"', xb)
    if sorted(e.upper() for e in ents) != sorted(pids):
        raise Fail("the xblock's entities are not the manifests'")
    for v in re.findall(r'<set value="([^"]*)" />', xb):
        if v and v not in urns:
            raise Fail("xblock value %s does not resolve" % v)


# --- sounds ----------------------------------------------------------------------------------------------

def pluck(notes, seconds, rate=RATE, stagger=0.012):
    """Plucked strings: harmonics 1/k^1.3 (12 at most), each decaying faster than the one below; NOTES plucked
    STAGGER s apart (a strum, low to high). Peak at -6 dBFS. -> 16-bit samples."""
    n = int(seconds * rate)
    out = [0.0] * n
    for idx, midi in enumerate(notes):
        f0 = 440.0 * 2 ** ((midi - 69) / 12)
        start = int(idx * stagger * rate)
        tau1 = 1.6 if midi < 45 else 1.2
        for kk in range(1, 13):
            f = f0 * kk
            if f > 9000:
                break
            amp = 1.0 / kk ** 1.3
            tau = tau1 / (1 + 0.5 * (kk - 1))
            w = 2 * math.pi * f / rate
            dec = math.exp(-1.0 / (tau * rate))
            # a rotating phasor: cos/sin by recurrence, the decay by a factor a sample
            c, s = 1.0, 0.0
            cw, sw = math.cos(w), math.sin(w)
            a = amp
            for i in range(start, n):
                out[i] += a * s
                c, s = c * cw - s * sw, s * cw + c * sw
                a *= dec
                if a < 1e-4 * amp:
                    break
    peak = max(abs(x) for x in out) or 1.0
    g = 0.5 / peak
    return [max(-32767, min(32767, int(round(x * g * 32767)))) for x in out]


def write_wav(path, samples, rate=RATE):
    tmp = path + ".part"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))
    os.replace(tmp, path)


# --- the run ---------------------------------------------------------------------------------------------

def load_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_json(path, data):
    tmp = path + ".part"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=1, ensure_ascii=False, sort_keys=False)
        f.write("\n")
    os.replace(tmp, path)


class Fixtures:
    def __init__(self, root, extra, check_only):
        self.root = root
        self.dir = os.path.join(root, "fixtures")
        self.pc, self.mac, self.audio = (os.path.join(self.dir, d) for d in ("pc", "mac", "audio"))
        self.check_only = check_only
        self.extra = extra
        self.old = load_json(os.path.join(self.dir, "fixtures.json")) or {}
        self.done = []
        self.problems = []

    def path(self, form, name):
        return os.path.join(self.pc if form == "pc" else self.mac, name)

    def unchanged(self, cid, source_sha, recorded_inputs=None):
        """The chart as recorded: the same source, both files there with the recorded hashes."""
        old = (self.old.get("charts") or {}).get(cid)
        if not old or old.get("source_sha256") != source_sha or old.get("maker") != FORMAT:
            return None
        if recorded_inputs is not None and old.get("inputs") != recorded_inputs:
            return None
        for form in ("pc", "mac"):
            p = self.path(form, old["files"][form])
            if not os.path.isfile(p) or sha256(p) != old["sha256"][form]:
                return None
        return old

    def place_pair(self, cid, src, purpose, inputs=None, made=None):
        """SRC (a chart in either form) and its other form into pc/ and mac/, checked; its fixtures.json entry."""
        name = os.path.basename(src)
        form = form_of(name)
        src_sha = sha256(src)
        old = self.unchanged(cid, src_sha, inputs)
        if old:
            self.done.append("%s: as before" % cid)
            return old
        if self.check_only:
            self.problems.append("%s: missing or changed (run fixtures.py without --check)" % cid)
            return (self.old.get("charts") or {}).get(cid)
        names = {form: name, ("mac" if form == "pc" else "pc"): other_name(name)}
        for f, n in names.items():  # a native package's name keeps its _rsxr tag in both forms
            if f != form and name.lower()[:-8].endswith("_rsxr") and not n.lower()[:-8].endswith("_rsxr"):
                names[f] = n[:-8] + "_rsxr" + n[-8:]
        with tempfile.TemporaryDirectory(dir=self.dir, prefix=".tmp-") as tmp:
            mine = os.path.join(tmp, name)
            shutil.copy2(src, mine)
            out = convert(mine, os.path.join(tmp, "out"))
            if os.path.basename(out) != names["mac" if form == "pc" else "pc"]:
                names["mac" if form == "pc" else "pc"] = os.path.basename(out)
            pc_tmp = mine if form == "pc" else out
            mac_tmp = out if form == "pc" else mine
            bad = compare_forms(pc_tmp, mac_tmp)
            if bad:
                raise Fail("%s: the two forms differ: %s" % (cid, "; ".join(bad)))
            for f, p in (("pc", pc_tmp), ("mac", mac_tmp)):
                dst = self.path(f, names[f])
                old_entry = (self.old.get("charts") or {}).get(cid)
                if old_entry and old_entry["files"].get(f) != names[f]:
                    try:
                        os.remove(self.path(f, old_entry["files"][f]))
                    except OSError:
                        pass
                os.replace(p, dst)
        info = describe(cid, self.path("pc", names["pc"]))
        entry = {"files": names, "sha256": {f: sha256(self.path(f, n)) for f, n in names.items()},
                 "source": src, "source_sha256": src_sha, "made": made or ("converted from the %s form" % form),
                 "maker": FORMAT, "purpose": purpose}
        if inputs is not None:
            entry["inputs"] = inputs
        entry.update(info)
        self.done.append("%s: %s and %s" % (cid, names["pc"], names["mac"]))
        return entry

    def check_pair(self, cid, entry):
        for form in ("pc", "mac"):
            p = self.path(form, entry["files"][form])
            if not os.path.isfile(p):
                self.problems.append("%s: %s missing" % (cid, p))
                return
            if sha256(p) != entry["sha256"][form]:
                self.problems.append("%s: %s changed since fixtures.json" % (cid, p))
        bad = compare_forms(self.path("pc", entry["files"]["pc"]), self.path("mac", entry["files"]["mac"]))
        if bad:
            self.problems.append("%s: the two forms differ: %s" % (cid, "; ".join(bad)))

    def alternates(self, nb_entry, cd_entry):
        cid = "alternates"
        inputs = {"nickelback_pc": nb_entry["sha256"]["pc"], "cdlc_mac": cd_entry["sha256"]["mac"],
                  "generator": FORMAT}
        name = "%s_p.psarc" % ALT_KEY
        old = (self.old.get("charts") or {}).get(cid)
        if old and old.get("inputs") == inputs and self.unchanged(cid, old.get("source_sha256"), inputs):
            self.done.append("%s: as before" % cid)
            return old
        if self.check_only:
            self.problems.append("%s: missing or changed (run fixtures.py without --check)" % cid)
            return old
        with tempfile.TemporaryDirectory(dir=self.dir, prefix=".tmp-") as tmp:
            src = os.path.join(tmp, name)
            make_alternates(self.path("pc", nb_entry["files"]["pc"]), self.path("mac", cd_entry["files"]["mac"]), src)
            keep = os.path.join(self.dir, ".made")
            os.makedirs(keep, exist_ok=True)
            made = os.path.join(keep, name)
            os.replace(src, made)
        entry = self.place_pair(cid, made, ["alternates"], inputs=None,
                                made="made by fixtures.py from Nickelback's Rhythm and %s's Bass, then converted"
                                     % cd_entry.get("title"))
        entry["inputs"] = inputs
        return entry

    def sounds(self):
        os.makedirs(self.audio, exist_ok=True)
        out = {}
        old = self.old.get("audio") or {}
        for sid, fname, notes, secs in SOUNDS:
            p = os.path.join(self.audio, fname)
            o = old.get(sid)
            if o and o.get("version") == SOUND_VERSION and os.path.isfile(p) and sha256(p) == o.get("sha256"):
                out[sid] = o
                continue
            if self.check_only:
                self.problems.append("audio %s: missing or changed" % fname)
                continue
            write_wav(p, pluck(notes, secs))
            out[sid] = {"file": "audio/" + fname, "midi": notes, "seconds": secs, "rate": RATE, "channels": 1,
                        "bits": 16, "version": SOUND_VERSION, "sha256": sha256(p),
                        "what": "an E standard open strum (E2 A2 D3 G3 B3 E4, 12 ms apart)" if len(notes) > 1
                        else "one plucked string at MIDI %d, decaying over %.0f s" % (notes[0], secs)}
            self.done.append("audio: %s" % fname)
        return out

    def run(self):
        for d in (self.pc, self.mac, self.audio):
            if not self.check_only:
                os.makedirs(d, exist_ok=True)
        charts = {}
        for cid, fname, purpose in NATIVE:
            src = os.path.join(DOWNLOADS, fname)
            if not os.path.isfile(src):
                self.problems.append("%s: %s is not there" % (cid, src))
                continue
            charts[cid] = self.place_pair(cid, src, purpose)
        cid, src, purpose = STOCK_CDLC
        if os.path.isfile(src):
            charts[cid] = self.place_pair(cid, src, purpose)
        else:
            self.problems.append("%s: %s is not there" % (cid, src))
        if charts.get("nickelback") and charts.get(cid):
            charts["alternates"] = self.alternates(charts["nickelback"], charts[cid])
        for src in self.extra:
            src = os.path.abspath(os.path.expanduser(src))
            xid = re.sub(r"[^a-z0-9]+", "_", os.path.basename(src)[:-8].lower()).strip("_")
            if not os.path.isfile(src):
                self.problems.append("%s: %s is not there" % (xid, src))
                continue
            charts[xid] = self.place_pair(xid, src, ["extra"])
        # the stock songs, and the search texts (each edition's songs: songs.psarc and the copy's own DLC)
        cache_path = os.path.join(self.dir, ".songs-cache.json")
        cache = load_json(cache_path) or {}
        songs = {"mac": edition_songs(PLAYER_MAC_GAME, cache), "lp": edition_songs(PLAYER_LP_GAME, cache)}
        if not self.check_only:
            write_json(cache_path, cache)
        for e in songs:
            for c in charts.values():
                if c and c.get("song_key"):
                    songs[e][c["song_key"]] = (c.get("title") or "", c.get("artist") or "", c.get("album") or "")
        stock = {}
        for edition, key in STOCK_SONGS.items():
            game = PLAYER_MAC_GAME if edition == "mac" else PLAYER_LP_GAME
            try:
                s = stock_song(edition, key, game)
            except (OSError, Fail) as e:
                self.problems.append("stock song %s: %s" % (edition, e))
                continue
            s["search"] = pick_search(key, s["title"], songs[edition])
            s["search_matches"] = matches(s["search"], songs[edition])
            stock[edition] = s
        for c in charts.values():
            if not c or not c.get("song_key"):
                continue
            both = {}
            for e in songs:
                both.update(songs[e])
            c["search"] = pick_search(c["song_key"], c["title"], both)
            c["search_matches"] = {e: matches(c["search"], songs[e]) for e in songs}
        audio = self.sounds()
        data = {
            "format": FORMAT, "made": time.strftime("%Y-%m-%d %H:%M:%S"), "root": self.root,
            "notes": {
                "files": "files.pc in fixtures/pc, files.mac in fixtures/mac; audio files relative to fixtures/",
                "tuning": "the manifest's Tuning as it is (native packages: string0 is the lowest string); "
                          "open_midi: the open strings, lowest first",
                "tuning_name": "as the game shows it; tuning_name_sure false: derived (tuning_name_from says how)",
                "search": "the text that finds the song alone in Learn a Song's search, checked against the titles, "
                          "artists and albums of each edition's songs.psarc and the game copy's own DLC (not songs "
                          "put in dlc by hand, nor how the game itself matches: a run shows the count)",
                "stock_songs": "per edition, the disc song of the normal-flow cases (Learn & Play has no other disc "
                               "song than Brannan Street Blues): lead and bass name the arrangement files",
            },
            "charts": charts, "stock_songs": stock, "audio": audio,
        }
        if self.check_only:
            for cid, entry in charts.items():
                if entry:
                    self.check_pair(cid, entry)
            if not self.old:
                self.problems.append("no fixtures.json yet")
        else:
            write_json(os.path.join(self.dir, "fixtures.json"), data)
        return data


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("charts", nargs="*", help="more charts (_p or _m .psarc)")
    ap.add_argument("--root", default=None)
    ap.add_argument("--check", action="store_true", help="check, change nothing; exit 1 if anything is off")
    a = ap.parse_args(argv)
    root = os.path.abspath(os.path.expanduser(a.root or os.environ.get("RS_GAMETEST_ROOT") or "~/rs-gametest"))
    extra = list(a.charts) + [p for p in (os.environ.get("RS_GAMETEST_CHARTS") or "").split(os.pathsep) if p]
    t0 = time.time()
    fx = Fixtures(root, extra, a.check)
    try:
        data = fx.run()
    except Fail as e:
        say("fixtures: error: %s" % e)
        return 1
    for line in fx.done:
        say("  " + line)
    say("fixtures in %s (%.1f s)%s:" % (fx.dir, time.time() - t0, " (checked)" if a.check else ""))
    for cid, c in (data.get("charts") or {}).items():
        if not c:
            continue
        arrs = ", ".join("%s %s%s" % (x["name"], x.get("strings", ""), (" " + x["tuning_name"]) if x.get("tuning_name")
                                      else "") for x in c.get("arrangements", []) if x.get("instrument") != "vocals")
        say("  %-12s %-34s search %-12r %s" % (cid, (c.get("artist") or "") + " - " + (c.get("title") or ""),
                                             c.get("search"), arrs))
    for e, s in (data.get("stock_songs") or {}).items():
        say("  stock %-6s %-34s search %-12r lead %s, bass %s (%d match)" % (
            e, s["artist"] + " - " + s["title"], s["search"], s["lead"], s["bass"], len(s["search_matches"])))
    say("  audio: %s" % ", ".join(v["file"] for v in (data.get("audio") or {}).values()))
    for p in fx.problems:
        say("  PROBLEM: " + p)
    return 1 if fx.problems else 0


if __name__ == "__main__":
    sys.exit(main())
