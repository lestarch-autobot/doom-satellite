"""doom-satellite-hub-bridge: feed DoomCoprocessor's GenericHub from the UART telemetry stream.

The Teensy's Ethernet (the intended Svc.GenericHub UDP link to the coprocessor) is not always wired. This bridge
re-creates the hub frame feed from the frames that do reach the ground over the USB CDC UART: it polls the
FrameRow000..FrameRow<rows-1> and PaletteOut channels from fprime-yamcs (REST ``parameters:batchGet``), packs each
complete frame and palette exactly as ``Components.FrameBufferAdapter`` does on DoomFlight (kind byte, header,
pixels / serialized Doom.Palette) and sends them as ``Svc.GenericHub`` HUB_TYPE_BUFFER datagrams to the coprocessor's
``hubComDriver`` port, where ``hub.bufferOut[HUB_FRAME_PORT] -> frameAdapter.packedIn -> frameReader`` consumes them.

Wire format (F Prime big-endian serialization):
    U32 HubType (1 = HUB_TYPE_BUFFER), U32 port, FwBuffSizeType size (U64 on the native coprocessor), then the packed buffer
    frame:   U8 kind=0, U32 frameNumber, U16 width, U16 height, width*height U8 pixels
    palette: U8 kind=1, U32 generation, 768 U8 rgb

Example: ``doom-satellite-hub-bridge --rows 50 --target 127.0.0.1:50555``
"""
import argparse
import json
import socket
import struct
import sys
import time
import urllib.request

from doom_satellite_tools.view import yamcs_value

HUB_TYPE_BUFFER = 1
HUB_HEADER = struct.Struct(">IIQ")
FRAME_HEADER = struct.Struct(">BIHH")
PALETTE_HEADER = struct.Struct(">BI")
KIND_FRAME = 0
KIND_PALETTE = 1
PALETTE_BYTES = 3 * 256


def hub_datagram(port, packed):
    """Wrap a packed FrameBufferAdapter buffer as a GenericHub buffer transmission"""
    return HUB_HEADER.pack(HUB_TYPE_BUFFER, port, len(packed)) + packed


def pack_frame(number, width, height, pixels):
    return FRAME_HEADER.pack(KIND_FRAME, number, width, height) + pixels


def pack_palette(generation, rgb):
    return PALETTE_HEADER.pack(KIND_PALETTE, generation) + bytes(rgb[:PALETTE_BYTES]).ljust(PALETTE_BYTES, b"\0")


def poll(endpoint, request_body):
    request = urllib.request.Request(endpoint, data=request_body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response).get("value", [])


def main():
    parser = argparse.ArgumentParser(description="Bridge UART-downlinked DOOM frames into DoomCoprocessor's hub")
    parser.add_argument("--target", default="127.0.0.1:50555",
                        help="DoomCoprocessor hubComDriver host:port (default %(default)s)")
    parser.add_argument("--hub-port", type=int, default=0, help="GenericHub buffer port index (default %(default)s)")
    parser.add_argument("--yamcs-url", default="http://localhost:8090", help="YAMCS base URL (default %(default)s)")
    parser.add_argument("--instance", default="fprime-project", help="YAMCS instance (default %(default)s)")
    parser.add_argument("--processor", default="realtime", help="YAMCS processor (default %(default)s)")
    parser.add_argument("--prefix", default="/DoomFlight_DoomFlight/DoomSubtopology/frameTlmProcessor",
                        help="parameter prefix of FrameRow*/PaletteOut (default %(default)s)")
    parser.add_argument("--rows", type=int, default=50, help="downsampled rows downlinked (default %(default)s)")
    parser.add_argument("--period", type=float, default=0.05, help="poll period in seconds (default %(default)s)")
    parser.add_argument("--status-every", type=float, default=10.0, help="status line period in seconds")
    args = parser.parse_args()

    host, _, port = args.target.rpartition(":")
    target = (host or "127.0.0.1", int(port))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    names = [f"{args.prefix}/FrameRow{row:03d}" for row in range(args.rows)] + [f"{args.prefix}/PaletteOut"]
    request_body = json.dumps({"id": [{"name": name} for name in names]}).encode()
    endpoint = f"{args.yamcs_url.rstrip('/')}/api/processors/{args.instance}/{args.processor}/parameters:batchGet"

    last_frame = None
    last_generation = None
    sent_frames = sent_palettes = 0
    polls = errors = 0
    status_at = time.monotonic()
    print(f"hub-bridge: {endpoint} -> udp://{target[0]}:{target[1]} hub port {args.hub_port}", flush=True)
    while True:
        started = time.monotonic()
        try:
            values = poll(endpoint, request_body)
            polls += 1
        except (OSError, ValueError) as error:
            errors += 1
            print(f"hub-bridge: yamcs error: {error}", file=sys.stderr, flush=True)
            time.sleep(1.0)
            continue
        rows = {}
        palette = None
        for item in values:
            eng = yamcs_value(item.get("engValue", {}))
            if not isinstance(eng, dict):
                continue
            if "rgb" in eng:
                palette = eng
            elif "pixels" in eng:
                rows[int(eng["row"])] = (int(eng["frame"]), bytes(int(v) & 0xFF for v in eng["pixels"]))
        if palette is not None and palette.get("generation") != last_generation:
            last_generation = palette.get("generation")
            sock.sendto(hub_datagram(args.hub_port, pack_palette(int(last_generation), palette["rgb"])), target)
            sent_palettes += 1
        if len(rows) == args.rows:
            number = max(frame for frame, _ in rows.values())
            if number != last_frame:
                width = len(rows[0][1])
                pixels = b"".join(rows[row][1] for row in range(args.rows))
                sock.sendto(hub_datagram(args.hub_port, pack_frame(number, width, args.rows, pixels)), target)
                sent_frames += 1
                last_frame = number
        now = time.monotonic()
        if now - status_at >= args.status_every:
            status_at = now
            print(f"hub-bridge: polls={polls} errors={errors} frames_sent={sent_frames} palettes_sent={sent_palettes} "
                  f"last_frame={last_frame}", flush=True)
        time.sleep(max(0.0, args.period - (time.monotonic() - started)))


if __name__ == "__main__":
    main()
