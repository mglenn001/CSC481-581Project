#!/usr/bin/env python3
"""
Milestone 3 Sections C.2-C.4: Runs the performance experiments.

Usage examples:
    python3 ../experiments/run_experiments.py                  # full run, all 3 experiments
    python3 ../experiments/run_experiments.py --quick          # short test run (a minute or two)
    python3 ../experiments/run_experiments.py --experiments 1  # only experiment 1
"""

import argparse
import csv
import os
import subprocess
import sys
import time

# Defines the 3 experiments. Each changes one factor while keeping others fixed.
EXPERIMENTS = {
    1: {
        "name": "Moving object scaling",
        "vary": "moving",
        "values": [10, 100, 500, 1000],
        "fixed": {"clients": 3, "static": 50},
    },
    2: {
        "name": "Client scaling",
        "vary": "clients",
        "values": [1, 2, 3, 4],
        "fixed": {"moving": 500, "static": 50},
    },
    3: {
        # Control experiment (static objects are not sent over the network)
        "name": "Static object scaling (control)",
        "vary": "static",
        "values": [10, 100, 500, 1000],
        "fixed": {"clients": 3, "moving": 100},
    },
}

FORMATS = ["text", "binary"]

CSV_COLUMNS = [
    "experiment", "vary", "x_value", "clients", "moving", "static", "format", "trial",
    "client_id", "iterations", "elapsed_seconds", "network_ticks",
    "bytes_per_tick", "encode_us_per_tick", "decode_us_per_message",
]

def run_once(binary, clients, fmt, moving, static, iterations, tick_us, startup_ms, timeout):
    """Starts benchmark processes together and returns results."""
    start_epoch_ms = int(time.time() * 1000) + startup_ms # everyone starts at this time
    processes = []
    for client_id in range(1, clients + 1):
        command = [binary, str(client_id), str(clients), fmt, str(moving), str(static),
                   str(iterations), str(start_epoch_ms), str(tick_us)]
        processes.append(subprocess.Popen(command, stdout=subprocess.PIPE,
                                          stderr=subprocess.DEVNULL, text=True))

    results = []
    for process in processes:
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            continue
        for line in output.splitlines():
            if line.startswith("RESULT,"):
                f = line.split(",")
                results.append({
                    "client_id": int(f[1]), "iterations": int(f[6]),
                    "elapsed_seconds": float(f[7]), "network_ticks": int(f[8]),
                    "bytes_per_tick": float(f[9]), "encode_us_per_tick": float(f[10]),
                    "decode_us_per_message": float(f[11]),
                })
    return results


def main():
    parser = argparse.ArgumentParser(description="Run the Milestone 3 Section C experiments.")
    parser.add_argument("--binary", default="./benchmark", help="path to the built benchmark program")
    parser.add_argument("--experiments", type=int, nargs="+", default=[1, 2, 3], help="which experiments to run")
    parser.add_argument("--iterations", type=int, default=100000, help="game loop iterations per run")
    parser.add_argument("--trials", type=int, default=5, help="repeats per condition and format")
    parser.add_argument("--tick-us", type=int, default=1000, help="network tick in microseconds")
    parser.add_argument("--startup-ms", type=int, default=2000, help="time given for processes to start")
    parser.add_argument("--out", default="results/raw_results.csv", help="CSV file to write")
    parser.add_argument("--quick", action="store_true", help="short test run (10,000 iterations, 2 trials)")
    args = parser.parse_args()

    if args.quick:
        args.iterations = 10000
        args.trials = 2

    if not os.path.exists(args.binary):
        sys.exit("Cannot find '%s'. Build the project first (see README)." % args.binary)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    out_file = open(args.out, "w", newline="")
    writer = csv.DictWriter(out_file, fieldnames=CSV_COLUMNS)
    writer.writeheader()

    # Count runs so we can show progress
    total_runs = sum(len(EXPERIMENTS[e]["values"]) for e in args.experiments) * len(FORMATS) * args.trials
    done = 0
    started = time.time()

    # Warm-up run to stabilize CPU and cache performance
    print("Warm-up run (not recorded)...")
    run_once(args.binary, 2, "text", 100, 10, 5000, args.tick_us, args.startup_ms, 120)
    time.sleep(0.5)

    for exp_id in args.experiments:
        exp = EXPERIMENTS[exp_id]
        for value in exp["values"]:
            # Build the settings for this condition
            settings = dict(exp["fixed"])
            settings[exp["vary"]] = value
            clients, moving, static = settings["clients"], settings["moving"], settings["static"]

            for trial in range(1, args.trials + 1):
                # Alternate format order to prevent bias
                order = FORMATS if trial % 2 == 1 else list(reversed(FORMATS))
                for fmt in order:
                    results = run_once(args.binary, clients, fmt, moving, static,
                                       args.iterations, args.tick_us, args.startup_ms, 300)
                    # Retry once if a client fails
                    if len(results) != clients:
                        print("  (run incomplete, retrying)")
                        time.sleep(1.0)
                        results = run_once(args.binary, clients, fmt, moving, static,
                                           args.iterations, args.tick_us, args.startup_ms, 300)
                    for r in results:
                        row = {"experiment": exp_id, "vary": exp["vary"], "x_value": value,
                               "clients": clients, "moving": moving, "static": static,
                               "format": fmt.upper(), "trial": trial}
                        row.update(r)
                        writer.writerow(row)
                    out_file.flush()

                    done += 1
                    mean_time = (sum(r["elapsed_seconds"] for r in results) / len(results)) if results else float("nan")
                    print("[%3d/%d] exp %d | %s=%-5s | %-6s | trial %d | avg loop time %.3f s"
                          % (done, total_runs, exp_id, exp["vary"], value, fmt, trial, mean_time))
                    time.sleep(0.7) # let the network ports close before the next run

    out_file.close()
    print("\nFinished in %.1f minutes. Raw results: %s" % ((time.time() - started) / 60, args.out))
    print("Next: python3 analyze.py --in %s" % args.out)


if __name__ == "__main__":
    main()