#!/usr/bin/env python3
"""Turn the raw experiment logs into tables and figures.

Reads  results/raw/<mode>/<config>.log  (written by run_experiments.py)
Writes results/jobs_<mode>.csv      every JOB line, one row per job
       results/summary.md           all tables used in README.md
       docs/figures/*.png           figures used in README.md

All numbers come from the logs; nothing is typed in by hand.
"""

import csv
import glob
import os
import re
import statistics as stats
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402

RAW = "results/raw"
FIG = "docs/figures"

# Colours (validated with the dataviz palette checker): an ordinal blue
# ramp for queue levels (darker = higher priority) and categorical hues.
LEVEL = {0: "#104281", 1: "#3987e5", 2: "#86b6ef"}
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
INK, INK2, MUTED, GRID = "#0b0b0b", "#52514e", "#898781", "#e6e5e1"
RR_COLOR = "#898781"

CONFIG_LABEL = {
    "rr": "RR (original xv6)",
    "q2-4-8": "MLFQ 2/4/8",
    "mlfq": "MLFQ 4/8/16",
    "q8-16-32": "MLFQ 8/16/32",
    "aging0": "aging off",
    "aging25": "aging 25",
    "aging100": "aging 100",
}

RUNNING, RUNNABLE, SLEEPING = 4, 3, 2


# ---------------------------------------------------------------- parsing

def parse_log(path):
    """Return (jobs, events, configs) from one raw log.

    jobs:   list of dicts (JOB lines; numbers converted to int)
    events: {exp: [event dicts]} for repetition 1
    config: {exp: CONFIG dict}
    """
    jobs, events, configs = [], defaultdict(list), {}
    exp, cfg = None, None
    for line in open(path, errors="replace"):
        line = line.strip()
        kv = dict(re.findall(r"(\w+)=(\S+)", line))
        for k, v in kv.items():
            if re.fullmatch(r"-?\d+", v):
                kv[k] = int(v)
        if line.startswith("CONFIG"):
            cfg = kv
        elif line.startswith("BEGIN"):
            exp = kv["exp"]
            configs[exp] = cfg
        elif line.startswith("JOB"):
            jobs.append(kv)
        elif line.startswith("EV") and exp:
            events[exp].append(kv)
    return jobs, events, configs


def load(mode):
    data = {}
    for path in sorted(glob.glob(f"{RAW}/{mode}/*.log")):
        name = os.path.basename(path)[:-4]
        data[name] = parse_log(path)
    return data


# ------------------------------------------------------------ helpers

def ms(us):
    return us / 1000.0


def fmt(x, nd=1):
    return f"{x:.{nd}f}"


def by_rep(jobs, exp):
    reps = defaultdict(list)
    for j in jobs:
        if j["exp"] == exp:
            reps[j["rep"]].append(j)
    return reps


def per_job(jobs, exp, key):
    """{job name: [value per repetition]} (stream jobs are pooled)."""
    out = defaultdict(list)
    for j in jobs:
        if j["exp"] == exp:
            out[j["name"]].append(j[key])
    return out


def mean(xs):
    return stats.mean(xs) if xs else float("nan")


def sd(xs):
    return stats.pstdev(xs) if len(xs) > 1 else 0.0


def intervals(events, pid):
    """RUNNING intervals of pid: [(start_us, end_us, level)]."""
    out, start, level = [], None, 0
    for e in events:
        if e["pid"] != pid or e["type"] != "S":
            continue
        if e["to"] == RUNNING:
            start, level = e["t"], e["prio"]
        elif e["from"] == RUNNING and start is not None:
            out.append((start, e["t"], level))
            start = None
    return out


def wake_latencies(events, pid):
    """Time from each wakeup (SLEEPING -> RUNNABLE) to the next dispatch."""
    out, woke = [], None
    for e in events:
        if e["pid"] != pid or e["type"] != "S":
            continue
        if e["from"] == SLEEPING and e["to"] == RUNNABLE:
            woke = e["t"]
        elif e["to"] == RUNNING and woke is not None:
            out.append(e["t"] - woke)
            woke = None
    return out


