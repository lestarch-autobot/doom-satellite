"""DoomFrameReader.py: Python implementation of Components.DoomFrameReader (fprime-python)

Reads the downsampled DOOM frames unpacked from the hub by Components.FrameBufferAdapter and reports frame statistics
as telemetry. The handlers run on the F Prime thread that invokes the port (the hub receive path), holding the
interpreter lock, so the per-frame work is kept to C-speed operations on the pixel memoryview.

Optionally forwards every frame and palette as UDP datagrams to the viewer (tools/doom-satellite-tools
doom-satellite-view) at the host:port named by the DOOM_FRAME_SINK environment variable, and prints a status line
every STATUS_PERIOD frames so the counters are visible on the console (the coprocessor has no telemetry downlink of
its own).

Also receives the native 320x200 frame that Components.FrameStreamer on DoomFlight streams as raw UDP chunks
(DOOM_STREAM_BIND, default 0.0.0.0:50601, "none" to disable), reassembles each frame by sequence number and byte
offset on a daemon thread, tolerates loss (an incomplete frame is retired when a newer one starts) and forwards the
complete frames and their palettes to the same viewer sink. Counters and the 10 s frame rate go to the console.
"""
import os
import socket
import struct
import sys
import threading
import time

from DoomFrameReaderBaseAc import DoomFrameReaderBase

# BT.601 luma weights, used to turn each palette entry into a 0-255 brightness
LUMA_WEIGHTS = (0.299, 0.587, 0.114)
# Datagram layouts shared with the viewer: magic, frame number, width, height, then the pixels / the RGB palette
FRAME_HEADER = struct.Struct("<4sIHH")
FRAME_MAGIC = b"DFRM"
PALETTE_MAGIC = b"DPAL"
PALETTE_BYTES = 3 * 256
STATUS_PERIOD = 30
# Components.FrameStreamer chunk header (big-endian): magic, frameSeq, width, height, offset, length, kind, flags
CHUNK_HEADER = struct.Struct(">4sIHHIHBB")
CHUNK_MAGIC = b"DSF1"
CHUNK_KIND_PIXELS = 0
CHUNK_KIND_PALETTE = 1
STREAM_DEFAULT_BIND = "0.0.0.0:50601"
STREAM_STATUS_SECONDS = 10.0
STREAM_LATE_WINDOW = 8  # chunks this far behind the current frame are stragglers; further back = sender restarted


class StreamFrame:
    """One native frame being reassembled from FrameStreamer chunks"""

    __slots__ = ("seq", "width", "height", "pixels", "received", "palette")

    def __init__(self, seq, width, height):
        self.seq = seq
        self.width = width
        self.height = height
        self.pixels = bytearray(width * height)
        self.received = 0
        self.palette = None

    def complete(self):
        return self.palette is not None and self.received >= len(self.pixels)


