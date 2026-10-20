"""
Components.DoomKeyBridge: viewer keyboard -> Doom.KeyEvent ports.

Receives ``DKEY`` datagrams (``>4sBB``: magic, pressed, Doom.DoomKey code) from doom-satellite-view on the host:port
named by the DOOM_KEY_BIND environment variable (default 127.0.0.1:50602) and invokes keyDownOut / keyUpOut with the
decoded Doom.DoomKey. The listener is a daemon thread; the port calls run on it and reach DoomFlight through the hub.
"""
import os
import socket
import struct
import threading

from DoomKeyBridgeBaseAc import DoomKeyBridgeBase
import fprime_py

KEY_HEADER = struct.Struct(">4sBB")
KEY_MAGIC = b"DKEY"
DEFAULT_BIND = "127.0.0.1:50602"


class DoomKeyBridge(DoomKeyBridgeBase):
    """Forward viewer key datagrams as Doom.KeyEvent port calls"""

    def __init__(self):
        super().__init__()
        self.keys_sent = 0
        self.codes = {int(member.value): member for member in fprime_py.Doom.DoomKey.T.__members__.values()}
        bind = os.environ.get("DOOM_KEY_BIND", DEFAULT_BIND)
        host, _, port = bind.rpartition(":")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((host or "127.0.0.1", int(port)))
        print(f"keyBridge: listening for DKEY datagrams on {bind}", flush=True)
        threading.Thread(target=self._listen, name="keyBridge", daemon=True).start()

    def _listen(self):
        while True:
            data = self.sock.recv(64)
            if len(data) < KEY_HEADER.size:
                continue
            magic, pressed, code = KEY_HEADER.unpack_from(data)
            if magic != KEY_MAGIC:
                continue
            member = self.codes.get(code)
            if member is None:
                self.log_WARNING_LO_KeyRejected(code)
                continue
            key = fprime_py.Doom.DoomKey(member)
            if pressed:
                self.keyDownOut_out(0, key)
            else:
                self.keyUpOut_out(0, key)
            self.keys_sent += 1
            self.tlmWrite_KeysSent(self.keys_sent)
