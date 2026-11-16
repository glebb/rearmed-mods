#!/usr/bin/env python3
"""Makes Helix Native's side of rs-tonehost from gear.json and the Helix install's own model files.

    make_presets.py OUTDIR [--setlist]

OUTDIR gets presets/, the pieces of a preset as the plug-in's engine takes one: a frame with nothing in it, a block
for each Helix amp with its cab, and a block for each effect. The host puts a tone's preset together from them. It gets
tones.json too, what the host's config needs: the blocks in the chain's order, where each knob has its value in one,
and the rules from the game's knobs to parameters. With --setlist it gets setlist.hls, a whole preset per amp for
Helix's own import: verify.sh compares the two through the plug-in. Every value not set here is Line 6's default, read from the install, and nothing of Line 6's is kept in the
repository. The chain is the same for every amp, so the knobs and
switches mean the same everywhere: octave up and down, wah, fuzz, metal, drive, amp and its default cab, compressor, then
acoustic sim, chorus, flanger, phaser, tremolo, delay and reverb on the second path, as a path has room for eight blocks.
A Helix preamp in place of an amp has no cab, and knobs it has no use for can go to a block's spare_knobs. README.md has
what was learned about Helix's files.
"""
import base64, json, os, struct, sys, time, zlib

RES = "/Library/Application Support/Line 6/Helix Native/res"
CORE = "/Library/Frameworks/HelixCoreLib.framework/Resources"  # Line 6's headers for the plug-in's engine, as installed
NATIVE = 2162944  # Helix Native in Helix compatibility mode
ORDER = ["octave_up", "octave_down", "wah", "fuzz", "metal", "drive", None, "compressor",  # None: the amp
         "acoustic", "chorus", "flanger", "phaser", "tremolo", "delay", "reverb"]
PATH_ONE = 8  # blocks on the first path; the rest go on the second
TO_PATH_2A = 2  # @output of the first path
SWITCH = 16  # Audio Unit parameters: knobs 0-15, switches 16-31
AMP_KNOBS = {"Bass": 1, "Mid": 2, "Treble": 3, "Presence": 4}
GAIN_KNOB = 0
GAIN = ["Drive", "Gain", "DriveNorm", "Ch1Drive", "Ch2Drive", "Norm Drive"]  # what an amp calls its gain, the first it has
GAIN_FIRST = {"AmpAguaSledge": "Gain",  # its Drive is a second stage, at 0 by default
              "PreampVintagePre": "Input"}  # Studio Tube Pre: a mic pre, its gain called Input
BLOCK_TYPE = {"drive": 0, "compressor": 0, "chorus": 0, "phaser": 0, "tremolo": 0, "delay": 0, "reverb": 0}


def models():
    out = {}
    for f in os.listdir(RES):
        if f.endswith(".models"):
            data = json.load(open(os.path.join(RES, f)))
            for m in data if isinstance(data, list) else []:
                if isinstance(m, dict) and m.get("symbolicID"):
                    out[m["symbolicID"]] = m
    return out


def defaults(m):
    return {p["symbolicID"]: p["default"] for p in m["params"] if not p["symbolicID"].startswith("@") and "default" in p}


def block(m, position, enabled, extra=None):
    b = {"@model": m["symbolicID"], "@position": position, "@path": 0, "@type": 0, "@enabled": enabled, "@no_snapshot_bypass": False, "@stereo": False}
    b.update(defaults(m))
    b.update(extra or {})
    return b


