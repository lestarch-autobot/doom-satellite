module Components {

  @ Streams the native DOOM frame (320x200 palette indices plus the 256-entry palette) to a UDP peer from its own
  @ thread. The engine thread copies each frame into one of two slots (capture hook); this thread sends the newest
  @ slot as self-describing chunks of at most 1472 bytes. A frame captured while the previous one is still being
  @ sent replaces it (latest wins), so the stream never queues up behind the link.
  active component FrameStreamer {

    @ One kick per captured frame, dropped when the queue is full (the slots already hold the newest frame)
    internal port deliver drop

    @ Start or stop the stream (the deployment sets the boot default)
    sync command ENABLE(
                        enable: Fw.Enabled @< ENABLED streams, DISABLED stops capturing and sending
                      )

    @ Send every divisor-th engine frame: 1 streams at the engine rate (30 Hz), 2 at half, ...
    sync command SET_RATE(
                          divisor: U8 @< 1..255
                        )

    @ Peer that receives the chunks; the socket is reopened before the next frame
    sync command SET_PEER(
                          address: string size 16 @< IPv4 dotted address
                          peerPort: U16 @< UDP port
                        )

    @ Stream configuration after ENABLE / SET_RATE / SET_PEER
    event StreamState(
                       enable: Fw.Enabled @< Stream state
                       divisor: U8 @< Frame divisor
                       address: string size 16 @< Peer address
                       peerPort: U16 @< Peer port
                     ) severity activity high \
      format "Frame stream {}: every {} frames to {}:{}"

    @ A divisor of 0 was rejected
    event RateRejected(
                        divisor: U8 @< Requested divisor
                      ) severity warning low \
      format "Frame stream divisor {} rejected (1..255)"

    @ The stream socket could not be opened
    event SocketFailed(
                        status: I32 @< Drv::SocketIpStatus
                      ) severity warning high \
      format "Frame stream socket open failed with status {}" \
      throttle 5

    @ A chunk send failed; the rest of that frame is dropped
    event SendFailed(
                      status: I32 @< Drv::SocketIpStatus
                    ) severity warning low \
      format "Frame stream send failed with status {}" \
      throttle 5

    @ Frames sent completely
    telemetry FramesSent: U32

    @ Captured frames replaced by a newer one before they were sent (latest wins)
    telemetry FramesSkipped: U32
    @ Frames dropped because the Ethernet carrier was down (subset of FramesSkipped)
    telemetry FramesLinkDown: U32

    @ Chunk datagrams sent
    telemetry ChunksSent: U32

    @ Chunk sends that failed
    telemetry SendErrors: U32

    @ Longest time to send one frame, all chunks, on the streamer thread
    telemetry TxTimeMaxUs: U32

    @ Longest frame copy on the engine thread (capture hook)
    telemetry CopyTimeMaxUs: U32

    command recv port cmdIn
    command reg port cmdRegOut
    command resp port cmdResponseOut
    event port eventOut
    text event port logTextOut
    telemetry port tlmOut
    time get port timeCaller
  }
}
