"""Figures for the README, each in a light and a dark version (docs/figures/<name>-{light,dark}.png).

    python3 scripts/plots.py [machine]      (default: laptop; reads results/<machine>/)

Colours: categorical slots checked for colour-vision deficiency on both surfaces, plus a neutral grey for
context; text always uses the ink colours, never a series colour.
"""
from __future__ import annotations

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib import font_manager  # noqa: E402
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle  # noqa: E402

BASE = Path(__file__).resolve().parent.parent
OUT = BASE / "docs" / "figures"
DPI = 200
PX = DPI / 100  # figure pixels per screen pixel: the PNGs are drawn at twice the size they are shown
PT = 72 / DPI   # points per figure pixel

THEMES = {
    "light": {"surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "muted": "#898781", "grid": "#e1e0d9",
              "axis": "#c3c2b7", "fill": "#f0efec", "s1": "#2a78d6", "s2": "#eb6834", "s3": "#1baf7a"},
    "dark": {"surface": "#1a1a19", "ink": "#f0efec", "ink2": "#c3c2b7", "muted": "#898781", "grid": "#2c2c2a",
             "axis": "#383835", "fill": "#2c2c2a", "s1": "#3987e5", "s2": "#d95926", "s3": "#199e70"},
}


def read(machine: str, name: str) -> list[dict]:
    with open(BASE / "results" / machine / name, newline="") as f:
        return list(csv.DictReader(f))


# ---------------------------------------------------------------- drawing helpers

def heading(fig, t: dict, text: str, sub: str) -> None:
    fig.text(0.025, 1 - 0.18 / fig.get_figheight(), text, color=t["ink"], fontsize=12, fontweight="semibold", va="top")
    fig.text(0.025, 1 - 0.50 / fig.get_figheight(), sub, color=t["ink2"], fontsize=8.5, va="top")


def note(fig, t: dict, text: str) -> None:
    fig.text(0.025, 0.1 / fig.get_figheight(), text, color=t["muted"], fontsize=7.5, va="bottom")


def legend_row(fig, t: dict, items: list[tuple[str, str, str]], top: float) -> None:
    """Legend under the subtitle: a key mark (line or square) in the series colour beside ink text."""
    y = 1 - top / fig.get_figheight()
    x = 0.025
    renderer = fig.canvas.get_renderer()
    for color, kind, label in items:
        if kind == "line":
            fig.lines.append(plt.Line2D([x + 0.004, x + 0.022], [y, y], transform=fig.transFigure, color=color,
                                        linewidth=2 * PX * PT, solid_capstyle="round"))
            fig.lines.append(plt.Line2D([x + 0.013], [y], transform=fig.transFigure, marker="o",
                                        markersize=8 * PX * PT, color=color, markeredgecolor=t["surface"],
                                        markeredgewidth=2 * PX * PT))
        else:
            fig.lines.append(plt.Line2D([x + 0.013], [y], transform=fig.transFigure, marker="s", markersize=7,
                                        color=color, markeredgewidth=0, linestyle=""))
        txt = fig.text(x + 0.03, y, label, color=t["ink2"], fontsize=8.5, va="center")
        x += 0.03 + txt.get_window_extent(renderer).width / fig.bbox.width + 0.035


def style_axes(ax, t: dict, grid_axis: str) -> None:
    for side in ("top", "right", "left", "bottom"):
        ax.spines[side].set_visible(False)
    ax.tick_params(colors=t["muted"], labelcolor=t["muted"], length=0, labelsize=8, pad=5)
    ax.grid(axis=grid_axis, color=t["grid"], linewidth=0.75)
    ax.set_axisbelow(True)


def baseline(ax, t: dict, axis: str) -> None:
    side = "bottom" if axis == "x" else "left"
    ax.spines[side].set_visible(True)
    ax.spines[side].set_color(t["axis"])
    ax.spines[side].set_linewidth(0.75)


def px_size(ax) -> tuple[float, float]:
    box = ax.get_window_extent()
    (x0, x1), (y0, y1) = ax.get_xlim(), ax.get_ylim()
    return (x1 - x0) / box.width, abs(y1 - y0) / box.height


def hbar(ax, y: float, value: float, color: str, thick: float = 14) -> None:
    """Horizontal bar from the baseline: square there, 4px rounded at the data end, at most 24px thick."""
    dx, dy = px_size(ax)
    h = min(thick, 24) * PX * dy
    r = min(4 * PX * dx, value / 2)
    ax.add_patch(FancyBboxPatch((0, y - h / 2), value, h, boxstyle=f"round,pad=0,rounding_size={r}",
                                mutation_aspect=dy / dx, facecolor=color, edgecolor="none", zorder=3))
    ax.add_patch(Rectangle((0, y - h / 2), min(value, r), h, facecolor=color, edgecolor="none", zorder=3))


