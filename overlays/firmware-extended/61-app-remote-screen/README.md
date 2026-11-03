# Remote touchscreen over WebRTC

`/screen/` receives H.264 video and sends touch events over the same WebRTC
connection. HTTP handles the page, authentication and SDP/ICE signalling;
the viewer does not fall back to HTTP touch requests.

The display producer publishes complete frames to `/run/ui-frame`. `fb-mpp`
encodes the latest frame with Rockchip MPP and supplies `stream-screen-webrtc`.
Older producers can use the visible framebuffer page as a compatibility source.
Video encoding is independent of the Mali GPU setting.

`screen.js` sends `down`, `move`, `hold` and `up` on the server-created
`keepalive` data channel. Its current libdatachannel defaults are reliable,
ordered delivery. `screen_touch.h` accepts one controlling viewer at a time,
releases input on disconnect and expires a press after two seconds without
renewal. A stationary press sends `hold` every 500 ms. The channel acknowledges
accepted input with `touch-ok`.

Reliable ordering can delay newer movement behind retransmission on a weak
network. The current viewer sends every pointer move and does not yet coalesce
queued positions. WebRTC alone does not guarantee low touch latency. A future
bounded movement channel must preserve reliable press/release and reject stale
or reordered positions across gestures; this has not been implemented or
validated by the menu-cadence fix.

`fb-http.py` still serves PNG snapshots and the loopback diagnostic `/touch`
endpoint on port 8092. These support inspection and local test input; they are
not the video or touch transport used by `/screen/`.

Enable the feature in Firmware's Web settings. See
[Remote screen access](../../../../docs/remote_screen.md) for configuration.