class DoomFrameReader(DoomFrameReaderBase):
    """Reads frames and palettes; see DoomFrameReader.fpp for the interface"""

    def __init__(self):
        super().__init__()
        self.frames_read = 0
        self.palettes_read = 0
        # 256-byte translation table: palette index -> brightness, None until a palette arrives
        self.brightness_table = None
        self.sink = None
        self.sink_address = None
        sink = os.environ.get("DOOM_FRAME_SINK")
        if sink:
            host, _, port = sink.rpartition(":")
            self.sink_address = (host or "127.0.0.1", int(port))
            self.sink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            print(f"frameReader: forwarding frames to udp://{self.sink_address[0]}:{self.sink_address[1]}",
                  flush=True)
        # Native 320x200 stream from Components.FrameStreamer (raw UDP chunks), reassembled on a daemon thread
        self.stream_frames = 0
        self.stream_incomplete = 0
        self.stream_chunks = 0
        self.stream_bytes = 0
        self.stream_last_seq = None
        stream_bind = os.environ.get("DOOM_STREAM_BIND", STREAM_DEFAULT_BIND)
        if stream_bind and stream_bind != "none":
            host, _, port = stream_bind.rpartition(":")
            self.stream_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.stream_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            self.stream_sock.bind((host or "0.0.0.0", int(port)))
            print(f"frameReader: native frame stream on udp://{stream_bind}", flush=True)
            threading.Thread(target=self._stream_loop, name="frameStream", daemon=True).start()

    def _stream_loop(self):
        """Reassemble FrameStreamer chunks by (seq, offset); a newer frame retires an incomplete older one"""
        current = None
        window_start = time.monotonic()
        window_frames = 0
        while True:
            data = self.stream_sock.recv(2048)
            if len(data) < CHUNK_HEADER.size:
                continue
            magic, seq, width, height, offset, length, kind, _flags = CHUNK_HEADER.unpack_from(data)
            payload = data[CHUNK_HEADER.size: CHUNK_HEADER.size + length]
            if magic != CHUNK_MAGIC or len(payload) != length or width == 0 or height == 0:
                continue
            self.stream_chunks += 1
            self.stream_bytes += len(data)
            if current is None or seq != current.seq:
                if current is not None and not current.complete():
                    behind = (current.seq - seq) & 0xFFFFFFFF
                    if 0 < behind < STREAM_LATE_WINDOW:
                        continue  # late chunk of a frame already superseded
                    self.stream_incomplete += 1  # newer frame, or the sender restarted its sequence
                current = StreamFrame(seq, width, height)
            if kind == CHUNK_KIND_PALETTE:
                current.palette = bytes(payload[:PALETTE_BYTES])
            elif kind == CHUNK_KIND_PIXELS and offset + length <= len(current.pixels):
                current.pixels[offset: offset + length] = payload
                current.received += length
            if current.complete():
                self.stream_frames += 1
                window_frames += 1
                self.stream_last_seq = current.seq
                self._forward(FRAME_HEADER.pack(PALETTE_MAGIC, current.seq, 256, 3) + current.palette)
                self._forward(FRAME_HEADER.pack(FRAME_MAGIC, current.seq, current.width, current.height)
                              + bytes(current.pixels))
                current = None
                now = time.monotonic()
                if now - window_start >= STREAM_STATUS_SECONDS:
                    fps = window_frames / (now - window_start)
                    print(f"frameStream: frames={self.stream_frames} incomplete={self.stream_incomplete} "
                          f"chunks={self.stream_chunks} bytes={self.stream_bytes} lastSeq={self.stream_last_seq} "
                          f"{fps:.1f} fps", flush=True)
                    window_start = now
                    window_frames = 0

    def _forward(self, payload):
        """Best-effort datagram to the viewer; a missing viewer is not an error"""
        if self.sink is not None:
            try:
                self.sink.sendto(payload, self.sink_address)
            except OSError:
                pass

    def frameIn_handler(self, portNum, frameNumber, width, height, pixels):
        """Read one frame: width * height palette indices in the caller's buffer, valid only during this call"""
        count = width * height
        size = pixels.getSize()
        if count == 0 or size < count:
            self.log_WARNING_LO_FrameRejected(width, height, size)
            return
        indices = pixels.getData()[:count].tobytes()
        self.frames_read += 1
        mean_pixel = sum(indices) / count
        mean_brightness = None
        self.tlmWrite_FramesRead(self.frames_read)
        self.tlmWrite_LastFrame(frameNumber)
        self.tlmWrite_MeanPixel(mean_pixel)
        if self.brightness_table is not None:
            mean_brightness = sum(indices.translate(self.brightness_table)) / count
            self.tlmWrite_MeanBrightness(mean_brightness)
        self._forward(FRAME_HEADER.pack(FRAME_MAGIC, frameNumber, width, height) + indices)
        if self.frames_read % STATUS_PERIOD == 0:
            brightness = "n/a" if mean_brightness is None else f"{mean_brightness:.1f}"
            print(f"frameReader: FramesRead={self.frames_read} LastFrame={frameNumber} {width}x{height} "
                  f"PalettesRead={self.palettes_read} MeanPixel={mean_pixel:.1f} MeanBrightness={brightness}",
                  flush=True)

    def paletteIn_handler(self, portNum, palette):
        """Rebuild the brightness table from the 256 RGB triples of the palette"""
        rgb = bytes(palette.get_rgb())
        table = bytearray(256)
        for entry in range(256):
            table[entry] = min(
                255,
                int(sum(weight * rgb[3 * entry + channel] for channel, weight in enumerate(LUMA_WEIGHTS))),
            )
        self.brightness_table = bytes(table)
        self.palettes_read += 1
        self.tlmWrite_PalettesRead(self.palettes_read)
        self._forward(FRAME_HEADER.pack(PALETTE_MAGIC, self.palettes_read, 256, 3) + rgb[:PALETTE_BYTES])
