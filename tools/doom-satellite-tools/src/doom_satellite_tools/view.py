"""doom-satellite-view: draw the downsampled DOOM frames in a window (Tk, standard library only).

Two frame sources:

* ``udp`` (default): the datagrams Components.DoomFrameReader forwards when DoomCoprocessor runs with
  ``--frame-sink host:port`` (``DFRM`` frame and ``DPAL`` palette datagrams, see DoomFrameReader.py).
* ``yamcs``: polls the FrameRow000..FrameRow<rows-1> and PaletteOut channels from a running fprime-yamcs through its
  REST API (``parameters:batchGet``), i.e. the frames that reach the ground over the USB CDC UART.

Example: ``DISPLAY=:1 doom-satellite-view --source yamcs --rows 50 --scale 8``
"""
import argparse
import json
import socket
import struct
import sys
import threading
import time
import tkinter as tk
import urllib.request

FRAME_HEADER = struct.Struct("<4sIHH")
FRAME_MAGIC = b"DFRM"
PALETTE_MAGIC = b"DPAL"
GREY_PALETTE = bytes(sum(([v, v, v] for v in range(256)), []))


class FrameStore:
    """Latest frame and palette, shared between the source thread and the Tk main loop"""

    def __init__(self):
        self.lock = threading.Lock()
        self.frame = None  # (frame_number, width, height, bytes)
        self.palette = GREY_PALETTE
        self.frames = 0
        self.started = time.monotonic()

    def put_frame(self, number, width, height, pixels):
        with self.lock:
            self.frame = (number, width, height, pixels)
            self.frames += 1

    def put_palette(self, rgb):
        with self.lock:
            self.palette = bytes(rgb[: 3 * 256]).ljust(3 * 256, b"\0")

    def snapshot(self):
        with self.lock:
            return self.frame, self.palette, self.frames


def udp_source(store, bind):
    """Receive DoomFrameReader datagrams"""
    host, _, port = bind.rpartition(":")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host or "0.0.0.0", int(port)))
    while True:
        data = sock.recv(65535)
        if len(data) < FRAME_HEADER.size:
            continue
        magic, number, width, height = FRAME_HEADER.unpack_from(data)
        payload = data[FRAME_HEADER.size:]
        if magic == FRAME_MAGIC and len(payload) >= width * height > 0:
            store.put_frame(number, width, height, payload[: width * height])
        elif magic == PALETTE_MAGIC:
            store.put_palette(payload)


def yamcs_value(value):
    """Flatten a YAMCS REST Value into Python (ints, lists, dicts)"""
    kind = value.get("type")
    if kind == "ARRAY":
        return [yamcs_value(item) for item in value.get("arrayValue", [])]
    if kind == "AGGREGATE":
        aggregate = value["aggregateValue"]
        return dict(zip(aggregate["name"], (yamcs_value(item) for item in aggregate["value"])))
    for key in ("uint32Value", "sint32Value", "uint64Value", "sint64Value", "floatValue", "doubleValue",
                "stringValue", "booleanValue"):
        if key in value:
            return value[key]
    return None


def yamcs_source(store, url, instance, processor, prefix, rows, period):
    """Poll FrameRow*/PaletteOut through parameters:batchGet and assemble frames"""
    names = [f"{prefix}/FrameRow{row:03d}" for row in range(rows)] + [f"{prefix}/PaletteOut"]
    request_body = json.dumps({"id": [{"name": name} for name in names]}).encode()
    endpoint = f"{url}/api/processors/{instance}/{processor}/parameters:batchGet"
    last_frame = None
    while True:
        started = time.monotonic()
        try:
            request = urllib.request.Request(endpoint, data=request_body, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=5) as response:
                values = json.load(response).get("value", [])
        except (OSError, ValueError) as error:
            print(f"yamcs: {error}", file=sys.stderr, flush=True)
            time.sleep(1.0)
            continue
        row_values = {}
        for item in values:
            name = item.get("id", {}).get("name", "")
            eng = yamcs_value(item.get("engValue", {}))
            if name.endswith("/PaletteOut") and isinstance(eng, dict) and "rgb" in eng:
                store.put_palette(bytes(int(v) & 0xFF for v in eng["rgb"]))
            elif isinstance(eng, dict) and "pixels" in eng:
                row_values[int(eng["row"])] = (int(eng["frame"]), bytes(int(v) & 0xFF for v in eng["pixels"]))
        if row_values:
            width = len(next(iter(row_values.values()))[1])
            # Rows may straddle two frames while the downlink is mid-frame; draw what is there, tagged with the
            # newest frame number
            number = max(frame for frame, _ in row_values.values())
            pixels = b"".join(row_values.get(row, (0, bytes(width)))[1] for row in range(rows))
            if number != last_frame:
                store.put_frame(number, width, rows, pixels)
                last_frame = number
        time.sleep(max(0.0, period - (time.monotonic() - started)))