def pct(xs, p):
    xs = sorted(xs)
    if not xs:
        return float("nan")
    k = (len(xs) - 1) * p / 100
    lo = int(k)
    hi = min(lo + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def pid_of(jobs, exp, name, rep=1):
    for j in jobs:
        if j["exp"] == exp and j["rep"] == rep and j["name"] == name:
            return j["pid"]
    return None


def md_table(header, rows):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---" if i == 0 else "---:"
                          for i in range(len(header))) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def style(ax, title=None, xlabel=None, ylabel=None):
    ax.set_facecolor("#fcfcfb")
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(MUTED)
    ax.tick_params(colors=INK2, labelsize=9)
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    if title:
        ax.set_title(title, loc="left", color=INK, fontsize=11)
    if xlabel:
        ax.set_xlabel(xlabel, color=INK2, fontsize=9)
    if ylabel:
        ax.set_ylabel(ylabel, color=INK2, fontsize=9)


def save(fig, name):
    os.makedirs(FIG, exist_ok=True)
    fig.patch.set_facecolor("#fcfcfb")
    fig.savefig(f"{FIG}/{name}", dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {FIG}/{name}")


# ------------------------------------------------------------ figures

def gantt(ax, jobs, events, exp, order, title, policy):
    """One row per job; bars are RUNNING intervals coloured by level."""
    for row, name in enumerate(order):
        pid = pid_of(jobs, exp, name)
        for s, e, lvl in intervals(events, pid):
            color = RR_COLOR if policy == "RR" else LEVEL[lvl]
            ax.broken_barh([(ms(s), max(ms(e - s), 0.3))], (row - 0.35, 0.7),
                           facecolors=color, edgecolor="none")
    ax.set_yticks(range(len(order)))
    ax.set_yticklabels(order)
    ax.invert_yaxis()
    style(ax, title=title)


def fig_gantt(data, exp, order, fname, xmax=None):
    fig, axes = plt.subplots(2, 1, figsize=(10, 1.2 + 0.55 * len(order) * 2),
                             sharex=True)
    for ax, cfg, label in ((axes[0], "mlfq", "MLFQ (4/8/16 ticks)"),
                           (axes[1], "rr", "Round robin (original xv6)")):
        jobs, events, configs = data[cfg]
        gantt(ax, jobs, events[exp], exp, order, label,
              configs[exp]["policy"])
    axes[1].set_xlabel("time since first job created (ms)", color=INK2,
                       fontsize=9)
    if xmax:
        axes[1].set_xlim(0, xmax)
    handles = [Patch(color=LEVEL[i], label=f"running in Q{i}")
               for i in range(3)]
    handles.append(Patch(color=RR_COLOR, label="running (RR)"))
    fig.legend(handles=handles, loc="upper right", ncol=4, frameon=False,
               fontsize=8, bbox_to_anchor=(1.0, 1.02))
    fig.tight_layout()
    save(fig, fname)


def fig_single_levels(data, fname):
    jobs, events, _ = data["mlfq"]
    pid = pid_of(jobs, "single", "cpu")
    fig, ax = plt.subplots(figsize=(9, 2.6))
    cpu = 0.0
    for s, e, lvl in intervals(events["single"], pid):
        ax.broken_barh([(ms(s), ms(e - s))], (lvl - 0.3, 0.6),
                       facecolors=LEVEL[lvl], edgecolor="none")
        cpu += ms(e - s)
    for e in events["single"]:
        if e["pid"] == pid and e["type"] == "L":
            ax.annotate(f"Q{e['from']}→Q{e['to']} at {ms(e['t']):.0f} ms",
                        (ms(e["t"]), e["to"]), xytext=(6, -2),
                        textcoords="offset points", fontsize=8, color=INK2,
                        va="center")
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["Q0", "Q1", "Q2"])
    ax.invert_yaxis()
    style(ax, "Experiment 1: one CPU-bound job (500 ms of CPU) moves down "
              "the queues", "time (ms)", "queue level")
    save(fig, fname)