def whisker(ax, y: float, lo: float, hi: float, color: str) -> None:
    ax.plot([lo, hi], [y, y], color=color, linewidth=1.1, solid_capstyle="butt", zorder=4)
    for x in (lo, hi):
        ax.plot([x, x], [y - 0.08, y + 0.08], color=color, linewidth=1.1, zorder=4)


def save(fig, name: str, theme: str) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{name}-{theme}.png"
    fig.savefig(path, dpi=DPI, facecolor=fig.get_facecolor())
    plt.close(fig)
    return path


# ---------------------------------------------------------------- figures

BOOK_VARIANTS = [  # (levels, index, label); the first is the design the feed handler uses
    ("vector", "flat", "Sorted vector + open addressing, huge pages"),
    ("vector", "flat-4k", "Sorted vector + open addressing, 4 KB pages"),
    ("vector", "std", "Sorted vector + std::unordered_map"),
    ("map", "flat", "std::map + open addressing"),
    ("map", "std", "std::map + std::unordered_map"),
]


def fig_book(t: dict, machine: str, cpu: str):
    """Median time per message for each book design, with the range over the rounds."""
    runs = defaultdict(list)
    for r in read(machine, "book.csv"):
        runs[(r["levels"], r["index"])].append(float(r["ns_per_message"]))
    rows = [(label, statistics.median(runs[(lv, ix)]), min(runs[(lv, ix)]), max(runs[(lv, ix)]), i == 0)
            for i, (lv, ix, label) in enumerate(BOOK_VARIANTS) if runs.get((lv, ix))]
    rounds = len(next(iter(runs.values())))
    fig = plt.figure(figsize=(7.2, 3.8), dpi=DPI, facecolor=t["surface"])
    ax = fig.add_axes((0.45, 0.2, 0.47, 0.54), facecolor=t["surface"])
    style_axes(ax, t, "x")
    baseline(ax, t, "y")
    top = max(r[3] for r in rows)
    ax.set_xlim(0, top * 1.18)
    ax.set_ylim(len(rows) - 0.45, -0.55)
    ax.set_yticks(range(len(rows)), [r[0] for r in rows])
    ax.tick_params(axis="y", labelcolor=t["ink2"], labelsize=8.5)
    ax.set_xlabel("ns per message", color=t["muted"], fontsize=7.5, labelpad=4)
    for i, (label, med, lo, hi, chosen) in enumerate(rows):
        hbar(ax, i, med, t["s1"] if chosen else t["muted"])
        whisker(ax, i, lo, hi, t["ink2"])
        ax.text(hi + top * 0.02, i, f"{med:.0f} ns", color=t["ink"], fontsize=8, va="center",
                fontweight="semibold" if chosen else "normal")
    ax.get_yticklabels()[0].set_fontweight("semibold")
    heading(fig, t, "Building order books for every instrument from a real ITCH feed",
            f"40.3M NASDAQ messages (30 Dec 2019, 03:04-10:00); median of {rounds} interleaved rounds, bars show the range")
    note(fig, t, f"One core of an {cpu}, g++ -O2. The time includes reading each message from a memory-mapped file.")
    return fig


def fig_message_latency(t: dict, machine: str):
    """Per-message cost by message type: median and 99th percentile."""
    names = {"A": "Add", "F": "Add (attributed)", "E": "Execute", "C": "Execute at price", "X": "Cancel part",
             "D": "Delete", "U": "Replace", "all": "All messages"}
    rows = [r for r in read(machine, "book_latency.csv") if r["type"] in names]
    rows.sort(key=lambda r: (r["type"] != "all", float(r["p50_ns"])))
    fig = plt.figure(figsize=(7.2, 4.1), dpi=DPI, facecolor=t["surface"])
    ax = fig.add_axes((0.25, 0.18, 0.68, 0.56), facecolor=t["surface"])
    style_axes(ax, t, "x")
    ax.set_xscale("log")
    ax.set_xlim(20, 4000)
    ax.set_xticks([30, 100, 300, 1000, 3000], ["30", "100", "300", "1,000", "3,000"])
    ax.xaxis.set_minor_locator(matplotlib.ticker.NullLocator())
    ax.set_ylim(len(rows) - 0.5, -0.5)
    ax.set_yticks(range(len(rows)), [f"{names[r['type']]} ({int(r['messages']) / 1e6:.2f}M)"
                                     if int(r["messages"]) >= 1e6 else f"{names[r['type']]} ({int(r['messages']) / 1e3:.0f}K)"
                                     for r in rows])
    ax.tick_params(axis="y", labelcolor=t["ink2"], labelsize=8.5)
    ax.set_xlabel("ns per message (log scale)", color=t["muted"], fontsize=7.5, labelpad=4)
    for i, r in enumerate(rows):
        p50, p99 = float(r["p50_ns"]), float(r["p99_ns"])
        ax.plot([p50, p99], [i, i], color=t["axis"], linewidth=2 * PX * PT, zorder=2)
        for x, c in ((p50, t["s1"]), (p99, t["s2"])):
            ax.plot([x], [i], marker="o", markersize=8 * PX * PT, color=c, markeredgecolor=t["surface"],
                    markeredgewidth=2 * PX * PT, zorder=3)
        ax.text(p99 * 1.12, i, f"{p50:.0f} / {p99:.0f}", color=t["ink2"], fontsize=7.5, va="center")
    ax.get_yticklabels()[0].set_fontweight("semibold")
    heading(fig, t, "Time to apply one message, by type",
            "Default books (sorted vector, open addressing, huge pages); each message timed with the TSC")
    legend_row(fig, t, [(t["s1"], "line", "median"), (t["s2"], "line", "99th percentile")], top=0.86)
    note(fig, t, "About 10 ns of each value is the cost of reading the TSC twice.")
    return fig