def preset(name, amp, base, M, gear):
    h = json.loads(json.dumps(base))
    t = h["data"]["tone"] if "data" in h else h["tone"]
    d, second = t["dsp0"], t["dsp1"]
    for dsp in (d, second):
        for k in [k for k in dsp if k.startswith(("block", "cab"))]:
            del dsp[k]
    d["outputA"]["@output"] = TO_PATH_2A
    second["inputA"]["@input"] = 0
    second["outputA"]["@output"] = 1
    assign, knob_default, blocks = [None] * (SWITCH + 16), {}, {"dsp0": {}, "dsp1": {}}
    lent = []  # rules for the knobs a block took over from this preset's amp
    sw = 0
    for index, kind in enumerate(ORDER):
        dsp_name, pos = ("dsp0", index) if index < PATH_ONE else ("dsp1", index - PATH_ONE)
        key = "block%d" % pos
        if kind is None:
            m = M["HD2_" + amp]
            extra = dict(gear.get("amp_set", {}).get(amp, {}), **{"@bypassvolume": 1})
            if amp.startswith("Preamp"):  # no cab: full range, as a DI is
                a = block(m, pos, True, dict(extra, **{"@type": 1}))
            else:
                a = block(m, pos, True, dict(extra, **{"@type": 3, "@cab": "cab0"}))
                cab = M[m.get("ircablink") or "HD2_CabMicIr_4x12Greenback25"]
                d["cab0"] = {"@model": cab["symbolicID"], "@enabled": True}
                d["cab0"].update(defaults(cab))
            a.pop("@stereo")
            d[key] = a
            blocks["dsp0"][key] = True
            have = defaults(m)
            gain = GAIN_FIRST.get(amp) or next(p for p in GAIN if p in have)
            for p, k in {gain: GAIN_KNOB, **AMP_KNOBS}.items():
                if p in have:
                    assign[k] = {"@param": "dsp0.%s.%s" % (key, p), "@min": 0, "@max": 1}
                    knob_default[str(k)] = have[p]
            continue
        spec = gear["blocks"][kind]
        m = M[spec["model"]]
        dsp = t[dsp_name]
        dsp[key] = block(m, pos, False, spec.get("set"))
        blocks[dsp_name][key] = False
        ranges = {p["symbolicID"]: (p["min"], p["max"]) for p in m["params"] if "min" in p}
        for p, k in spec.get("knobs", {}).items():
            lo, hi = ranges[p]
            if p == "Time":
                hi = 2.0  # of 8 s: a knob's whole travel for the times the game's pedals have
            assign[k] = {"@param": "%s.%s.%s" % (dsp_name, key, p), "@min": lo, "@max": hi}
            knob_default[str(k)] = (dsp[key][p] - lo) / (hi - lo)
        # {"Body": [1, "_Body"]}: where the amp left knob 1 free, it is this block's Body, from the game's knob ending _Body,
        # 0-100 over the knob's travel. Only that preset's template has the rule, after the shared ones it overrides.
        for p, (k, suffix) in spec.get("spare_knobs", {}).items():
            if assign[k] is None:
                lo, hi = ranges[p]
                assign[k] = {"@param": "%s.%s.%s" % (dsp_name, key, p), "@min": lo, "@max": hi}
                knob_default[str(k)] = (dsp[key][p] - lo) / (hi - lo)
                lent += [{"param": k, "from": suffix, "prefix": g + "_", "scale": 0.01, "offset": 0} for g in sorted(spec["gear"])]
        assign[SWITCH + sw] = {"@param": "%s.%s.@enabled" % (dsp_name, key), "@min": False, "@max": True}
        sw += 1
    while assign and assign[-1] is None:
        assign.pop()
    t["automation"] = {"@assignments": assign}
    t.pop("footswitch", None)
    t.pop("controller", None)
    d["inputA"]["noiseGate"] = False
    for s in [k for k in t if k.startswith("snapshot")]:
        if "blocks" in t[s]:
            t[s]["blocks"] = json.loads(json.dumps(blocks))
        t[s].pop("controllers", None)
    meta = h["data"]["meta"] if "data" in h else h["meta"]
    meta["name"] = name[:16]
    return (h["data"] if "data" in h else h), knob_default, lent


# ---- The engine's own preset ----
# Helix Native hands its engine, HelixCoreLib, a preset as one plain block of 18,332 bytes, and the host can hand it one
# too. HelixCorePreset.h, installed with the framework, lays it out, packed: two flows (dsp0, dsp1), each a topology and
# twenty blocks of 458 bytes, then the tempo. A flow's blocks are path A's input, eight effects and output, then path
# B's, whose input and output hold the split and the join. A model is a number, its place in symbolTableMasterList.h,
# and an effect's name there ends in Mono or Stereo. A block's values are in the engine's order (Engine.params):
# without the ones only the editor has (@...) and, in a mono effect, without the ones marked stereo-only. A delay or
# reverb has its trails switch after them. README.md has how this was checked against the plug-in.
BLOCK, VALUES, PER_FLOW, UNSET = 458, 2 + 27 * 8, 20, 0xFFFFFFFF
FLOW = 4 + PER_FLOW * BLOCK
SINGLE, AMP, AMP_AND_CAB, TRAILS = 1, 17, 33, 8  # HCMFXType, for the kinds of block these presets have
INPUT, OUTPUT, BRANCH_IN, BRANCH_OUT, EFFECT, EMPTY = 0, 1, 2, 3, 6, 8  # HCMType


