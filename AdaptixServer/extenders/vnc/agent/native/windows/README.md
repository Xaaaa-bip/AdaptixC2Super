Place the native Windows VNC server package in this directory.

Supported layout:

- `adaptix_start.bat` or `adaptix_start.cmd` is preferred. It receives the requested VNC listen port as `%1` and should start the bundled server in the foreground or background.
- If no start script exists, the beacon extender falls back to `winvnc.exe`, `winvnc4.exe`, any `*winvnc*.exe`, then the first `.exe` found.
- Support files and subdirectories are uploaded recursively to a random directory under `C:\Windows\Temp`.

The bundled UltraVNC payload uses `adaptix_start.bat` to rewrite `ultravnc.ini` for the requested local-only no-auth tunnel port before launching `winvnc.exe`.
It also runs `adaptix_probe.ps1` before reporting success, so the task output confirms that the server exposes RFB security type `None`.

Example `adaptix_start.bat`:

```bat
@echo off
set PORT=%1
start "" "%~dp0winvnc.exe" -run
```

Most native VNC servers need a prebuilt config file for no-auth/listen-port behavior. Put that config beside the executable and start it from the script.