def fig_queue(t: dict, machine: str):
    """SPSC queue against a mutex-protected ring: throughput and round-trip latency, per CPU pair."""
    rows = read(machine, "queue.csv")
    pairs = list(dict.fromkeys(r["pair"] for r in rows))
    pair_names = {"separate-cores": "Separate cores", "smt-siblings": "SMT siblings (one core)",
                  "same-socket": "Same socket", "cross-socket": "Across sockets"}
    queues = [("spsc", "s1", "Lock-free SPSC queue"), ("mutex", "s2", "Ring under std::mutex")]
    data = {(r["queue"], r["pair"]): r for r in rows}
    fig = plt.figure(figsize=(7.2, 3.6), dpi=DPI, facecolor=t["surface"])
    panels = [("messages_per_s", "Throughput, million items per second", lambda v: v / 1e6, "{:.1f}M"),
              ("rtt_p50_ns", "Round trip, median ns", float, "{:.0f}")]
    for k, (field, title, scale, fmt) in enumerate(panels):
        ax = fig.add_axes((0.20 + k * 0.42, 0.14, 0.33, 0.52), facecolor=t["surface"])
        style_axes(ax, t, "x")
        baseline(ax, t, "y")
        values = [scale(float(data[(q, p)][field])) for p in pairs for q, _, _ in queues if (q, p) in data]
        ax.set_xlim(0, max(values) * 1.32)
        ax.set_ylim(len(pairs) - 0.4, -0.6)
        ax.set_yticks(range(len(pairs)), [pair_names.get(p, p) for p in pairs] if k == 0 else [])
        ax.tick_params(axis="y", labelcolor=t["ink2"], labelsize=8.5)
        ax.set_title(title, color=t["ink"], fontsize=9, loc="left", pad=8)
        ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(4))
        for i, p in enumerate(pairs):
            for j, (q, slot, _) in enumerate(queues):
                if (q, p) not in data:
                    continue
                v = scale(float(data[(q, p)][field]))
                y = i + (j - 0.5) * 0.34
                hbar(ax, y, v, t[slot], thick=11)
                ax.text(v + max(values) * 0.03, y, fmt.format(v), color=t["ink"], fontsize=7.5, va="center")
    heading(fig, t, "Handing messages between two threads",
            "64-byte items, both sides busy-polling; one pair of pinned CPUs per row")
    legend_row(fig, t, [(t[s], "square", label) for _, s, label in queues], top=0.86)
    note(fig, t, "Throughput: 20M items one way. Round trip: an item sent and echoed back, 1M times.")
    return fig


