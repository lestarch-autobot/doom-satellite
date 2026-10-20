module Components {

  @ Python (fprime-python) bridge from the viewer's keyboard to the engine's key ports. Listens for key datagrams
  @ from doom-satellite-view on a UDP port and invokes keyDownOut / keyUpOut, which DoomCoprocessor wires into the
  @ GenericHub so they arrive at DoomFlight's doom.keyDownIn / keyUpIn.
  passive component DoomKeyBridge {

    @ Key pressed
    output port keyDownOut: Doom.KeyEvent

    @ Key released
    output port keyUpOut: Doom.KeyEvent

    @ A datagram carried a code that is not a Doom.DoomKey enumerator
    event KeyRejected(
                       code: U8 @< Rejected key code
                     ) severity warning low \
      format "Key code {} rejected" \
      throttle 5

    @ Key events forwarded
    telemetry KeysSent: U32

    event port eventOut
    text event port logTextOut
    telemetry port tlmOut
    time get port timeCaller
  }
}
