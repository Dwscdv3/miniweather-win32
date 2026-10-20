@echo off
rem Build with MSVC x64 (static CRT), no vcvars64.bat needed.
rem   build.bat        -> build\weather.exe
rem   build.bat test   -> build app, then build and run JSON parser self-test
rem   build.bat shot   -> build tools\shot.c (screenshot helper for docs only)
setlocal

set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "KITS=C:\Program Files (x86)\Windows Kits\10"

for /d %%D in ("%VSDIR%\VC\Tools\MSVC\*") do set "MSCVER=%%~fD"
for /d %%D in ("%KITS%\Include\*")        do set "SDKINC=%%~fD"
for /d %%D in ("%KITS%\Lib\*")            do set "SDKLIB=%%~fD"

if not exist "%MSCVER%\bin\Hostx64\x64\cl.exe" (
  echo [build] MSVC cl.exe not found, edit VSDIR in this script.
  exit /b 1
)
if not exist "%SDKINC%\um\windows.h" (
  echo [build] Windows SDK headers not found, edit KITS in this script.
  exit /b 1
)

set "PATH=%MSCVER%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%MSCVER%\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared;%SDKINC%\winrt;%SDKINC%\cppwinrt"
set "LIB=%MSCVER%\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"

if not exist build mkdir build
if not exist build\testobj mkdir build\testobj

set CFLAGS=/nologo /std:c17 /utf-8 /O1 /W3 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE

if /I "%1"=="shot" (
  cl %CFLAGS% /Fo:build\testobj\ /Fe:build\shot.exe tools\shot.c /link user32.lib gdi32.lib
  if errorlevel 1 exit /b 1
  cl %CFLAGS% /Fo:build\testobj\ /Fe:build\uitest.exe tools\uitest.c /link user32.lib
  if errorlevel 1 exit /b 1
  echo [build] ok - build\shot.exe build\uitest.exe
  exit /b 0
)

cl %CFLAGS% /Fo:build\ /Fe:build\weather.exe src\main.c src\ui.c src\api.c src\qweather.c src\gunzip.c src\net.c src\json.c src\util.c src\store.c ^
   /link /SUBSYSTEM:WINDOWS winhttp.lib gdi32.lib user32.lib advapi32.lib shell32.lib
if errorlevel 1 exit /b 1
echo [build] ok - build\weather.exe

if /I "%1"=="test" (
  cl %CFLAGS% /Fo:build\testobj\ /Fe:build\test_json.exe tests\test_json.c src\json.c
  if errorlevel 1 exit /b 1
  build\test_json.exe tests\sample.json
  if errorlevel 1 exit /b 1

  cl %CFLAGS% /Fo:build\testobj\ /Fe:build\test_gunzip.exe tests\test_gunzip.c src\gunzip.c src\json.c
  if errorlevel 1 exit /b 1
  build\test_gunzip.exe tests\qweather_24h.gz
  if errorlevel 1 exit /b 1
)

endlocal