class Engine:
    def __init__(self, M):
        text = open(os.path.join(CORE, "symbolTableMasterList.h")).read()
        names = [s.split("//")[0].strip() for s in text[text.index("{") + 1:text.index("}")].split(",")]
        if any("=" in n for n in names):
            raise ValueError("the model list numbers its models itself")
        self.M, self.number, self.order = M, {n: i for i, n in enumerate(n for n in names if n)}, {}
        self.catalog(json.load(open(os.path.join(RES, "HX_ModelCatalog.json"))))

    def catalog(self, o):
        if isinstance(o, dict):
            if isinstance(o.get("id"), str) and isinstance(o.get("params"), list):
                self.order[o["id"]] = [k for p in o["params"] if isinstance(p, dict) for k in p]
            o = list(o.values())
        for v in o if isinstance(o, list) else []:
            self.catalog(v)

    def name(self, model, stereo=False):
        base = model.split("_", 1)[1]
        return next(n for n in ([base + "Stereo", base + "Mono"] if stereo else [base + "Mono", base + "Stereo"]) + [base] if n in self.number)

    def params(self, model, name):
        """The model's parameters as the engine has them, in its order: the models file's, but where the catalog has some
        of them in another order, as nine models' are, those in the catalog's. Matchstick Ch2 is the one that was checked."""
        order = self.order.get(model, [])
        ps = [p for p in self.M[model]["params"] if not p["symbolicID"].startswith("@") and not (name.endswith("Mono") and p.get("stereo-only"))]
        listed = iter(sorted((p for p in ps if p["symbolicID"] in order), key=lambda p: order.index(p["symbolicID"])))
        return [next(listed) if p["symbolicID"] in order else p for p in ps]

    def values(self, src):
        model = src["@model"]
        out = []
        for p in self.params(model, self.name(model, src.get("@stereo", False))):
            kind, v = p["default"], src.get(p["symbolicID"], p["default"])
            out.append(struct.pack("<IB3x", 1, bool(v)) if isinstance(kind, bool) else struct.pack("<Ii", 2, int(v)) if isinstance(kind, int) else struct.pack("<If", 3, v))
        own = len(out)
        if any(p["symbolicID"] == "@trails" for p in self.M[model]["params"]):
            out.append(struct.pack("<IB3x", 1, bool(src.get("@trails", False))))
        return (struct.pack("<BB", len(out), own) + b"".join(out)).ljust(VALUES, b"\0")

    def effect(self, src, cab):
        name = self.name(src["@model"], src.get("@stereo", False))
        if cab:
            head = struct.pack("<BIIIB", True, self.number[name], self.number[self.name(cab["@model"])], AMP_AND_CAB, src["@enabled"])
        else:
            trails = any(p["symbolicID"] == "@trails" for p in self.M[src["@model"]]["params"])
            head = struct.pack("<BIIIB", False, self.number[name], UNSET, AMP if name.startswith(("Amp", "Preamp")) else TRAILS if trails else SINGLE, src["@enabled"])
        return struct.pack("<II", EFFECT, UNSET) + head + self.values(src) + (self.values(cab) if cab else bytes(VALUES))

    def end(self, kind, src, to):
        """An input or an output: where it comes from or goes to, and its values if that is anywhere."""
        return struct.pack("<III", kind, UNSET, src[to]) + (self.values(src) if src[to] else bytes(VALUES))

    def branch(self, src):
        """A split or a join. These presets have one path: it is there, and not in the signal's way."""
        return struct.pack("<II", self.number[self.name(src["@model"])], 0) + self.values(src) + struct.pack("<BB", src["@enabled"], False)

    def preset(self, tone):
        out = b""
        for dsp in ("dsp0", "dsp1"):
            d = tone[dsp]
            blocks = [struct.pack("<IIBIIIB", EMPTY, UNSET, False, UNSET, UNSET, 0, True)] * PER_FLOW
            blocks[0], blocks[9] = self.end(INPUT, d["inputA"], "@input"), self.end(OUTPUT, d["outputA"], "@output")
            blocks[10] = self.end(BRANCH_IN, d["inputB"], "@input") + self.branch(d["split"])
            blocks[19] = self.end(BRANCH_OUT, d["outputB"], "@output") + self.branch(d["join"])
            for key, src in d.items():
                if key.startswith("block"):
                    blocks[src["@path"] * 10 + 1 + src["@position"]] = self.effect(src, d[src["@cab"]] if "@cab" in src else None)
            out += struct.pack("<I", 0) + b"".join(b.ljust(BLOCK, b"\0") for b in blocks)  # 0: one path
        return out + struct.pack("<f", tone["global"]["@tempo"])

    def start(self, tone, dsp, key):
        """Where in the preset a block starts."""
        return int(dsp[3:]) * FLOW + 4 + (tone[dsp][key]["@path"] * 10 + 1 + tone[dsp][key]["@position"]) * BLOCK

    def place(self, tone, param):
        """Where in the preset "dsp0.block6.Drive" has its value, a float, or "dsp0.block6.@enabled" its switch, a byte."""
        dsp, key, p = param.split(".", 2)
        src = tone[dsp][key]
        at = self.start(tone, dsp, key) + 8
        if p == "@enabled":
            return at + 13
        ids = [q["symbolicID"] for q in self.params(src["@model"], self.name(src["@model"], src.get("@stereo", False)))]
        return at + 14 + 2 + ids.index(p) * 8 + 4


