# Components::DoomKeyBridge

Python (fprime-python) component on DoomCoprocessor. A daemon thread receives `DKEY` datagrams (`>4sBB`: magic,
pressed flag, `Doom.DoomKey` code) from `doom-satellite-view` on `DOOM_KEY_BIND` (default `127.0.0.1:50602`) and
invokes `keyDownOut` / `keyUpOut` (`Doom.KeyEvent`). The topology connects those to `hub.serialIn[0..1]`; DoomFlight
connects `hub.serialOut[0..1]` to `doom.keyDownIn` / `doom.keyUpIn`, so a key reaches the engine's input queue without
going through the command dispatcher. Codes that are not `Doom.DoomKey` enumerators raise `KeyRejected` (throttled).
`KeysSent` counts forwarded events.
