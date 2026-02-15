@echo off
set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VS_PATH%" (
    echo Error: vcvars64.bat not found at %VS_PATH%
    pause
    exit /b 1
)

call "%VS_PATH%"

echo Building EncodingConverter...
cl /EHsc /O2 /W3 /std:c++17 main.cpp /link user32.lib shell32.lib gdi32.lib ole32.lib /OUT:EncodingConverter.exe

if %ERRORLEVEL% equ 0 (
    echo Build Successful: EncodingConverter.exe
) else (
    echo Build Failed!
)
pause