def rules(gear):
    """The game's knobs onto parameters, the same for every preset. A value is scale * knob + offset."""
    # The input: 39.5 comes from a fit of a JCM800 with a drive pedal, and is too much for a tone with no pedal
    pedals = sorted(gear["blocks"]["drive"]["gear"])
    f = [{"param": 0, "from": "amp_gain", "scale": 0.93, "offset": 0},
         {"param": 32, "from": "amp_gain", "scale": 39.5, "offset": -14, "with": pedals},
         {"param": 32, "from": "amp_gain", "scale": 28, "offset": -14, "without": pedals}]
    # An amp's tone knobs go from 1 to 100, but on the amps in tone_db Bass, Mid and Treble are in dB, -12 to 12: 0 dB
    # is the middle of Helix's knob. The scale is 1/24 rounded down, so that 12 dB stays on the knob.
    db = [a + "_" for a in gear.get("tone_db", [])]
    for suffix, k in (("_Bass", 1), ("_Mid", 2), ("_Treble", 3)):
        f.append({"param": k, "from": suffix, "contains": "Amp_", "scale": 0.01, "offset": 0, "without": db})
        f.append({"param": k, "from": suffix, "contains": "Amp_", "scale": 0.041666, "offset": 0.5, "with": db})
    f.append({"param": 4, "from": "_Pres", "contains": "Amp_", "scale": 0.01, "offset": 0})
    # The octave's mix is the game's own, from a faint shimmer to a whole octave lead; the flanger's rate gave it its knob
    how = {("octave_up", "Mix"): ("_Mix", 0.01, 0), ("drive", "Gain"): ("_Gain", 0.0098, 0), ("drive", "Tone"): ("_Tone", 0.01, 0),
           ("compressor", "Threshold"): ("_Threshold", 1 / 60, 1), ("fuzz", "Sustain"): ("_Gain", 0.0098, 0), ("metal", "Drive"): ("_Gain", 0.0098, 0),
           ("chorus", "Mix"): ("_Mix", 0.01, 0),
           ("delay", "Time"): ("_Time", 0.0005, 0), ("delay", "Feedback"): ("_Feedback", 0.01, 0), ("delay", "Mix"): ("_Mix", 0.01, 0),
           ("reverb", "Mix"): ("_Mix", 0.01, 0)}
    other = {"_Comp": (-1 / 150, 1), "_Compress": (-1 / 150, 1), "_Smash": (-1 / 150, 1), "_Limit": (-1 / 150, 1), "_Boost": (0.0098, 0), "_GainWide": (0.0065, 0)}
    sw = 0
    for kind in [k for k in ORDER if k]:
        spec = gear["blocks"][kind]
        f.append({"param": SWITCH + sw, "present": sorted(spec["gear"])})
        sw += 1
        for key, names in spec["gear"].items():
            for p, k in spec.get("knobs", {}).items():
                suffix, scale, offset = how[(kind, p)]
                if p in names:
                    suffix = names[p]
                    scale, offset = other.get(suffix, (scale, offset))
                    suffix = {"_GainWide": "_Gain"}.get(suffix, suffix)  # the knob's real name, on another scale
                f.append({"param": k, "from": suffix, "prefix": key + "_", "scale": round(scale, 6), "offset": offset})
    return f


