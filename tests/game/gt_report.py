"""report.md of a run: a table an edition, the failed checks with their lines, the snapshots worth a look, the builds."""
import json
import os


def _esc(s):
    return str(s).replace("|", "\\|").replace("\n", " ")


def write(runner, total_s):
    out = []
    a = runner.args
    out.append("# Game tests %s" % runner.stamp)
    out.append("")
    out.append("Suite `%s`%s, builds `%s`, %.0f s in all. Cases: `<edition>/<case>/` (game.log, companion.log, "
               "events.jsonl, snap/, RSMods.ini, result.json)." % (
                   a.suite, " (cases %s)" % ", ".join(a.case) if a.case else "", a.builds, total_s))
    out.append("")
    for ed, res in runner.results.items():
        n = {s: sum(1 for r in res if r["status"] == s) for s in ("PASS", "FAIL", "SKIP", "MANUAL")}
        out.append("## %s: %d pass, %d fail, %d skip, %d manual (%s s)" % (
            {"mac": "Mac edition", "lp": "Learn & Play"}.get(ed, ed), n["PASS"], n["FAIL"], n["SKIP"], n["MANUAL"],
            runner.sessions.get(ed + "_time", "?")))
        out.append("")
        out.append("| Case | Result | Time | Notes |")
        out.append("|---|---|---|---|")
        for r in res:
            notes = []
            skipped = [c["name"] for c in r["checks"] if c["status"] == "SKIP"]
            failed = [c["name"] for c in r["checks"] if c["status"] == "FAIL"]
            if r.get("why"):
                notes.append(r["why"] + (" (on %s)" % r["where"] if r.get("where") else ""))
            if failed:
                notes.append("failed: " + ", ".join(failed))
            if skipped:
                notes.append("%d check(s) skipped: %s" % (len(skipped), ", ".join(skipped)))
            res_s = r["status"]
            out.append("| %s | %s | %.1f s | %s |" % (r["case"], res_s, r["time_s"], _esc("; ".join(notes))))
        out.append("")
        sess = [s for s in runner.sessions.get(ed, []) if isinstance(s, dict)]
        if sess:
            out.append("Game starts:")
            out.append("")
            for s in sess:
                for st in s["starts"]:
                    t0 = st.get("t_start")
                    tt = "title %.1f s, main menu %.1f s" % (st["t_title"] - t0, st["t_main"] - t0) \
                        if t0 and st.get("t_title") and st.get("t_main") else st.get("error", "did not start")
                    clean = st.get("exit_code") == 0 and st.get("quit_how") not in ("SIGTERM", "SIGKILL")
                    out.append("- %s start %d (%s): %s; quit: %s, exit status %s%s; charts: %s%s" % (
                        s["label"], st["n"], ", ".join(s["cases"]), tt, st.get("quit_how"), st.get("exit_code"),
                        "" if clean else " **NOT A CLEAN QUIT** (a negative status is the signal that ended it; "
                                         "~/Library/Logs/DiagnosticReports has its crash report)",
                        ", ".join(s["charts"]) or "none",
                        "; start settings %s" % s["start_ini"] if s["start_ini"] else ""))
            out.append("")
        fails = [r for r in res if r["status"] == "FAIL"]
        for r in fails:
            out.append("### %s %s: FAIL" % (ed, r["case"]))
            out.append("")
            if r.get("why"):
                out.append("Route: %s (on `%s`)." % (r["why"], r.get("where")))
                out.append("")
                if r.get("trace"):
                    out.append("Last steps: " + "; ".join("%s s %s" % (t, x) for t, x in r["trace"][-8:]))
                    out.append("")
            for c in r["checks"]:
                if c["status"] != "FAIL":
                    continue
                out.append("- **%s** (%s; `%s`): %s" % (c["name"], c.get("src", ""), c.get("pattern", ""),
                                                         c.get("why", "")))
                if c.get("context"):
                    out.append("")
                    out.append("  ```")
                    for i, t in c["context"]:
                        out.append("  %5d  %s" % (i, t[:220]))
                    out.append("  ```")
            out.append("")
        snaps = [(r["case"], s) for r in res for s in r.get("snapshots", [])]
        if snaps:
            out.append("Snapshots worth a look: " + ", ".join("`%s/%s/%s`" % (ed, c, s) for c, s in snaps))
            out.append("")
    out.append("## Builds")
    out.append("")
    b = runner.builds or {}
    out.append("From `builds.json` (made %s, `--builds %s`)." % (b.get("made", "?"), b.get("builds", "?")))
    out.append("")
    out.append("| Edition | Part | Commit | Built | sha256 | From |")
    out.append("|---|---|---|---|---|---|")
    for ed, parts in (b.get("parts") or {}).items():
        if not isinstance(parts, dict):
            continue
        for k, v in parts.items():
            if not isinstance(v, dict):
                continue
            commit = (v.get("commit") or "")[:10]
            if commit and str(v.get("dirty")) == "True":
                commit += " (changes)"
            out.append("| %s | %s | %s %s | %s | %s | `%s` |" % (
                ed, k, commit or v.get("from", ""), (v.get("branch") or ""), v.get("built", ""),
                (v.get("sha256") or "")[:12], _esc(v.get("source", ""))))
    for k, v in (b.get("tools") or {}).items():
        if isinstance(v, dict):
            out.append("| tools | %s | %s | | %s | `%s` |" % (k, (v.get("commit") or "")[:10], (v.get("sha256") or "")[:12],
                                                          _esc(v.get("source", ""))))
    out.append("")
    path = os.path.join(runner.run_dir, "report.md")
    with open(path, "w") as f:
        f.write("\n".join(out))
    return path
