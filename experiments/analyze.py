#!/usr/bin/env python3
"""
Milestone 3 Sections C.2-C.4: Processes experiment results into tables and graphs.

Usage:
    python3 ../experiments/analyze.py
"""

import argparse
import csv
import os
import statistics
from collections import defaultdict

EXPERIMENT_NAMES = {
    1: "Experiment 1: Moving object scaling (3 clients, 50 static objects)",
    2: "Experiment 2: Client scaling (500 moving, 50 static objects)",
    3: "Experiment 3: Static object scaling, control (3 clients, 100 moving objects)",
}
X_LABELS = {"moving": "Number of moving objects", "clients": "Number of clients", "static": "Number of static objects"}

def mean(values):
    return sum(values) / len(values)

def variance(values):
    # Sample variance (divides by n-1). Needs at least 2 trials.
    return statistics.variance(values) if len(values) > 1 else 0.0

def main():
    parser = argparse.ArgumentParser(description="Analyze Milestone 3 Section C results.")
    parser.add_argument("--in", dest="infile", default="results/raw_results.csv")
    parser.add_argument("--outdir", default="results")
    args = parser.parse_args()

    # Group rows by run (experiment, x, format, trial)
    runs = defaultdict(lambda: defaultdict(list)) # run key -> metric -> values from each client
    vary_of = {}
    with open(args.infile) as f:
        for row in csv.DictReader(f):
            key = (int(row["experiment"]), int(row["x_value"]), row["format"], int(row["trial"]))
            vary_of[int(row["experiment"])] = row["vary"]
            runs[key]["loop"].append(float(row["elapsed_seconds"]))
            runs[key]["bytes"].append(float(row["bytes_per_tick"]))
            runs[key]["encode"].append(float(row["encode_us_per_tick"]))
            runs[key]["decode"].append(float(row["decode_us_per_message"]))

    # Mean and variance over the trials of each condition
    conditions = defaultdict(lambda: defaultdict(list)) # (exp, x, format) -> metric -> one value per trial
    for (exp, x, fmt, trial), metrics in runs.items():
        for name, values in metrics.items():
            conditions[(exp, x, fmt)][name].append(mean(values)) # average over the clients of this run

    os.makedirs(args.outdir, exist_ok=True)
    summary = []
    for (exp, x, fmt), m in sorted(conditions.items()):
        summary.append({
            "experiment": exp, "x_value": x, "format": fmt, "trials": len(m["loop"]),
            "loop_mean_s": mean(m["loop"]), "loop_variance_s2": variance(m["loop"]),
            "loop_stdev_s": variance(m["loop"]) ** 0.5,
            "bytes_per_tick": mean(m["bytes"]),
            "encode_us_per_tick": mean(m["encode"]), "decode_us_per_message": mean(m["decode"]),
        })

    summary_path = os.path.join(args.outdir, "summary.csv")
    with open(summary_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0].keys()))
        writer.writeheader()
        writer.writerows(summary)

    # Print readable tables
    for exp in sorted({s["experiment"] for s in summary}):
        print("\n" + EXPERIMENT_NAMES.get(exp, "Experiment %d" % exp))
        print("%-8s %-7s %6s %12s %14s %10s %12s %12s" %
              (vary_of[exp], "format", "trials", "mean (s)", "variance (s^2)", "bytes/tick", "enc us/tick", "dec us/msg"))
        for s in [s for s in summary if s["experiment"] == exp]:
            print("%-8d %-7s %6d %12.4f %14.2e %10.0f %12.1f %12.1f" %
                  (s["x_value"], s["format"], s["trials"], s["loop_mean_s"], s["loop_variance_s2"],
                   s["bytes_per_tick"], s["encode_us_per_tick"], s["decode_us_per_message"]))
        print("  Speedup (TEXT time / BINARY time):")
        for x in sorted({s["x_value"] for s in summary if s["experiment"] == exp}):
            t = [s for s in summary if s["experiment"] == exp and s["x_value"] == x and s["format"] == "TEXT"]
            b = [s for s in summary if s["experiment"] == exp and s["x_value"] == x and s["format"] == "BINARY"]
            if t and b:
                print("    %s=%-5d  %.2fx" % (vary_of[exp], x, t[0]["loop_mean_s"] / b[0]["loop_mean_s"]))
    print("\nSummary table written to %s" % summary_path)

    # Graphs
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed, so no graphs were drawn. Install it with: pip3 install matplotlib")
        return

    colors = {"TEXT": "tab:red", "BINARY": "tab:blue"}
    for exp in sorted({s["experiment"] for s in summary}):
        rows = [s for s in summary if s["experiment"] == exp]
        xs = sorted({s["x_value"] for s in rows})
        fig, axes = plt.subplots(1, 3, figsize=(15, 4.2))
        fig.suptitle("Figure %d. %s" % (exp, EXPERIMENT_NAMES.get(exp, "")), fontsize=11)

        for fmt in ["TEXT", "BINARY"]:
            r = sorted([s for s in rows if s["format"] == fmt], key=lambda s: s["x_value"])
            x = [s["x_value"] for s in r]
            # Required metric: time for the fixed number of loop iterations, error bar = std deviation
            axes[0].errorbar(x, [s["loop_mean_s"] for s in r], yerr=[s["loop_stdev_s"] for s in r],
                             marker="o", capsize=4, label=fmt, color=colors[fmt])
            # Message size
            axes[1].plot(x, [s["bytes_per_tick"] for s in r], marker="o", label=fmt, color=colors[fmt])
            # Serialization cost: encoding a tick plus decoding one message
            axes[2].plot(x, [s["encode_us_per_tick"] + s["decode_us_per_message"] for s in r],
                         marker="o", label=fmt, color=colors[fmt])

        axes[0].set_title("(a) Time for fixed loop iterations")
        axes[0].set_ylabel("seconds (mean, bars = std dev)")
        axes[1].set_title("(b) Bytes sent per network tick")
        axes[1].set_ylabel("bytes")
        axes[2].set_title("(c) Serialization cost")
        axes[2].set_ylabel("microseconds (encode + decode)")
        axes[2].set_yscale("log")
        for ax in axes:
            ax.set_xlabel(X_LABELS[vary_of[exp]])
            ax.set_xticks(xs)
            ax.grid(alpha=0.3)
            ax.legend()
        fig.tight_layout()
        path = os.path.join(args.outdir, "figure%d_experiment%d.png" % (exp, exp))
        fig.savefig(path, dpi=150)
        plt.close(fig)
        print("Graph written to %s" % path)


if __name__ == "__main__":
    main()