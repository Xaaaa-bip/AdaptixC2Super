# Native VNC Integration Spec

## Goal

Replace the current MJPEG screenshot streamer path with a standard VNC/RFB path:

- Agent side runs a real VNC server on the target.
- Teamserver keeps using the existing `lportfwd` tunnel.
- AdaptixClient embeds a native Qt VNC viewer dock.

This avoids continuous whole-screen JPEG screenshots. RFB sends an initial framebuffer and then incremental dirty rectangles, using Tight/ZRLE compression when the server supports them.

## Current State

The existing `AdaptixServer/extenders/vnc` implementation is VNC-like, not native VNC:

- `vnc_stream.exe` captures DXGI/GDI frames.
- Frames are JPEG encoded and served as MJPEG over `/stream`.
- Input is sent by HTTP JSON to `/mouse` and `/keyboard`.
- The web viewer parses JPEG markers and displays frames.

This is bandwidth-heavy and CPU-heavy under motion.

## Target Architecture

### Agent

The target must run a standard VNC server that listens on localhost, for example:

- TigerVNC `winvnc.exe`
- UltraVNC `winvnc.exe`
- A future in-tree `libvncserver`-based minimal server

The operator can either point Adaptix at an already-running VNC server or use `vnc deploy` to upload and start a packaged Windows VNC server payload.

Recommended agent listener:

```text
127.0.0.1:5900
```

### Tunnel

Use the existing server-side local port forward:

```text
teamserver:<local_port> -> agent:127.0.0.1:<vnc_port>
```

No tunnel protocol changes are required.

### Client

AdaptixClient gets a new `VncViewerWidget` dock:

- Connects directly to the teamserver lportfwd listener and auto-connects when opened from a VNC tunnel.
- Speaks RFB over TCP.
- Renders framebuffer updates in a Qt widget.
- Sends RFB keyboard and pointer events.

Default connection target when opened from a tunnel:

```text
host = Adaptix profile host
port = selected tunnel listen port
```

The host field remains editable for local/teamserver topology differences.

## MVP Protocol Support

The first implementation is intentionally minimal:

- RFB 3.8 and 3.3 protocol negotiation.
- Security type `None`.
- 32-bit true-color pixel format requested from the server.
- Encodings:
  - `Tight`
  - `ZRLE`
  - `Raw`
  - `CopyRect`
  - `DesktopSize`
- Incremental framebuffer update requests.
- Pointer events.
- Common keyboard keysyms.

Limitations:

- VNC password authentication is not implemented yet.
- Tight covers fill, JPEG/PNG, copy, palette and gradient filters; ZRLE covers raw, solid, packed palette and RLE tiles.
- Clipboard sync is not implemented yet.
- Multiple monitor control depends on the selected VNC server.

## Next Phase

To tighten compatibility:

1. Add VNC authentication.
2. Add VNCAuth/VeNCrypt if authenticated native servers are required.
3. Add Cursor pseudo-encoding for local cursor rendering.
4. Add a first-party minimal `libvncserver` payload if relying on external winvnc packages is undesirable.

## Operator Workflow

### Already-Running Server

1. Start a standard VNC server on the agent, listening on `127.0.0.1:5900`.
2. Create an lportfwd tunnel:

```text
teamserver:15900 -> agent:127.0.0.1:5900
```

The beacon/gopher command wrapper provides a shortcut for this:

```text
vnc native 5900 15900
```

3. In AdaptixClient, open the Tunnels dock.
4. Right-click the VNC tunnel and choose `Open VNC viewer`.
5. The viewer dock opens and connects automatically; use `Disconnect`/`Connect` only for manual reconnects.

### Packaged Server

1. Place the Windows VNC server package under `AdaptixServer/extenders/vnc/agent/native/windows`.
2. Prefer an `adaptix_start.bat` or `adaptix_start.cmd` file that starts the server; it receives the requested VNC port as `%1`.
3. Use the session menu item `VNC Desktop -> Start Native VNC Server`, or run:

```text
vnc deploy 5900 15900
```

4. The extender creates the lportfwd tunnel, queues directory creation and uploads, starts the packaged server, and the AdaptixClient tunnel dock opens the embedded viewer automatically when the tunnel syncs.

## Acceptance Criteria

- Client can open a dock from a selected tunnel.
- Native VNC tunnels open the dock automatically after sync.
- `vnc deploy` uploads the packaged Windows VNC payload, starts it, and creates the tunnel.
- Client can complete RFB handshake against a no-auth VNC server.
- Initial desktop frame renders.
- Incremental screen updates refresh without reconnecting.
- Mouse movement, clicks, wheel, and common keyboard keys are sent through RFB.
- Existing MJPEG/web viewer code remains available as fallback.
