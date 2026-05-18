@echo off
setlocal EnableExtensions
set "PORT=%~1"
if "%PORT%"=="" set "PORT=5900"
cd /d "%~dp0"

set "LOG=%TEMP%\adaptix_winvnc_%PORT%.log"
set "PROBE=%~dp0adaptix_probe.ps1"

echo [adaptix-vnc] dir=%CD% port=%PORT% > "%LOG%"
echo [adaptix-vnc] writing UltraVNC config >> "%LOG%"
(
    echo [ultravnc]
    echo passwd=
    echo passwd2=
    echo [admin]
    echo UseRegistry=0
    echo AuthRequired=0
    echo MSLogonRequired=0
    echo NewMSLogon=0
    echo UseDSMPlugin=0
    echo DSMPlugin=
    echo SocketConnect=1
    echo AutoPortSelect=0
    echo PortNumber=%PORT%
    echo HTTPConnect=0
    echo HTTPPortNumber=0
    echo AllowLoopback=1
    echo LoopbackOnly=1
    echo InputsEnabled=1
    echo QueryIfNoLogon=0
    echo QuerySetting=2
    echo QueryAccept=1
    echo ConnectPriority=0
    echo DisableTrayIcon=1
    echo AllowShutdown=0
    echo AllowProperties=0
    echo AllowEditClients=0
    echo path=%TEMP%
    echo DebugMode=0
    echo DebugLevel=0
    echo [poll]
    echo TurboMode=1
    echo PollFullScreen=1
    echo EnableHook=1
) > "%~dp0ultravnc.ini"

if not exist "%~dp0winvnc.exe" (
    echo [adaptix-vnc] ERROR: winvnc.exe not found in %CD%
    type "%LOG%"
    exit /b 1
)

if not exist "%PROBE%" (
    echo [adaptix-vnc] ERROR: adaptix_probe.ps1 not found in %CD%
    type "%LOG%"
    exit /b 1
)

echo [adaptix-vnc] configuring local firewall rule >> "%LOG%"
netsh advfirewall firewall delete rule name="Adaptix UltraVNC %PORT%" program="%~dp0winvnc.exe" >nul 2>&1
netsh advfirewall firewall add rule name="Adaptix UltraVNC %PORT%" dir=in action=allow program="%~dp0winvnc.exe" protocol=TCP localip=127.0.0.1 localport=%PORT% profile=any enable=yes >> "%LOG%" 2>>&1

echo [adaptix-vnc] starting UltraVNC winvnc >> "%LOG%"
"%~dp0winvnc.exe" -kill >> "%LOG%" 2>>&1
start "" /D "%~dp0" "%~dp0winvnc.exe" -multi -run

for /L %%I in (1,1,45) do (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%PROBE%" -Port %PORT% >> "%LOG%" 2>>&1
    if not errorlevel 1 (
        echo [adaptix-vnc] listening on 127.0.0.1:%PORT% with RFB security None
        type "%LOG%"
        exit /b 0
    )
    timeout /t 1 /nobreak >nul
)

echo [adaptix-vnc] ERROR: winvnc did not expose a no-auth RFB server on 127.0.0.1:%PORT%
tasklist /FI "IMAGENAME eq winvnc.exe"
netstat -ano | findstr ":%PORT%"
type "%LOG%"
exit /b 1