def fig_pipeline(t: dict, machine: str):
    """Packet received to message applied, against the publisher's message rate."""
    setups = [("threads1", "s1", "One thread: receive and apply"),
              ("threads2-core", "s2", "Two threads, separate cores"),
              ("threads2-smt", "s3", "Two threads, SMT siblings")]
    points = defaultdict(list)
    for r in read(machine, "pipeline.csv"):
        setup, rate = r["label"].rsplit("_rate", 1)
        points[setup].append((int(rate), float(r["p50_ns"]) / 1000, float(r["p99_ns"]) / 1000))
    fig = plt.figure(figsize=(7.2, 4.2), dpi=DPI, facecolor=t["surface"])
    for k, (col, title) in enumerate([(1, "Median"), (2, "99th percentile")]):
        ax = fig.add_axes((0.08 + k * 0.48, 0.2, 0.38, 0.48), facecolor=t["surface"])
        style_axes(ax, t, "y")
        baseline(ax, t, "x")
        ax.set_xscale("log")
        ax.set_xticks([1e5, 2.5e5, 5e5, 1e6, 2e6], ["100K", "250K", "500K", "1M", "2M"])
        ax.xaxis.set_minor_locator(matplotlib.ticker.NullLocator())
        ymax = max(p[col] for pts in points.values() for p in pts)
        ax.set_ylim(0, ymax * 1.15)
        ax.set_title(f"{title}, microseconds", color=t["ink"], fontsize=9, loc="left", pad=8)
        ax.set_xlabel("messages per second sent", color=t["muted"], fontsize=7.5, labelpad=4)
        for setup, slot, _ in setups:
            if setup not in points:
                continue
            pts = sorted(points[setup])
            ax.plot([p[0] for p in pts], [p[col] for p in pts], color=t[slot], linewidth=2 * PX * PT,
                    marker="o", markersize=8 * PX * PT, markeredgecolor=t["surface"], markeredgewidth=2 * PX * PT,
                    zorder=3, clip_on=False)
    heading(fig, t, "Latency from packet received to message applied",
            "5M ITCH messages per run over UDP multicast (MoldUDP64) on one host; no message lost in any run")
    legend_row(fig, t, [(t[s], "line", label) for _, s, label in setups], top=0.86)
    note(fig, t, "Measured inside the feed handler with the TSC, from the return of recv() to the end of the book update.")
    return fig


def fig_architecture(t: dict):
    """The feed handler's path, from the multicast packet to the books."""
    fig = plt.figure(figsize=(7.2, 3.0), dpi=DPI, facecolor=t["surface"])
    ax = fig.add_axes((0.01, 0.02, 0.98, 0.76), facecolor=t["surface"])
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 40)
    ax.axis("off")

    def box(x, y, w, h, title, lines, edge=None):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=1.4", mutation_aspect=0.6,
                                    facecolor=t["fill"] if edge is None else t["surface"],
                                    edgecolor=edge or "none", linewidth=2 * PX * PT if edge else 0, zorder=2))
        ax.text(x + w / 2, y + h - 3.2, title, color=t["ink"], fontsize=8.5, fontweight="semibold", ha="center",
                va="center")
        for i, line in enumerate(lines):
            ax.text(x + w / 2, y + h - 7.6 - i * 3.9, line, color=t["ink2"], fontsize=7.2, ha="center", va="center")

    def arrow(x0, x1, y, label=None):
        ax.add_patch(FancyArrowPatch((x0, y), (x1, y), arrowstyle="-|>", mutation_scale=7, color=t["muted"],
                                     linewidth=1.0, zorder=3))
        if label:
            ax.text((x0 + x1) / 2, y + 2.2, label, color=t["muted"], fontsize=6.8, ha="center", va="bottom")

    box(0.5, 9, 17, 22, "Publisher", ["ITCH 5.0 file", "MoldUDP64 packets", "<= 1,400 bytes", "paced rate"])
    arrow(17.8, 23.2, 20, "multicast")
    box(23.5, 9, 20, 22, "Receive thread", ["busy-polls recv()", "sequence numbers:", "gaps, duplicates", "TSC stamp"])
    arrow(43.8, 49.2, 20)
    box(49.5, 9, 15, 22, "SPSC queue", ["64-byte slots", "release/acquire", "no locks"], edge=t["s1"])
    arrow(64.8, 70.2, 20)
    box(70.5, 9, 29, 22, "Book thread", ["order index (ref -> slot)", "order pool, huge pages",
                                          "price levels per side", "FIFO queue per level"])
    ax.text(50, 3.5, "One-thread mode: the receive thread applies each message itself, with no queue.",
            color=t["muted"], fontsize=7.5, ha="center", va="center")
    heading(fig, t, "Feed handler", "From the multicast packet to the order books of every instrument")
    return fig


def main() -> None:
    machine = sys.argv[1] if len(sys.argv) > 1 else "laptop"
    env = {}
    for line in (BASE / "results" / machine / "environment.txt").read_text().splitlines():
        key, _, value = line.partition(": ")
        env[key] = value
    cpu = env.get("cpu", "unknown CPU").replace("(R)", "").replace("(TM)", "").replace(" CPU", "").split(" @")[0]
    names = {f.name for f in font_manager.fontManager.ttflist}
    plt.rcParams["font.family"] = "Inter" if "Inter" in names else "DejaVu Sans"
    figures = [("architecture", fig_architecture), ("book", lambda t: fig_book(t, machine, cpu)),
               ("message-latency", lambda t: fig_message_latency(t, machine)),
               ("queue", lambda t: fig_queue(t, machine)), ("pipeline", lambda t: fig_pipeline(t, machine))]
    for theme, t in THEMES.items():
        for name, make in figures:
            print(save(make(t), name, theme).relative_to(BASE))


if __name__ == "__main__":
    main()