def main():
    out = sys.argv[1]
    os.makedirs(os.path.join(out, "presets"), exist_ok=True)
    here = os.path.dirname(os.path.abspath(__file__))
    gear = json.load(open(os.path.join(here, "gear.json")))
    M = models()
    base = json.load(open(os.path.join(here, "base.hlx")))
    used = sorted(set(gear["amps"].values()) | set(gear["fallback"].values()))
    missing = [a for a in used if "HD2_" + a not in M]
    if missing:
        sys.exit("not in this Helix: %s" % missing)
    presets, templates, blocks = [], {}, []
    try:
        engine = Engine(M)
    except (OSError, ValueError) as e:
        sys.exit("this Helix Native has no headers of its engine to make presets from: %s" % e)
    for i, amp in enumerate(used):
        p, knobs, lent = preset(amp[3:] if amp.startswith("Amp") else amp, amp, base, M, gear)
        presets.append(p)
        templates[amp] = {"slot": i, "state": amp + ".plist", "output_db": 0, "params": knobs}
        # The preset as the engine takes it, in pieces: the amp's block here, the effects' blocks and the frame once. A
        # place is counted from its block's start. The amp's own knobs, 0 to 4, are the template's, with the block they
        # are in: the amp, or the effect it lends a knob to
        tone = p["tone"]
        whole, places, own = engine.preset(tone), {}, {}
        for k, a in enumerate(tone["automation"]["@assignments"]):
            if not a:
                continue
            dsp, key, param = a["@param"].split(".", 2)
            kind = ORDER[(PATH_ONE if dsp == "dsp1" else 0) + tone[dsp][key]["@position"]] or "amp"
            place = [engine.place(tone, a["@param"]) - engine.start(tone, dsp, key)] + ([] if param == "@enabled" else [a["@min"], a["@max"]])
            if k <= max(AMP_KNOBS.values()):
                places[str(k)] = [kind] + place
            else:
                own.setdefault(kind, {})[str(k)] = place
        at = engine.start(tone, "dsp0", "block%d" % ORDER.index(None))
        open(os.path.join(out, "presets", amp + ".bin"), "wb").write(whole[at:at + BLOCK])
        templates[amp].update(block=amp + ".bin", places=places)
        if not blocks:
            for index, kind in enumerate(ORDER):
                if not kind:
                    blocks.append({"amp": True})
                    continue
                at = engine.start(tone, "dsp1" if index >= PATH_ONE else "dsp0", "block%d" % (index % PATH_ONE))
                open(os.path.join(out, "presets", kind + ".bin"), "wb").write(whole[at:at + BLOCK])
                blocks.append({"name": kind, "file": kind + ".bin", "on": SWITCH + len([b for b in blocks if "on" in b]), "places": own[kind]})
            empty = json.loads(json.dumps(tone))
            for dsp in ("dsp0", "dsp1"):
                empty[dsp] = {k: v for k, v in empty[dsp].items() if not k.startswith(("block", "cab"))}
            open(os.path.join(out, "presets", "frame.bin"), "wb").write(engine.preset(empty))
        follow = list(lent)
        if amp in gear["high_gain"]:  # gain enough of their own: less drive and input for the same knob, a tighter gate
            templates[amp]["tight"] = 1.3
            follow += [{"param": 0, "from": "amp_gain", "scale": 0.6, "offset": 0}, {"param": 32, "from": "amp_gain", "scale": 18, "offset": -14}]
        if follow:
            templates[amp]["follow"] = follow
    filler = json.loads(json.dumps(presets[-1]))
    filler["meta"]["name"] = "New Preset"
    while len(presets) < 128:
        presets.append(filler)
    raw = json.dumps({"meta": {"name": "RS Tone Host"}, "presets": presets}).encode()
    hls = {"version": 2, "meta": {"device_version": presets[0].get("device_version", 0), "device": NATIVE, "application": "Helix Native",
                                  "modifieddate": int(time.time()), "name": "RS Tone Host", "appversion": presets[0].get("device_version", 0)},
           "encoding": "Base64", "encoded_data": base64.b64encode(zlib.compress(raw)).decode(),
           "compression": {"crc32": zlib.crc32(raw) & 0xffffffff, "decompressed_size": len(raw), "type": "zlib"}, "schema": "L6Setlist"}
    if "--setlist" in sys.argv[2:]:
        json.dump(hls, open(os.path.join(out, "setlist.hls"), "w"), indent=2)
    by = {k + "_": v for k, v in gear["amps"].items()}
    json.dump({"templates": templates, "by_prefix": by, "fallback": gear["fallback"], "trim": gear.get("trim", []), "follow": rules(gear),
               "drive_pedals": sorted(k for b in ("drive", "fuzz", "metal") for k in gear["blocks"][b]["gear"]), "engine": "helix",
               "frame": "frame.bin", "blocks": blocks, "spots": [f * FLOW + 4 + (1 + i) * BLOCK for f in (0, 1) for i in range(PATH_ONE)]},
              open(os.path.join(out, "tones.json"), "w"), indent=1)
    print("%d presets, %d game amps, %d rules" % (len(used), len(gear["amps"]), len(rules(gear))))


if __name__ == "__main__":
    main()
