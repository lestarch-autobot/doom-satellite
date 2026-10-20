"""Per-minute soak logger for DoomFlight: rate-group slips, frame counters and FSW event counts from fprime-yamcs.

Writes one CSV row per window (default 60 s) with the deltas of the cumulative counters, the engine state, and
the number of FSW events of each type seen in the window. Lines appended to the optional notes file
(``--notes``) during a window are copied into that row's ``note`` column, so ground-side activity
(viewer on/off, browser on/off, commands) can be correlated with the slip series.
"""

import argparse
import csv
import json
import os
import sys
import time
import urllib.parse
import urllib.request

PROC = "/api/processors/fprime-project/realtime/parameters/DoomFlight_DoomFlight/"
EVENTS = "/api/archive/fprime-project/events?source=FPrimeEventProcessor&limit=1000"
COUNTERS = {
    "doomSlips": "DoomFlight/rateGroupDoom/RgCycleSlips",
    "rg10Slips": "DoomFlight/rateGroup10Hz/RgCycleSlips",
    "rg1Slips": "DoomFlight/rateGroup1Hz/RgCycleSlips",
    "frames": "DoomSubtopology/doom/FrameCount",
    "framesDropped": "DoomSubtopology/doom/FramesDropped",
    "txDrop": "DoomFlight/comDriver/TxDropCount",
    "txBytes": "DoomFlight/comDriver/TxBytes",
    "rxBytes": "DoomFlight/comDriver/RxBytes",
    "rxBackpressure": "DoomFlight/comDriver/RxBackpressureCount",
    "hubFrames": "DoomFlight/frameAdapter/FramesPacked",
    "hubRejected": "DoomFlight/frameAdapter/FramesRejected",
}
GAUGES = {
    "rgMaxTimeMs": "DoomFlight/rateGroupDoom/RgMaxTime",
    "rg1MaxTimeMs": "DoomFlight/rateGroup1Hz/RgMaxTime",
    "rg10MaxTimeMs": "DoomFlight/rateGroup10Hz/RgMaxTime",
    "state": "DoomSubtopology/doom/State",
    "frameRateHz": "DoomSubtopology/doom/FrameRateHz",
}


def get(base, path):
    try:
        with urllib.request.urlopen(base + PROC + path, timeout=5) as resp:
            value = json.load(resp).get("engValue", {})
    except Exception:
        return None
    for key in ("uint64Value", "uint32Value", "sint64Value", "sint32Value", "floatValue", "doubleValue", "stringValue"):
        if key in value:
            return value[key]
    return None


def events_since(base, last_seq):
    try:
        with urllib.request.urlopen(base + EVENTS, timeout=10) as resp:
            events = json.load(resp).get("event", [])
    except Exception:
        return {}, last_seq
    counts = {}
    newest = last_seq
    for event in events:
        seq = int(event.get("seqNumber", 0))
        if last_seq is not None and seq <= last_seq:
            continue
        newest = seq if newest is None else max(newest, seq)
        if last_seq is not None:
            name = event.get("type", "?").split(".")[-1]
            counts[name] = counts.get(name, 0) + 1
    return counts, newest


def read_notes(path, offset):
    if not path or not os.path.exists(path):
        return "", offset
    with open(path, "r", encoding="utf-8") as handle:
        handle.seek(offset)
        text = handle.read()
        return " | ".join(line.strip() for line in text.splitlines() if line.strip()), handle.tell()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--yamcs", default="http://localhost:8090")
    parser.add_argument("--window", type=float, default=60.0, help="seconds per row")
    parser.add_argument("--windows", type=int, default=0, help="number of rows (0 = until interrupted)")
    parser.add_argument("--notes", default=None, help="file whose newly appended lines annotate each row")
    parser.add_argument("csv", help="output CSV path (appended)")
    args = parser.parse_args()

    fields = ["window", "utc", "elapsed_s"] + list(COUNTERS) + list(GAUGES) + ["events", "note"]
    new_file = not os.path.exists(args.csv)
    out = open(args.csv, "a", newline="", encoding="utf-8")
    writer = csv.DictWriter(out, fieldnames=fields)
    if new_file:
        writer.writeheader()
        out.flush()

    prev = {k: get(args.yamcs, v) for k, v in COUNTERS.items()}
    _, last_seq = events_since(args.yamcs, None)
    note_offset = 0
    if args.notes and os.path.exists(args.notes):
        note_offset = os.path.getsize(args.notes)
    start = time.time()
    window = 0
    while args.windows <= 0 or window < args.windows:
        time.sleep(args.window)
        cur = {k: get(args.yamcs, v) for k, v in COUNTERS.items()}
        row = {"window": window, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "elapsed_s": int(time.time() - start)}
        for key in COUNTERS:
            row[key] = (int(cur[key]) - int(prev[key])) if cur[key] is not None and prev[key] is not None else ""
        for key, path in GAUGES.items():
            value = get(args.yamcs, path)
            row[key] = "" if value is None else value
        counts, last_seq = events_since(args.yamcs, last_seq)
        row["events"] = json.dumps(counts, sort_keys=True)
        row["note"], note_offset = read_notes(args.notes, note_offset)
        writer.writerow(row)
        out.flush()
        print(" ".join(f"{k}={row[k]}" for k in ("window", "utc", "doomSlips", "rg10Slips", "rg1Slips", "rgMaxTimeMs", "frames", "framesDropped", "txDrop", "state", "events", "note")), flush=True)
        prev = cur
        window += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
