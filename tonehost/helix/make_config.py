#!/usr/bin/env python3
"""Writes Helix Native's profile from make_presets.py's tones.json, and the host's config with that profile chosen,
keeping the player's settings if a config exists. Both go to OUT, for setup.sh to put in place once the states are.

    make_config.py TONES.json CONFIG.json --out OUT --id ID [--name NAME] [--version N]
                   [--input DEVICE] [--channel N, from 1] [--output DEVICE]
"""
import argparse, json, os

PLUGIN = {"type": "aumf", "subtype": "6HPA", "manufacturer": "LSIX"}
DEFAULTS = {
    "input": {"device": "", "channel": 0, "gain_db": 6},
    "buffer": 128, "fade_ms": 20, "target_db": -22.5, "probe_peak_db": -12,
    "gate": {"margin_db": 12, "tight_db": 8, "min_db": -70, "max_db": -40, "hold_ms": [150, 40], "release_ms": [120, 30]},
}


def main():
    a = argparse.ArgumentParser()
    a.add_argument("tones")
    a.add_argument("config")
    a.add_argument("--input")
    a.add_argument("--channel", type=int)
    a.add_argument("--output")
    a.add_argument("--out", required=True)
    a.add_argument("--id", required=True)
    a.add_argument("--name")
    a.add_argument("--version", type=int)
    o = a.parse_args()
    t = json.load(open(o.tones))
    c = json.load(open(o.config)) if os.path.exists(o.config) else json.loads(json.dumps(DEFAULTS))
    if o.input:
        c["input"]["device"] = o.input
    if o.channel:
        c["input"]["channel"] = o.channel - 1
    if o.output:
        c["output"] = {"device": o.output}
    if not c["input"]["device"]:
        raise SystemExit("say which device the guitar is on: --input NAME --channel N")
    c["fifo"] = os.path.join(os.path.dirname(os.path.abspath(o.config)), "commands")
    for k in ("plugin", "templates", "map"):  # a config from before profiles has the plug-in itself
        c.pop(k, None)
    c["profile"] = o.id
    p = {"name": o.name or o.id, "plugin": PLUGIN, "templates": {}}
    if o.version is not None:
        p["plugin_version"] = o.version
    for name, v in t["templates"].items():
        e = {"state": "states/" + v["state"], "output_db": 0, "params": v["params"]}
        for k in ("tight", "follow"):
            if k in v:
                e[k] = v[k]
        p["templates"][name] = e
    p["map"] = {"gain_knobs": ["_Gain", "_Loudness1", "_Loudness2"], "drive_pedals": t["drive_pedals"], "clean": t["fallback"]["clean"],
                "driven": t["fallback"]["driven"], "driven_from_gain": 0.3, "by_prefix": t["by_prefix"], "follow": t["follow"], "trim": t.get("trim", [])}
    json.dump(p, open(os.path.join(o.out, "profile.json"), "w"), indent=1)
    json.dump(c, open(os.path.join(o.out, "config.json"), "w"), indent=1)
    print("%s: %d templates, guitar on %s channel %d" % (o.id, len(p["templates"]), c["input"]["device"], c["input"]["channel"] + 1))


if __name__ == "__main__":
    main()
