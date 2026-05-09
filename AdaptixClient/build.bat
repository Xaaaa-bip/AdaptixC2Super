@echo off
set "PATH=C:\msys64\mingw64\bin;D:\Qt\6.11.0\mingw_64\bin;%PATH%"

echo [0/5] Map project to X: drive (avoids path issues with Chinese chars)...
subst X: /D >nul 2>&1
subst X: "%~dp0.."

echo [1/5] Build Release...
cmake -S X:/AdaptixClient -B X:/AdaptixClient/cmake-build-release -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe ^
    -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
if %ERRORLEVEL% NEQ 0 goto :error
cmake --build X:/AdaptixClient/cmake-build-release --config Release
if %ERRORLEVEL% NEQ 0 goto :error

echo [2/5] Create dist...
if exist dist rmdir /s /q dist
mkdir dist
copy X:\AdaptixClient\cmake-build-release\AdaptixClient.exe dist\ >nul

echo [3/5] windeployqt...
cd dist
windeployqt.exe AdaptixClient.exe
cd ..

echo [4/5] Add MSYS2 runtime DLLs...
for %%f in (libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll zlib1.dll libzstd.dll libintl-8.dll libiconv-2.dll) do (
    copy "C:\msys64\mingw64\bin\%%f" dist\ >nul 2>&1
)

echo [5/5] Cleanup X: drive...
subst X: /D >nul 2>&1

echo [OK] Complete! Output: dist\AdaptixClient.exe
pause
goto :eof

:error
echo BUILD FAILED!
subst X: /D >nul 2>&1
pause
exit /b 1
