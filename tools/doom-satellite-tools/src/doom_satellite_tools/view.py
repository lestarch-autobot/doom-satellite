"""doom-satellite-view: draw the DOOM frames in a window (Tk; numpy speeds up the full-resolution path when present).

Two frame sources:

* ``udp`` (default): the datagrams Components.DoomFrameReader forwards when DoomCoprocessor runs with
  ``--frame-sink host:port`` (``DFRM`` frame and ``DPAL`` palette datagrams, see DoomFrameReader.py). Both the
  downsampled hub frames and the native 320x200 frames reassembled from Components.FrameStreamer arrive here; while
  native frames are flowing the smaller ones are ignored.
* ``yamcs``: polls the FrameRow000..FrameRow<rows-1> and PaletteOut channels from a running fprime-yamcs through its
  REST API (``parameters:batchGet``), i.e. the frames that reach the ground over the USB CDC UART.

Keyboard: W/A/S/D and the arrows move and turn, Space uses, Ctrl fires, Shift runs, Tab toggles the automap, Esc and
Enter drive the menu, Y/N answer prompts, 1-7 select weapons. Presses and releases go to Components.DoomKeyBridge on
DoomCoprocessor as ``DKEY`` datagrams (``--keys host:port``), which forwards them over the hub to the engine's
keyDownIn / keyUpIn ports.

Example: ``DISPLAY=:1 doom-satellite-view --scale 3 --keys 127.0.0.1:50602``
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

try:
    import numpy as np
except ImportError:  # the 80x50 path works without it
    np = None

FRAME_HEADER = struct.Struct("<4sIHH")
FRAME_MAGIC = b"DFRM"
PALETTE_MAGIC = b"DPAL"
GREY_PALETTE = bytes(sum(([v, v, v] for v in range(256)), []))
KEY_HEADER = struct.Struct(">4sBB")
KEY_MAGIC = b"DKEY"
# Larger (native) frames take precedence over smaller (downsampled) ones for this long after the last one
PREFER_LARGE_SECONDS = 2.0
FPS_WINDOW_SECONDS = 60.0

# Tk keysym -> Doom.DoomKey code (lib/fprime-stress/Doom/Doom.fpp)
DOOM_KEYS = {
    "Up": 0xAD, "w": 0xAD, "W": 0xAD,
    "Down": 0xAF, "s": 0xAF, "S": 0xAF,
    "Left": 0xAC, "Right": 0xAE,
    "a": 0xA0, "A": 0xA0, "d": 0xA1, "D": 0xA1,
    "space": 0xA2,
    "Control_L": 0xA3, "Control_R": 0xA3,
    "Shift_L": 0xB6, "Shift_R": 0xB6,
    "Escape": 0x1B, "Return": 0x0D, "KP_Enter": 0x0D, "Tab": 0x09,
    "y": 0x79, "Y": 0x79, "n": 0x6E, "N": 0x6E,
    "1": 0x31, "2": 0x32, "3": 0x33, "4": 0x34, "5": 0x35, "6": 0x36, "7": 0x37,
    "Pause": 0xFF,
}


class FrameStore:
    """Latest frame and palette, shared between the source thread and the Tk main loop"""

    def __init__(self):
        self.lock = threading.Lock()
        self.frame = None  # (frame_number, width, height, bytes)
        self.palette = GREY_PALETTE
        self.frames = 0
        self.ignored = 0
        self.started = time.monotonic()
        self.last_large = (0, 0.0)  # (pixel count, monotonic time) of the last frame accepted
        self.arrivals = []  # monotonic arrival times of accepted frames, FPS_WINDOW_SECONDS deep

    def put_frame(self, number, width, height, pixels):
        now = time.monotonic()
        with self.lock:
            count = width * height
            large, when = self.last_large
            if count < large and (now - when) < PREFER_LARGE_SECONDS:
                self.ignored += 1
                return
            self.last_large = (count, now)
            self.frame = (number, width, height, pixels)
            self.frames += 1
            self.arrivals.append(now)
            cutoff = now - FPS_WINDOW_SECONDS
            while self.arrivals and self.arrivals[0] < cutoff:
                self.arrivals.pop(0)

    def window_fps(self):
        """Accepted frames per second over the last FPS_WINDOW_SECONDS (or since the first frame if younger)"""
        with self.lock:
            if len(self.arrivals) < 2:
                return 0.0
            span = max(1e-6, min(FPS_WINDOW_SECONDS, time.monotonic() - self.arrivals[0]))
            return len(self.arrivals) / span

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


class KeySender:
    """Tk key events -> DKEY datagrams, with X autorepeat filtered out (a repeat is a release+press pair in one tick)"""

    RELEASE_DELAY_MS = 25

    def __init__(self, root, target):
        host, _, port = target.rpartition(":")
        self.address = (host or "127.0.0.1", int(port))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.root = root
        self.down = set()
        self.pending_release = {}
        self.sent = 0
        root.bind("<KeyPress>", self.on_press)
        root.bind("<KeyRelease>", self.on_release)
        root.bind("<FocusOut>", self.release_all)

    def send(self, pressed, code):
        try:
            self.sock.sendto(KEY_HEADER.pack(KEY_MAGIC, 1 if pressed else 0, code), self.address)
            self.sent += 1
        except OSError:
            pass

    def on_press(self, event):
        code = DOOM_KEYS.get(event.keysym)
        if code is None:
            return
        timer = self.pending_release.pop(code, None)
        if timer is not None:
            self.root.after_cancel(timer)  # autorepeat: the release never happened
        if code not in self.down:
            self.down.add(code)
            self.send(True, code)

    def on_release(self, event):
        code = DOOM_KEYS.get(event.keysym)
        if code is None or code in self.pending_release:
            return
        self.pending_release[code] = self.root.after(self.RELEASE_DELAY_MS, lambda: self.release(code))

    def release(self, code):
        self.pending_release.pop(code, None)
        if code in self.down:
            self.down.discard(code)
            self.send(False, code)

    def release_all(self, _event=None):
        for code in list(self.down):
            self.release(code)


def render_ppm(width, height, pixels, palette, scale):
    """Palette-indexed pixels -> binary PPM, already scaled (numpy) or unscaled (pure Python, caller zooms)"""
    if np is not None:
        lut = np.frombuffer(palette, dtype=np.uint8).reshape(256, 3)
        rgb = lut[np.frombuffer(pixels, dtype=np.uint8)].reshape(height, width, 3)
        if scale > 1:
            rgb = rgb.repeat(scale, axis=0).repeat(scale, axis=1)
        return b"P6\n%d %d\n255\n" % (width * scale, height * scale) + rgb.tobytes(), True
    table = [palette[3 * i: 3 * i + 3] for i in range(256)]
    return b"P6\n%d %d\n255\n" % (width, height) + b"".join(table[p] for p in pixels), False


class Viewer:
    """Tk window redrawing the latest frame at a fixed rate"""

    def __init__(self, store, scale, title, redraw_hz, keys=None):
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
        self.drawn_count = 0
        self.keys = KeySender(self.root, keys) if keys else None
        self.last_report = time.monotonic()
        self.root.after(self.redraw_ms, self.redraw)

    def frame_scale(self, width):
        """Explicit --scale, else fit roughly 960 pixels wide (3x for 320, 12x for 80)"""
        if self.scale > 0:
            return self.scale
        return max(1, 960 // max(1, width))

    def redraw(self):
        frame, palette, count = self.store.snapshot()
        if frame is not None and frame[0] != self.drawn:
            number, width, height, pixels = frame
            scale = self.frame_scale(width)
            ppm, scaled = render_ppm(width, height, pixels, palette, scale)
            image = tk.PhotoImage(data=ppm)
            if scale > 1 and not scaled:
                image = image.zoom(scale, scale)
            self.image = image
            self.label.configure(image=image)
            self.drawn = number
            self.drawn_count += 1
            fps = self.store.window_fps()
            keys = f"  keys {self.keys.sent}" if self.keys else ""
            self.status.configure(text=f"frame {number}  {width}x{height}  {count} frames  {fps:.1f} fps (60 s){keys}")
            now = time.monotonic()
            if now - self.last_report >= 10.0:
                print(f"viewer: frames={count} drawn={self.drawn_count} ignored={self.store.ignored} "
                      f"{width}x{height} {fps:.1f} fps over 60 s", flush=True)
                self.last_report = now
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
    parser.add_argument("--scale", type=int, default=0,
                        help="integer zoom factor (default 0 = fit ~960 px wide: 3x for 320x200, 12x for 80x50)")
    parser.add_argument("--keys", default="127.0.0.1:50602",
                        help="host:port of DoomCoprocessor's keyBridge for keyboard input ('' disables; "
                             "default %(default)s)")
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
    Viewer(store, args.scale, title, args.redraw_hz, args.keys).run()


if __name__ == "__main__":
    main()