def fig_io_latency(data, fname):
    fig, ax = plt.subplots(figsize=(7, 3.2))
    for i, (cfg, label) in enumerate((("mlfq", "MLFQ 4/8/16"),
                                      ("rr", "RR (original xv6)"))):
        jobs, events, _ = data[cfg]
        lat = sorted(ms(x) for x in
                     wake_latencies(events["mixed"],
                                    pid_of(jobs, "mixed", "io")))
        ys = [(k + 1) / len(lat) for k in range(len(lat))]
        ax.step(lat, ys, where="post", color=SERIES[i], linewidth=2,
                label=label)
        ax.annotate(label, (lat[len(lat) // 2], ys[len(lat) // 2]),
                    xytext=(8, -12 if i else 8), textcoords="offset points",
                    color=INK2, fontsize=8)
    ax.set_ylim(0, 1.02)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    style(ax, "Experiment 3: I/O-bound job, delay from wakeup to running",
          "wakeup latency (ms)", "fraction of wakeups")
    ax.legend(frameon=False, fontsize=8, loc="lower right")
    save(fig, fname)


def long_cpu_curve(jobs, events, pid):
    xs, ys, cpu = [0.0], [0.0], 0.0
    for s, e, _ in intervals(events, pid):
        xs += [ms(s), ms(e)]
        ys += [cpu, cpu + ms(e - s)]
        cpu += ms(e - s)
    return xs, ys


def fig_aging(data, fname):
    fig, ax = plt.subplots(figsize=(8, 3.6))
    cfgs = [("aging0", "aging off"), ("aging25", "threshold 25 ticks"),
            ("mlfq", "threshold 50 ticks"),
            ("aging100", "threshold 100 ticks")]
    for i, (cfg, label) in enumerate(cfgs):
        if cfg not in data or "aging" not in data[cfg][1]:
            continue
        jobs, events, _ = data[cfg]
        xs, ys = long_cpu_curve(jobs, events["aging"],
                                pid_of(jobs, "aging", "long"))
        ax.plot(xs, ys, color=SERIES[i], linewidth=2, label=label)
        if cfg == "aging0":
            # The flat stretch is the starvation.
            flat = max(range(1, len(xs)), key=lambda k: xs[k] - xs[k - 1]
                       if ys[k] == ys[k - 1] else 0)
            ax.annotate(f"aging off: no CPU for "
                        f"{(xs[flat] - xs[flat - 1]) / 1000:.1f} s",
                        ((xs[flat] + xs[flat - 1]) / 2, ys[flat]),
                        xytext=(0, -14), textcoords="offset points",
                        fontsize=8, color=INK2, ha="center")
    ax.axvline(2000, color=MUTED, linewidth=1, linestyle="--")
    ax.annotate("stream of new jobs stops", (2000, 20), xytext=(4, 0),
                textcoords="offset points", fontsize=8, color=INK2)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    style(ax, "Experiment 4: CPU received by the long job while short jobs "
              "keep arriving", "time (ms)", "CPU time received (ms)")
    ax.legend(frameon=False, fontsize=8, loc="upper left")
    ax.set_xlim(0, None)
    save(fig, fname)


def fig_quanta(rows, fname):
    """rows: [(label, short_tat, long_tat, dispatches)]"""
    fig, axes = plt.subplots(1, 3, figsize=(11, 2.8), sharey=True)
    labels = [r[0] for r in rows]
    panels = [("short jobs: mean turnaround (ms)", 1),
              ("long jobs: mean turnaround (ms)", 2),
              ("context switches (dispatches)", 3)]
    for ax, (title, k) in zip(axes, panels):
        vals = [r[k] for r in rows]
        ax.barh(range(len(rows)), vals, color=SERIES[0], height=0.6)
        for y, v in enumerate(vals):
            ax.annotate(f"{v:.0f}", (v, y), xytext=(3, 0),
                        textcoords="offset points", va="center",
                        fontsize=8, color=INK2)
        ax.set_yticks(range(len(rows)))
        ax.set_yticklabels(labels)
        ax.invert_yaxis()
        style(ax, title)
        ax.set_xlim(0, max(vals) * 1.25)
    fig.suptitle("Experiment 5: effect of the time quanta on the mixed "
                 "workload", x=0.01, ha="left", color=INK, fontsize=11)
    fig.tight_layout()
    save(fig, fname)


# ------------------------------------------------------------ tables

def job_table(jobs, exp, names, cols):
    """Mean over repetitions for each job, one row per job."""
    rows = []
    for name in names:
        sel = [j for j in jobs if j["exp"] == exp and j["name"] == name]
        if not sel:
            continue
        row = [name]
        for key, conv in cols:
            row.append(conv(mean([j[key] for j in sel])))
        rows.append(row)
    return rows


MSCOL = lambda x: fmt(ms(x))  # noqa: E731
INTCOL = lambda x: f"{x:.0f}"  # noqa: E731


def determinism(data):
    """Largest difference between repetitions of any time metric."""
    worst = 0
    for cfg, (jobs, _, _) in data.items():
        groups = defaultdict(list)
        for j in jobs:
            if j["name"] == "stream":
                continue
            groups[(j["exp"], j["name"])].append(j)
        for g in groups.values():
            for key in ("turnaround", "wait", "response", "cpu"):
                vals = [j[key] for j in g]
                worst = max(worst, max(vals) - min(vals))
    return worst


def summarize(mode, data, out):
    out.append(f"\n## Mode: {mode}\n")
    reps = max((j["rep"] for j in data["mlfq"][0]), default=0)
    out.append(f"Repetitions per experiment: {reps}. Times in ms, mean over "
               f"repetitions.\n")
    if mode == "icount":
        out.append(f"Largest difference between repetitions of any time "
                   f"metric: {determinism(data)} us.\n")

    cols = [("cpu", MSCOL), ("wait", MSCOL), ("response", MSCOL),
            ("turnaround", MSCOL), ("dispatch", INTCOL),
            ("demote", INTCOL), ("q0", MSCOL), ("q1", MSCOL), ("q2", MSCOL)]
    hdr = ["config", "CPU", "wait", "response", "turnaround",
           "dispatches", "demotions", "CPU in Q0", "CPU in Q1", "CPU in Q2"]

    # Experiment 1
    rows = []
    for cfg in ("mlfq", "q2-4-8", "q8-16-32", "rr"):
        if cfg in data:
            r = job_table(data[cfg][0], "single", ["cpu"], cols)
            if r:
                rows.append([CONFIG_LABEL[cfg]] + r[0][1:])
    out.append("\n### Experiment 1: single CPU-bound job (500 ms)\n\n")
    out.append(md_table(hdr, rows))

    # Experiment 2
    out.append("\n### Experiment 2: three CPU-bound jobs (400 ms each)\n\n")
    cols2 = [("response", MSCOL), ("wait", MSCOL), ("turnaround", MSCOL),
             ("end", MSCOL), ("dispatch", INTCOL), ("demote", INTCOL),
             ("promote", INTCOL), ("maxwait", MSCOL)]
    hdr2 = ["job", "response", "wait", "turnaround", "finish time",
            "dispatches", "demotions", "promotions", "longest wait"]
    for cfg in ("mlfq", "rr", "q2-4-8", "q8-16-32"):
        if cfg not in data:
            continue
        jobs = data[cfg][0]
        out.append(f"\n**{CONFIG_LABEL[cfg]}**\n\n")
        rows = job_table(jobs, "multi", ["A", "B", "C"], cols2)
        out.append(md_table(hdr2, rows))
        ends = sorted((mean(v), k) for k, v in
                      per_job(jobs, "multi", "end").items())
        tats = [mean(v) for v in per_job(jobs, "multi",
                                          "turnaround").values()]
        out.append(f"\nCompletion order: {' → '.join(k for _, k in ends)}; "
                   f"mean turnaround {fmt(ms(mean(tats)))} ms; spread of "
                   f"finish times {fmt(ms(ends[-1][0] - ends[0][0]))} ms.\n")

    # Experiment 3
    out.append("\n### Experiment 3: CPU-bound + I/O-bound + short jobs\n\n")
    cols3 = [("arrive", MSCOL), ("cpu", MSCOL), ("response", MSCOL),
             ("wait", MSCOL), ("sleep", MSCOL), ("turnaround", MSCOL),
             ("dispatch", INTCOL), ("prio", INTCOL)]
    hdr3 = ["job", "arrival", "CPU", "response", "wait", "sleep",
            "turnaround", "dispatches", "final level"]
    names3 = ["long1", "long2", "io", "short1", "short2"]
    for cfg in ("mlfq", "rr", "q2-4-8", "q8-16-32"):
        if cfg not in data:
            continue
        jobs, events, _ = data[cfg]
        out.append(f"\n**{CONFIG_LABEL[cfg]}**\n\n")
        out.append(md_table(hdr3, job_table(jobs, "mixed", names3, cols3)))
        lat = wake_latencies(events["mixed"], pid_of(jobs, "mixed", "io"))
        out.append(f"\nI/O job wakeup latency (repetition 1, {len(lat)} "
                   f"wakeups): median {fmt(ms(pct(lat, 50)), 2)} ms, 95th "
                   f"percentile {fmt(ms(pct(lat, 95)), 2)} ms, max "
                   f"{fmt(ms(max(lat)), 2)} ms.\n")

    # Experiment 4
    out.append("\n### Experiment 4: aging (long job vs. a stream of 100 ms "
               "jobs for 2 s)\n\n")
    hdr4 = ["config", "long: turnaround", "long: longest wait",
            "long: promotions", "long: demotions", "stream jobs run",
            "stream: mean turnaround"]
    rows = []
    for cfg in ("aging0", "aging25", "mlfq", "aging100", "rr"):
        if cfg not in data:
            continue
        jobs = data[cfg][0]
        lng = [j for j in jobs if j["exp"] == "aging" and j["name"] == "long"]
        strm = [j for j in jobs if j["exp"] == "aging" and
                j["name"] == "stream"]
        if not lng:
            continue
        nreps = len(lng)
        label = CONFIG_LABEL[cfg] if cfg != "mlfq" else "aging 50 (default)"
        rows.append([label, fmt(ms(mean([j["turnaround"] for j in lng]))),
                     fmt(ms(mean([j["maxwait"] for j in lng]))),
                     fmt(mean([j["promote"] for j in lng]), 1),
                     fmt(mean([j["demote"] for j in lng]), 1),
                     fmt(len(strm) / nreps, 1),
                     fmt(ms(mean([j["turnaround"] for j in strm])))])
    out.append(md_table(hdr4, rows))

    # Experiment 5
    out.append("\n### Experiment 5: time quanta (mixed and multi "
               "workloads)\n\n")
    hdr5 = ["config", "short: mean response", "short: mean turnaround",
            "I/O: turnaround", "I/O: wait", "long: mean turnaround",
            "all jobs: mean wait", "dispatches (mixed)", "demotions (mixed)",
            "multi: mean turnaround", "dispatches (multi)"]
    rows, figrows = [], []
    for cfg in ("q2-4-8", "mlfq", "q8-16-32", "rr"):
        if cfg not in data:
            continue
        jobs = data[cfg][0]
        reps = by_rep(jobs, "mixed")
        mreps = by_rep(jobs, "multi")

        def avg(names, key, reps=reps):
            return mean([j[key] for r in reps.values() for j in r
                         if j["name"] in names])

        def total(key, reps):
            return mean([sum(j[key] for j in r) for r in reps.values()])

        short_tat = avg(["short1", "short2"], "turnaround")
        long_tat = avg(["long1", "long2"], "turnaround")
        disp = total("dispatch", reps)
        rows.append([CONFIG_LABEL[cfg],
                     fmt(ms(avg(["short1", "short2"], "response"))),
                     fmt(ms(short_tat)), fmt(ms(avg(["io"], "turnaround"))),
                     fmt(ms(avg(["io"], "wait"))), fmt(ms(long_tat)),
                     fmt(ms(avg(["long1", "long2", "io", "short1", "short2"],
                                "wait"))),
                     fmt(disp, 0), fmt(total("demote", reps), 0),
                     fmt(ms(avg(["A", "B", "C"], "turnaround", mreps))),
                     fmt(total("dispatch", mreps), 0)])
        figrows.append((CONFIG_LABEL[cfg], ms(short_tat), ms(long_tat),
                        disp))
    out.append(md_table(hdr5, rows))
    return figrows


def robustness(icount, realtime, out):
    """Compare key metrics between the two clock modes."""
    out.append("\n## Robustness: deterministic vs. real-time clock\n\n")
    out.append("Real-time values are mean ± standard deviation over the "
               "repetitions.\n\n")
    hdr = ["config", "experiment", "job", "metric", "icount", "real time"]
    rows = []
    picks = [("mlfq", "multi", "A", "turnaround"),
             ("rr", "multi", "A", "turnaround"),
             ("mlfq", "mixed", "short1", "turnaround"),
             ("rr", "mixed", "short1", "turnaround"),
             ("mlfq", "mixed", "io", "wait"),
             ("rr", "mixed", "io", "wait"),
             ("mlfq", "mixed", "long1", "turnaround"),
             ("rr", "mixed", "long1", "turnaround"),
             ("aging0", "aging", "long", "turnaround"),
             ("mlfq", "aging", "long", "turnaround")]
    for cfg, exp, name, key in picks:
        if cfg not in icount or cfg not in realtime:
            continue
        a = per_job(icount[cfg][0], exp, key).get(name, [])
        b = per_job(realtime[cfg][0], exp, key).get(name, [])
        if not a or not b:
            continue
        rows.append([CONFIG_LABEL.get(cfg, cfg) if cfg != "mlfq"
                     else "MLFQ 4/8/16", exp, name, key,
                     fmt(ms(mean(a))),
                     f"{fmt(ms(mean(b)))} ± {fmt(ms(sd(b)))}"])
    out.append(md_table(hdr, rows))


def write_csv(mode, data):
    rows = []
    for cfg, (jobs, _, _) in data.items():
        for j in jobs:
            rows.append(dict(config=cfg, **j))
    if not rows:
        return
    keys = list(rows[0].keys())
    with open(f"results/jobs_{mode}.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"  wrote results/jobs_{mode}.csv")


def main():
    out = ["# Experiment results\n\nGenerated by scripts/analyze.py from "
           "results/raw/. All times in milliseconds unless stated.\n"]
    icount = load("icount")
    realtime = load("realtime")
    figrows = None
    for mode, data in (("icount", icount), ("realtime", realtime)):
        if "mlfq" not in data:
            continue
        write_csv(mode, data)
        rows = summarize(mode, data, out)
        if mode == "icount":
            figrows = rows
    if icount and realtime:
        robustness(icount, realtime, out)
    with open("results/summary.md", "w") as f:
        f.write("".join(out))
    print("  wrote results/summary.md")

    # Figures from the deterministic runs (repetition 1).
    d = icount or realtime
    fig_single_levels(d, "exp1_single_levels.png")
    fig_gantt(d, "multi", ["A", "B", "C"], "exp2_multi_timeline.png")
    fig_gantt(d, "mixed", ["long1", "long2", "io", "short1", "short2"],
              "exp3_mixed_timeline.png", xmax=1200)
    fig_io_latency(d, "exp3_io_latency.png")
    fig_aging(d, "exp4_aging.png")
    if figrows:
        fig_quanta(figrows, "exp5_quanta.png")


if __name__ == "__main__":
    main()
