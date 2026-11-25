@echo off
setlocal
where cl >nul 2>&1
if errorlevel 1 (
    echo Run this script from x64 Native Tools Command Prompt for VS 2022.
    exit /b 1
)
pushd "%~dp0.."
if not exist "build\x64\Release\obj\Scene.obj" (
    echo First build Physics.sln in Release x64.
    popd
    exit /b 1
)
cl /nologo /std:c++17 /EHsc /MD /O2 /DNDEBUG /W4 /WX /IPhysics /Ivendor\SFML-3.1.0\include tests\ReportValidation.cpp build\x64\Release\obj\Object.obj build\x64\Release\obj\Button.obj build\x64\Release\obj\Game.obj build\x64\Release\obj\PhyObject.obj build\x64\Release\obj\Scene.obj /Fobuild\x64\Release\ReportValidation.obj /Febuild\x64\Release\ReportValidation.exe /link /OPT:NOREF /OPT:NOICF /LIBPATH:vendor\SFML-3.1.0\lib sfml-graphics.lib sfml-window.lib sfml-system.lib
if errorlevel 1 (
    popd
    exit /b 1
)
build\x64\Release\ReportValidation.exe
set "validationExit=%ERRORLEVEL%"
popd
exit /b %validationExit%
