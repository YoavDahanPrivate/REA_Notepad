@echo off
rem Builds Notepad-- with the Visual C++ Build Tools.
rem   build.bat           build for this PC's processor (x64 or ARM64)
rem   build.bat arm64     build an ARM64 exe (natively on ARM64, or cross-compiled on x64)
rem   build.bat x64       build an x64 exe
rem Output: build\<arch>\Notepad--.exe
setlocal
set "TARGET=%~1"
set "HOSTARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOSTARCH=arm64"
if not defined TARGET set "TARGET=%HOSTARCH%"
if /i not "%TARGET%"=="x64" if /i not "%TARGET%"=="arm64" (
    echo Unknown target "%TARGET%". Use x64 or arm64.
    exit /b 1
)

rem vcvarsall argument: native (x64, arm64) or cross (x64_arm64, arm64_x64)
set "VCARCH=%TARGET%"
if /i not "%HOSTARCH%"=="%TARGET%" set "VCARCH=%HOSTARCH%_%TARGET%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Build Tools are not installed. See README.md, step 1.
    exit /b 1
)
rem -all also finds installs that the installer still marks as incomplete.
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -all -products * -property installationPath`) do set "VSDIR=%%i"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo The C++ build tools workload is not installed. See README.md, step 1.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %VCARCH% >nul || (
    echo Could not set up the %VCARCH% compiler. For ARM64 builds install the
    echo "MSVC ARM64 build tools" component. See README.md, step 1.
    exit /b 1
)

cd /d "%~dp0"
set "OUT=build\%TARGET%"
if not exist "%OUT%" mkdir "%OUT%"
set "RCDEF="
if not exist src\app.ico set "RCDEF=/DNO_ICON"
rc /nologo %RCDEF% /fo %OUT%\notepad.res src\notepad.rc || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /permissive- /utf-8 /DUNICODE /D_UNICODE ^
   /Fo%OUT%\ /Fd%OUT%\ "/Fe%OUT%\Notepad--.exe" src\main.cpp src\markdown.cpp src\ui.cpp src\flyout.cpp src\dialog.cpp src\ascii.cpp %OUT%\notepad.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO || exit /b 1

rem Only if src\app.ico is missing: draw the icon with the app itself, then relink with it.
rem (src\app.ico is in the repository, so this normally doesn't run.)
if not defined RCDEF goto done
if /i not "%TARGET%"=="%HOSTARCH%" goto done
start "" /wait "%OUT%\Notepad--.exe" --write-icon src\app.ico
rc /nologo /fo %OUT%\notepad.res src\notepad.rc || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO "/OUT:%OUT%\Notepad--.exe" %OUT%\main.obj %OUT%\markdown.obj %OUT%\ui.obj %OUT%\flyout.obj %OUT%\dialog.obj %OUT%\ascii.obj %OUT%\notepad.res user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib dwmapi.lib uxtheme.lib advapi32.lib ole32.lib oleaut32.lib msimg32.lib || exit /b 1
:done
echo Built %OUT%\Notepad--.exe
