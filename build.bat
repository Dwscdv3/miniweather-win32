@echo off
rem Build with MSVC, static CRT, no vcvars needed.
rem   build.bat        -> build\weather.exe        (x64, Vista+)
rem   build.bat win7   -> build\weather-x86.exe    (x86, Windows 7 SP1+, any 32-bit Windows)
rem   build.bat test   -> build, then run JSON + gzip self-tests
rem   build.bat shot   -> build tools\shot.c and tools\uitest.c (docs / smoke test only)
setlocal

set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "KITS=C:\Program Files (x86)\Windows Kits\10"

for /d %%D in ("%VSDIR%\VC\Tools\MSVC\*") do set "MSCVER=%%~fD"
for /d %%D in ("%KITS%\Include\*")        do set "SDKINC=%%~fD"
for /d %%D in ("%KITS%\Lib\*")            do set "SDKLIB=%%~fD"

set "CFLAGS=/nologo /std:c17 /utf-8 /O1 /W3 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
set "LIBS=winhttp.lib gdi32.lib user32.lib advapi32.lib shell32.lib"

if /I "%1"=="win7" goto win7
goto x64

rem ---------------- x86 / Windows 7 ----------------
:win7
if not exist "%MSCVER%\bin\Hostx86\x86\cl.exe" (
  echo [build] x86 toolset not found, edit VSDIR in this script.
  exit /b 1
)
set "PATH=%MSCVER%\bin\Hostx86\x86;%PATH%"
set "INCLUDE=%MSCVER%\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared;%SDKINC%\winrt;%SDKINC%\cppwinrt"
set "LIB=%MSCVER%\lib\x86;%SDKLIB%\ucrt\x86;%SDKLIB%\um\x86"
rem _WIN32_WINNT=0x0601 plus subsystem 6.01 declares Windows 7 as the floor
set "CFLAGS=%CFLAGS% /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /MT"
set "EXE=build\weather-x86.exe"
set "SYSSUB=/SUBSYSTEM:WINDOWS,6.01 /MACHINE:X86"
goto build

rem ---------------- x64 ----------------
:x64
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
set "EXE=build\weather.exe"
set "SYSSUB=/SUBSYSTEM:WINDOWS"
goto build

rem ---------------- common ----------------
:build
if not exist build mkdir build
if not exist build\testobj mkdir build\testobj

if /I "%1"=="shot" (
  cl %CFLAGS% /Fo:build\testobj\ /Fe:build\shot.exe tools\shot.c /link user32.lib gdi32.lib
  if errorlevel 1 exit /b 1
  cl %CFLAGS% /Fo:build\testobj\ /Fe:build\uitest.exe tools\uitest.c /link user32.lib
  if errorlevel 1 exit /b 1
  echo [build] ok - build\shot.exe build\uitest.exe
  exit /b 0
)

cl %CFLAGS% /Fo:build\ /Fe:%EXE% src\main.c src\ui.c src\api.c src\qweather.c src\gunzip.c src\net.c src\json.c src\util.c src\store.c ^
   /link %SYSSUB% %LIBS%
if errorlevel 1 exit /b 1
echo [build] ok - %EXE%

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