class Viewer:
    """Tk window redrawing the latest frame at a fixed rate"""

    def __init__(self, store, scale, title, redraw_hz):
        self.store = store
        self.scale = scale
        self.redraw_ms = max(1, int(1000 / redraw_hz))
        self.root = tk.Tk()
        self.root.title(title)
        self.root.configure(background="black")
        self.label = tk.Label(self.root, background="black")
        self.label.pack()
        self.status = tk.Label(self.root, text="waiting for frames", fg="#d0d0d0", bg="black", font=("monospace", 10))
        self.status.pack(fill="x")
        self.image = None
        self.drawn = None
        self.root.after(self.redraw_ms, self.redraw)

    def redraw(self):
        frame, palette, count = self.store.snapshot()
        if frame is not None and frame[0] != self.drawn:
            number, width, height, pixels = frame
            table = [palette[3 * i: 3 * i + 3] for i in range(256)]
            rgb = b"".join(table[p] for p in pixels)
            ppm = b"P6\n%d %d\n255\n" % (width, height) + rgb
            image = tk.PhotoImage(data=ppm)
            if self.scale > 1:
                image = image.zoom(self.scale, self.scale)
            self.image = image
            self.label.configure(image=image)
            self.drawn = number
            elapsed = max(1e-6, time.monotonic() - self.store.started)
            self.status.configure(text=f"frame {number}  {width}x{height}  {count} frames  {count / elapsed:.1f} fps")
        self.root.after(self.redraw_ms, self.redraw)

    def run(self):
        self.root.mainloop()


def main():
    parser = argparse.ArgumentParser(description="Draw the downsampled DOOM frames in a window")
    parser.add_argument("--source", choices=("udp", "yamcs"), default="udp", help="frame source (default %(default)s)")
    parser.add_argument("--bind", default="127.0.0.1:50600",
                        help="udp: host:port to receive DoomFrameReader datagrams on (default %(default)s)")
    parser.add_argument("--yamcs-url", default="http://localhost:8090", help="yamcs: base URL (default %(default)s)")
    parser.add_argument("--instance", default="fprime-project", help="yamcs: instance (default %(default)s)")
    parser.add_argument("--processor", default="realtime", help="yamcs: processor (default %(default)s)")
    parser.add_argument("--prefix", default="/DoomFlight_DoomFlight/DoomSubtopology/frameTlmProcessor",
                        help="yamcs: parameter prefix of FrameRow*/PaletteOut (default %(default)s)")
    parser.add_argument("--rows", type=int, default=50, help="yamcs: downsampled rows to poll (default %(default)s)")
    parser.add_argument("--period", type=float, default=0.1, help="yamcs: poll period in seconds (default %(default)s)")
    parser.add_argument("--scale", type=int, default=8, help="integer zoom factor (default %(default)s)")
    parser.add_argument("--redraw-hz", type=float, default=30.0, help="window redraw rate (default %(default)s)")
    args = parser.parse_args()

    store = FrameStore()
    if args.source == "udp":
        target = udp_source
        source_args = (store, args.bind)
        title = f"DOOM frames (udp {args.bind})"
    else:
        target = yamcs_source
        source_args = (store, args.yamcs_url.rstrip("/"), args.instance, args.processor, args.prefix, args.rows,
                       args.period)
        title = f"DOOM frames (yamcs {args.yamcs_url})"
    threading.Thread(target=target, args=source_args, daemon=True).start()
    Viewer(store, args.scale, title, args.redraw_hz).run()


if __name__ == "__main__":
    main()
