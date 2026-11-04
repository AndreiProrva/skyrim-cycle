@echo off
setlocal
rem Run from an x64 Visual Studio 2022 Developer Command Prompt.
rem Usage: build-native.cmd CommonLibSource CommonLibSSE.lib VcpkgInstalledPrefix
if "%~3"=="" (
  echo Usage: build-native.cmd CommonLibSource CommonLibSSE.lib VcpkgInstalledPrefix
  exit /b 2
)
set "C3_COMMONLIB=%~f1"
set "C3_LIBRARY=%~f2"
set "C3_PREFIX=%~f3"
set "C3_SOURCE=%~dp0native"
if not exist "%C3_SOURCE%\KeyboardMotion.cpp" exit /b 3
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /EHsc /std:c++latest /MD /O2 /utf-8 "%C3_SOURCE%\MotionTests.cpp" /Fe:MotionTests.exe
if errorlevel 1 exit /b 1
MotionTests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++latest /MD /O2 /utf-8 /Zc:preprocessor /LD /DENABLE_SKYRIM_AE=1 /DENABLE_SKYRIM_SE=1 /DHAS_SKYRIM_MULTI_TARGETING=1 /DNOMINMAX /DSPDLOG_COMPILED_LIB /DSPDLOG_FMT_EXTERNAL /DUNICODE /D_UNICODE /DUSING_XINPUT /DWIN32_LEAN_AND_MEAN /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /I"%C3_COMMONLIB%\include" /I"%C3_PREFIX%\include" /I"%C3_PREFIX%\include\directxtk" /I"%C3_COMMONLIB%\extern\openvr\headers" "%C3_SOURCE%\KeyboardMotion.cpp" /link /OUT:C3KeyboardMotion.dll "%C3_LIBRARY%" /LIBPATH:"%C3_PREFIX%\lib" spdlog.lib fmt.lib DirectXTK.lib advapi32.lib bcrypt.lib D3D11.lib d3dcompiler.lib dbghelp.lib DXGI.lib ole32.lib version.lib kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib oleaut32.lib uuid.lib comdlg32.lib winmm.lib
if errorlevel 1 exit /b 1
popd
endlocal
