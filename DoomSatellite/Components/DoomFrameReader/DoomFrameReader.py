"""DoomFrameReader.py: Python implementation of Components.DoomFrameReader (fprime-python)

Reads the downsampled DOOM frames unpacked from the hub by Components.FrameBufferAdapter and reports frame statistics
as telemetry. The handlers run on the F Prime thread that invokes the port (the hub receive path), holding the
interpreter lock, so the per-frame work is kept to C-speed operations on the pixel memoryview.

Optionally forwards every frame and palette as UDP datagrams to the viewer (tools/doom-satellite-tools
doom-satellite-view) at the host:port named by the DOOM_FRAME_SINK environment variable, and prints a status line
every STATUS_PERIOD frames so the counters are visible on the console (the coprocessor has no telemetry downlink of
its own).
"""
import os
import socket
import struct
import sys

from DoomFrameReaderBaseAc import DoomFrameReaderBase

# BT.601 luma weights, used to turn each palette entry into a 0-255 brightness
LUMA_WEIGHTS = (0.299, 0.587, 0.114)
# Datagram layouts shared with the viewer: magic, frame number, width, height, then the pixels / the RGB palette
FRAME_HEADER = struct.Struct("<4sIHH")
FRAME_MAGIC = b"DFRM"
PALETTE_MAGIC = b"DPAL"
PALETTE_BYTES = 3 * 256
STATUS_PERIOD = 30


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
