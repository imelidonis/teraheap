#!/usr/bin/env python3
import argparse
import re
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

BYTES_TO_GB = 1024 ** 3

# ---------- Regex ----------
AVG_RE = re.compile(
    r"Avg\s*GC\s*Time:\s*([0-9]*\.?[0-9]+)\s*ms,\s*Avg\s*IO\s*Time:\s*([0-9]*\.?[0-9]+)\s*ms",
    re.IGNORECASE,
)
TS_RE = re.compile(r"at\s+timestamp\s*=\s*([0-9]*\.?[0-9]+)", re.IGNORECASE)
SHRINK_RE = re.compile(
    r"Current\s+capacity:\s*([0-9.]+)\s*GB,\s*Used:\s*([0-9.]+)\s*GB",
    re.IGNORECASE,
)
GROW_RE = re.compile(
    r"new\s+size\s+is\s*([0-9.]+)\s*GB.*?Used:\s*([0-9.]+)",
    re.IGNORECASE,
)

# ---------- PARSERS ----------
def parse_log(path: Path):
    """Parse tmp.err for heap usage and Avg GC/IO times."""
    heap_pts = []
    avg_pts = []
    last_ts = 0.0

    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            m_ts = TS_RE.search(line)
            if m_ts:
                try:
                    last_ts = float(m_ts.group(1))
                except ValueError:
                    continue

            m_avg = AVG_RE.search(line)
            if m_avg:
                gc, io = float(m_avg.group(1)), float(m_avg.group(2))
                avg_pts.append((last_ts, gc, io))
                continue

            m_shrink = SHRINK_RE.search(line)
            if m_shrink:
                curr, used = map(float, m_shrink.groups())
                heap_pts.append((last_ts, curr, used))
                continue

            m_grow = GROW_RE.search(line)
            if m_grow:
                curr, used = map(float, m_grow.groups())
                heap_pts.append((last_ts, curr, used))
                continue

    heap_pts.sort(key=lambda x: x[0])
    avg_pts.sort(key=lambda x: x[0])
    return heap_pts, avg_pts


def parse_mem_usage(path: Path):
    """Parse cgroup_mem_usage.csv into (timestamp, file_bytes_GB)."""
    pts = []
    first_ts = None
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        f.readline()  # skip header
        for line in f:
            vals = line.strip().split(",")
            if len(vals) < 3:
                continue
            ts = float(vals[0])
            fb = int(vals[2])
            if first_ts is None:
                first_ts = ts
            ts -= first_ts
            pts.append((ts, fb / BYTES_TO_GB))
    return pts


# ---------- PLOTS ----------
def plot_heap_pagecache(heap_pts, mem_pts, out_path):
    plt.figure(figsize=(12, 6))
    plt.title("Heap and Page Cache Usage Over Time")
    plt.xlabel("Timestamp (s)")
    plt.ylabel("Memory (GB)")
    plt.grid(True, linestyle="--", alpha=0.4)

    if heap_pts:
        ts = [p[0] for p in heap_pts]
        cap = [p[1] for p in heap_pts]
        used = [p[2] for p in heap_pts]
        plt.plot(ts, cap, "--", color="purple", linewidth=2, label="Heap Capacity (GB)")
        plt.plot(ts, used, "-", color="tab:blue", linewidth=2, label="Heap Used (GB)")

    if mem_pts:
        ts_mem = [p[0] for p in mem_pts]
        cache = [p[1] for p in mem_pts]
        plt.plot(ts_mem, cache, "-", color="tab:red", linewidth=2, label="Page Cache (GB)", alpha=0.8)

    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    print(f"Saved: {out_path}")

def plot_gc_vs_io(avg_pts, out_path):
    """GC vs IO over time — shared X-axis, both Y-axes increasing upward."""
    if not avg_pts:
        print("No GC/IO data to plot.")
        return

    xs = [p[0] for p in avg_pts]
    gc = [p[1] for p in avg_pts]
    io = [p[2] for p in avg_pts]

    fig, ax1 = plt.subplots(figsize=(14, 7))
    plt.title("GC and IO Over Time (Shared Axis, Upward)")

    # --- GC Time (Left Axis) ---
    ax1.plot(xs, gc, color="green", marker="o", markersize=5, linewidth=2, label="GC Time (ms)")
    ax1.set_ylabel("GC Time (ms)", color="green")
    ax1.tick_params(axis="y", labelcolor="green")
    ax1.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))

    # --- IO Time (Right Axis, not inverted) ---
    ax2 = ax1.twinx()
    ax2.plot(xs, io, color="red", marker="s", markersize=5, linewidth=2, label="IO Wait (ms)")
    ax2.set_ylabel("IO Wait Time (ms)", color="red")
    ax2.tick_params(axis="y", labelcolor="red")
    ax2.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))

    # Make sure IO axis is **not inverted**
    try:
        ax2.invert_yaxis(False)
    except Exception:
        pass

    ax1.set_xlabel("Timestamp (s)")
    ax1.grid(True, linestyle="--", alpha=0.4)

    # Combine legends
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right")

    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    print(f"Saved: {out_path}")


# ---------- MAIN ----------
def main():
    ap = argparse.ArgumentParser(description="Plot Heap/PageCache and GC vs IO (shared axis, non-mirrored).")
    ap.add_argument("logfile", type=Path, help="Path to tmp.err log file.")
    ap.add_argument("memfile", type=Path, help="Path to cgroup_mem_usage.csv.")
    ap.add_argument("--out-dir", type=Path, default=Path("./plots"))
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)

    heap_pts, avg_pts = parse_log(args.logfile)
    mem_pts = parse_mem_usage(args.memfile)

    if not heap_pts and not avg_pts:
        print("No data found.")
        return

    plot_heap_pagecache(heap_pts, mem_pts, args.out_dir / "heap_and_pagecache.png")
    plot_gc_vs_io(avg_pts, args.out_dir / "gc_vs_io.png")


if __name__ == "__main__":
    main()


