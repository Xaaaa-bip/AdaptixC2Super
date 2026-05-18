@echo off
echo Building VNC Stream Agent...
cl /nologo /O2 /MT /EHsc /Fe:vnc_stream.exe vnc_stream.c ^
    /link d3d11.lib dxgi.lib windowscodecs.lib ws2_32.lib ole32.lib user32.lib
if %ERRORLEVEL% EQU 0 (
    echo Build successful: vnc_stream.exe
) else (
    echo Build failed!
)
