@echo off
setlocal enabledelayedexpansion
rem Build AnimSpeedFix.dll (x86) and the optional AnimSpeedLoader.exe.
rem NOTE: %ProgramFiles(x86)% contains a ')' which closes batch if/for blocks
rem early, so paths are captured up front and referenced with !delayed! syntax
rem inside blocks.

set "PF=%ProgramFiles%"
set "PF86=%ProgramFiles(x86)%"
set "VCVARS="
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

rem well-known locations first (keeps the common case quiet)
for %%p in (
  "!PF!\Microsoft Visual Studio\2022\Community"
  "!PF!\Microsoft Visual Studio\2022\Professional"
  "!PF!\Microsoft Visual Studio\2022\Enterprise"
  "!PF86!\Microsoft Visual Studio\2019\Community"
) do if exist "%%~p\VC\Auxiliary\Build\vcvars32.bat" set "VCVARS=%%~p\VC\Auxiliary\Build\vcvars32.bat"

rem ...then ask vswhere for anything unusual
if not defined VCVARS if exist "!VSWHERE!" for /f "usebackq delims=" %%i in (
  `"!VSWHERE!" -latest -products * -property installationPath 2^>nul`
) do if exist "%%i\VC\Auxiliary\Build\vcvars32.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars32.bat"

if not defined VCVARS (
  echo ERROR: could not locate vcvars32.bat - is the x86 C++ toolset installed?
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
  echo ERROR: vcvars32.bat failed - is the x86 toolset installed?
  exit /b 1
)

pushd "%~dp0"
if not exist out mkdir out
if not exist out\proxy mkdir out\proxy

echo === AnimSpeedFix.dll (x86) ===
cl /nologo /LD /O2 /MT /W3 /GS- /EHsc ^
   /Fo:out\ /Fe:out\AnimSpeedFix.dll ^
   src\AnimSpeedFix.cpp src\playercollide.cpp src\goeditor.cpp src\rulerelax.cpp src\gluebridge.cpp src\nametag.cpp ^
   /link /SUBSYSTEM:WINDOWS /DYNAMICBASE:NO kernel32.lib user32.lib
if errorlevel 1 goto :fail

echo === dinput8.dll proxy (x86) - drop-in, no injector ===
cl /nologo /LD /O2 /MT /W3 /GS- /EHsc /DBUILD_DINPUT8_PROXY ^
   /Fo:out\proxy\ /Fe:out\dinput8.dll ^
   src\AnimSpeedFix.cpp src\playercollide.cpp src\goeditor.cpp src\rulerelax.cpp src\gluebridge.cpp src\nametag.cpp ^
   /link /SUBSYSTEM:WINDOWS /DYNAMICBASE:NO /DEF:src\dinput8.def kernel32.lib user32.lib
if errorlevel 1 goto :fail

echo === AnimSpeedLoader.exe (x86) ===
cl /nologo /O2 /MT /W3 /EHsc ^
   /Fo:out\ /Fe:out\AnimSpeedLoader.exe ^
   src\loader.cpp ^
   /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib
if errorlevel 1 goto :fail

echo === AnimSpeedSelfTest.exe (x86) ===
cl /nologo /O2 /MT /W3 /EHsc ^
   /Fo:out\ /Fe:out\AnimSpeedSelfTest.exe ^
   src\selftest.cpp ^
   /link /SUBSYSTEM:CONSOLE /DYNAMICBASE:NO /BASE:0x30000000 kernel32.lib user32.lib
if errorlevel 1 goto :fail

copy /y AnimSpeedFix.ini out\ >nul
echo.
echo Built:
dir /b out\*.dll out\*.exe out\*.ini

if /i "%~1"=="test" (
  echo.
  pushd out
  call ".\AnimSpeedSelfTest.exe"
  set TESTRC=!errorlevel!
  popd
  if not "!TESTRC!"=="0" goto :fail
)
popd
exit /b 0

:fail
echo.
echo BUILD FAILED
popd
exit /b 1
