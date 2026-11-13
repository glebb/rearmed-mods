#!/usr/bin/env python3
"""Moves the plug-in a config.json has in itself into a profile of its own, plugins/ID, with its states and levels.

    migrate.py FOLDER --id ID [--name NAME] [--host RS-TONEHOST]

The profile is whole before the config names it, and the old files go last. --host is the binary that tells the
plug-in's version.
"""
import argparse, json, os, shutil, subprocess

MOVED = ("plugin", "templates", "map")


def main():
    a = argparse.ArgumentParser()
    a.add_argument("folder")
    a.add_argument("--id", required=True)
    a.add_argument("--name")
    a.add_argument("--host")
    o = a.parse_args()
    if o.id.startswith(".") or "/" in o.id:
        raise SystemExit("not a profile's name: " + o.id)
    file = os.path.join(o.folder, "config.json")
    c = json.load(open(file))
    if "plugin" not in c or "templates" not in c:
        raise SystemExit("nothing to move: the config has no plug-in of its own" + (", and plays " + c["profile"] if c.get("profile") else ""))
    final = os.path.join(o.folder, "plugins", o.id)
    if os.path.exists(final):
        raise SystemExit(final + " is there already")
    profile = {"name": o.name or o.id}
    profile.update({k: c[k] for k in MOVED if k in c})
    if o.host:
        p = c["plugin"]
        profile["plugin_version"] = int(subprocess.check_output([o.host, "--plugin", p["type"], p["subtype"], p["manufacturer"]]))

    stage = os.path.join(o.folder, "plugins", "." + o.id + ".new")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    # A state beside the config goes along at the same place; one somewhere else stays where it is
    states = sorted({t["state"] for t in c["templates"].values() if not os.path.isabs(t.get("state", "/"))})
    for s in states + ["levels.plist"]:
        source = os.path.join(o.folder, s)
        if os.path.exists(source):
            os.makedirs(os.path.dirname(os.path.join(stage, s)), exist_ok=True)
            shutil.copy2(source, os.path.join(stage, s))
    json.dump(profile, open(os.path.join(stage, "profile.json"), "w"), indent=1)
    os.rename(stage, final)

    for k in MOVED:
        c.pop(k, None)
    c["profile"] = o.id
    json.dump(c, open(file + ".new", "w"), indent=1)
    os.replace(file + ".new", file)

    for s in states + ["levels.plist"]:
        source = os.path.join(o.folder, s)
        if os.path.exists(source):
            os.remove(source)
            try:
                os.removedirs(os.path.dirname(source))  # only an empty one goes, and never the folder: it has the config
            except OSError:
                pass
    print("%s: %d templates, %d states" % (final, len(profile["templates"]), len(states)))


if __name__ == "__main__":
    main()
