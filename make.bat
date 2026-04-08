@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" arm64 >nul 2>&1

echo [*] Building totalrecall.exe...
cl.exe /EHsc /W3 /O2 totalrecall.cpp /Fe:totalrecall.exe /link advapi32.lib shell32.lib user32.lib version.lib >nul
if %errorlevel% neq 0 (echo [X] totalrecall.exe FAILED & exit /b 1)
echo [+] totalrecall.exe

echo [*] Building totalrecall_payload.dll...
cl.exe /LD /EHsc /W3 /O2 totalrecall_payload.cpp /Fe:totalrecall_payload.dll /link ole32.lib oleaut32.lib runtimeobject.lib user32.lib gdi32.lib windowscodecs.lib shlwapi.lib >nul
if %errorlevel% neq 0 (echo [X] totalrecall_payload.dll FAILED & exit /b 1)
echo [+] totalrecall_payload.dll

del *.obj *.lib *.exp 2>nul
echo [+] Done
