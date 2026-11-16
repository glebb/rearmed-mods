#!/usr/bin/env python3
"""Writes Helix Native's profile from make_presets.py's tones.json, and the host's config with that profile chosen,
keeping the player's settings if a config exists. Both go to OUT, for setup.sh to put in place with the presets.
--states gives each template its saved state as well, for verify.sh.

    make_config.py TONES.json CONFIG.json --out OUT --id ID [--name NAME] [--version N] [--states]
                   [--input DEVICE] [--channel N, from 1] [--output DEVICE]
"""
import argparse, json, os

PLUGIN = {"type": "aumf", "subtype": "6HPA", "manufacturer": "LSIX"}
DEFAULTS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "config.default.json")))


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
    a.add_argument("--states", action="store_true")
    o = a.parse_args()
    t = json.load(open(o.tones))
    c = json.load(open(o.config)) if os.path.exists(o.config) else DEFAULTS
    if o.input:
        c["input"]["device"] = o.input
    if o.channel:
        c["input"]["channel"] = o.channel - 1
    if o.output:
        c["output"] = {"device": o.output}
    c["fifo"] = os.path.join(os.path.dirname(os.path.abspath(o.config)), "commands")
    for k in ("plugin", "templates", "map"):  # a config from before profiles has the plug-in itself
        c.pop(k, None)
    c["profile"] = o.id
    p = {"name": o.name or o.id, "plugin": PLUGIN, "templates": {}}
    if o.version is not None:
        p["plugin_version"] = o.version
    for name, v in t["templates"].items():
        e = {"output_db": 0, "params": v["params"]}
        if o.states:
            e["state"] = "states/" + v["state"]
        for k in ("tight", "follow", "places"):
            if k in v:
                e[k] = v[k]
        e["preset"] = "presets/" + v["preset"]  # as the plug-in's engine takes it
        p["templates"][name] = e
    p["map"] = {"gain_knobs": ["_Gain", "_Loudness1", "_Loudness2"], "drive_pedals": t["drive_pedals"], "clean": t["fallback"]["clean"],
                "driven": t["fallback"]["driven"], "driven_from_gain": 0.3, "by_prefix": t["by_prefix"], "follow": t["follow"], "trim": t.get("trim", [])}
    p["engine"] = t["engine"]
    json.dump(p, open(os.path.join(o.out, "profile.json"), "w"), indent=1)
    json.dump(c, open(os.path.join(o.out, "config.json"), "w"), indent=1)
    on = "guitar on %s channel %d" % (c["input"]["device"], c["input"]["channel"] + 1) if c["input"]["device"] else "the interface is still to choose, in Re-ARMed Mods' Tone Host settings or with --input and --channel"
    print("%s: %d templates, %s" % (o.id, len(p["templates"]), on))


if __name__ == "__main__":
    main()
