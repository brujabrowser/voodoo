@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem Copied from ~/go++/wasigocvm.bat so this dir links on that same clang line.
set "WASIGO_HERE=%~dp0"
if "%WASIGO_HERE:~-1%"=="\" set "WASIGO_HERE=%WASIGO_HERE:~0,-1%"
set "WASIGO_ROOT=%USERPROFILE%\go++"
cd /d "%WASIGO_ROOT%"

rem wasigocvm - Go++ -> C++ -> wasm using OUR toolchain contract.
rem Product define: -DWASIGO_GOCVM=1. compile.bat is the default driver
rem (this file is the same machine). Stock wasip1 leftover: legacy.bat.
rem
rem   compile.bat examples\httppkg\main.go
rem   wasigocvm.bat hello.go -o out.wasm

set "CONFIG=Release"
set "GOFILE="
set "OUTWASM="
set "WASIGOC_EXTRA="

:parse
if "%~1"=="" goto :parsed
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage
if /I "%~1"=="/?" goto :usage
if /I "%~1"=="-o" (
  if "%~2"=="" (echo error: -o requires a path & exit /b 2)
  set "OUTWASM=%~2"
  shift & shift & goto :parse
)
if /I "%~1"=="Debug" (set "CONFIG=Debug" & shift & goto :parse)
if /I "%~1"=="Release" (set "CONFIG=Release" & shift & goto :parse)
if /I "%~x1"==".go" (
  if defined GOFILE (echo error: extra .go file & exit /b 2)
  set "GOFILE=%~1" & shift & goto :parse
)
set "WASIGOC_EXTRA=!WASIGOC_EXTRA! %1"
shift
goto :parse

:parsed
call :find_wasigoc
if not defined WASIGOC_EXE (
  echo building wasigoc...
  call "%WASIGO_ROOT%\legacy.bat" %CONFIG%
  if errorlevel 1 exit /b 1
  call :find_wasigoc
)
if not defined WASIGOC_EXE (echo error: wasigoc.exe not found & exit /b 1)
if not defined GOFILE (
  echo wasigocvm: full libc++ / eh / rtti / WASIGO_GOCVM ^(docs\wasigocvm.md^)
  echo usage: wasigocvm.bat ^<file.go^> [-o out.wasm]
  exit /b 0
)
if not exist "%GOFILE%" if exist "%WASIGO_HERE%\%GOFILE%" set "GOFILE=%WASIGO_HERE%\%GOFILE%"
if not exist "%GOFILE%" (echo error: not found: %GOFILE% & exit /b 1)

for %%I in ("%GOFILE%") do (
  set "GO_ABS=%%~fI"
  set "GO_DIR=%%~dpI"
  set "GO_BASE=%%~nI"
)
if "%GO_DIR:~-1%"=="\" set "GO_DIR=%GO_DIR:~0,-1%"
set "GENCPP=%GO_DIR%\%GO_BASE%_gen.cpp"
if not defined OUTWASM set "OUTWASM=%GO_DIR%\%GO_BASE%.wasm"
if defined OUTWASM if "!OUTWASM:~1,1!" neq ":" if "!OUTWASM:~0,1!" neq "\" set "OUTWASM=%WASIGO_HERE%\!OUTWASM!"

echo [wasigoc] %GO_ABS%
"%WASIGOC_EXE%" "%GO_ABS%" -o "%GENCPP%" --out-dir "%GO_DIR%" %WASIGOC_EXTRA%
if errorlevel 1 exit /b 1

call :find_toolchain
if not defined WASIGO_CLANG (
  echo error: no wasigocvm clang - set WASIGO_TOOLCHAIN or run toolchain\bootstrap.ps1
  echo          ^(fallback: %%USERPROFILE%%\wasi-sdk with eh/^)
  exit /b 1
)

call :pick_triple
if not defined WASIGO_TRIPLE (
  echo error: full libc++ ^(eh^) missing under %WASIGO_SYSROOT%\include
  echo        run toolchain\bootstrap.ps1 or use a wasi-sdk with eh/
  exit /b 1
)

set "EH_INC=%WASIGO_SYSROOT%\include\%WASIGO_TRIPLE%\eh\c++\v1"
set "EH_LIB=%WASIGO_SYSROOT%\lib\%WASIGO_TRIPLE%\eh"

rem Threads: stamp threads:true + libpthread (prefer this triple so sockets stay).
set "WASIGO_THREADS="
set "WASIGO_PTHREAD_FLAGS="
set "STAMP=%WASIGO_ROOT%\toolchain\wasigocvm-toolchain.json"
if defined WASIGO_SDK_ROOT if exist "%WASIGO_SDK_ROOT%\wasigocvm-toolchain.json" set "STAMP=%WASIGO_SDK_ROOT%\wasigocvm-toolchain.json"
if exist "%STAMP%" (
  findstr /R /C:"threads.:.*true" "%STAMP%" >nul 2>&1 && set "WASIGO_THREADS=1"
)
if defined WASIGO_THREADS (
  set "PTHREAD_LIBDIR="
  if exist "%WASIGO_SYSROOT%\lib\%WASIGO_TRIPLE%\libpthread.a" set "PTHREAD_LIBDIR=%WASIGO_SYSROOT%\lib\%WASIGO_TRIPLE%"
  if not defined PTHREAD_LIBDIR if exist "%USERPROFILE%\wasi-sdk\share\wasi-sysroot\lib\%WASIGO_TRIPLE%\libpthread.a" set "PTHREAD_LIBDIR=%USERPROFILE%\wasi-sdk\share\wasi-sysroot\lib\%WASIGO_TRIPLE%"
  if not defined PTHREAD_LIBDIR if exist "%WASIGO_SYSROOT%\lib\wasm32-wasip1-threads\libpthread.a" set "PTHREAD_LIBDIR=%WASIGO_SYSROOT%\lib\wasm32-wasip1-threads"
  if defined PTHREAD_LIBDIR (
    rem WASMSafeSpace: wasm32 linear memory already is the process boundary.
    rem Do not pass --shared-memory (eh libc++ was not built with atomics;
    rem TLS errno disagrees with wasip2 libc). Child working set lives in
    rem ~/WASMSafeSpace Sandbox (heap cage). -D_REENTRANT without -pthread
    rem so errno stays non-TLS. std::thread still links -lpthread.
    set WASIGO_PTHREAD_FLAGS=-D_REENTRANT -L "%PTHREAD_LIBDIR%" -lpthread
  ) else (
    echo warning: stamp threads:true but libpthread.a missing - building without -pthread
    set "WASIGO_THREADS="
    set "WASIGO_PTHREAD_FLAGS="
  )
)


echo [wasigocvm] %WASIGO_CLANG%
echo             triple=%WASIGO_TRIPLE%  full libc++ + rtti + standard wasm EH
if defined WASIGO_THREADS echo             threads=on  (std::thread fork, WASMSafeSpace cage, no wasm shared-memory)
rem Resolve <go++>/src even if %~dp0 is toolchain\ (hardlink / odd invoke)
set "WASIGO_SRC_INC="
if exist "%WASIGO_ROOT%\src\wasigocvm_config.hpp" set "WASIGO_SRC_INC=%WASIGO_ROOT%\src"
if not defined WASIGO_SRC_INC set "WASIGO_SRC_INC=C:\Users\grego\go++\src"
echo             -I "%GO_DIR%" -I "%WASIGO_SRC_INC%"
set "WASIGO_PE="
if exist "%WASIGO_ROOT%\WASMPELoader\include\wasmpe\loader.hpp" set "WASIGO_PE=%WASIGO_ROOT%\WASMPELoader"
if not defined WASIGO_PE if exist "%USERPROFILE%\WASMPELoader\include\wasmpe\loader.hpp" set "WASIGO_PE=%USERPROFILE%\WASMPELoader"
if not defined WASIGO_PE if exist "%WASIGO_ROOT%\..\WASMPELoader\include\wasmpe\loader.hpp" for %%I in ("%WASIGO_ROOT%\..\WASMPELoader") do set "WASIGO_PE=%%~fI"
set "WASIGO_PE_I="
if defined WASIGO_PE (
  set WASIGO_PE_I=-I "!WASIGO_PE!\include"
  echo             -I "!WASIGO_PE!\include" ^(WASMPELoader^)
)
if exist "%WASIGO_ROOT%\WASMWin32\include\win32\wasi_host.hpp" set "WASIGO_WIN32=%WASIGO_ROOT%\WASMWin32"
if not defined WASIGO_WIN32 (
  echo error: wasigocvm requires WASMWin32\ in this repo ^(win32/wasi_host.hpp^)
  exit /b 2
)
set "WASIGO_WIN32_I="
set "WASIGO_WIN32_CC="
if defined WASIGO_WIN32 (
  set WASIGO_WIN32_I=-I "!WASIGO_WIN32!\include"
  echo             -I "!WASIGO_WIN32!\include" ^(WASMWin32 libc host^)
  if exist "!WASIGO_WIN32!\src\catalog.cc" (
    set WASIGO_WIN32_CC="!WASIGO_WIN32!\src\catalog.cc"
    echo             WASMWin32 catalog on EPT ^(TPT process/thread, CHPT session^)
  )
  if exist "!WASIGO_WIN32!\src\host_wasi.cc" (
    set WASIGO_WIN32_CC=!WASIGO_WIN32_CC! "!WASIGO_WIN32!\src\host_wasi.cc"
    echo             WASMWin32 wasmwin32_call ^(occupyCalc CreateProcessW^)
  )
)
if exist "%WASIGO_ROOT%\WASMNix\include\nix\posix_host.hpp" set "WASIGO_NIX=%WASIGO_ROOT%\WASMNix"
if not defined WASIGO_NIX (
  echo error: wasigocvm requires WASMNix\ in this repo ^(nix/posix_host.hpp^)
  exit /b 2
)
set "WASIGO_NIX_I="
set "WASIGO_NIX_CC="
if defined WASIGO_NIX (
  set WASIGO_NIX_I=-I "!WASIGO_NIX!\include"
  echo             -I "!WASIGO_NIX!\include" ^(WASMNix libc host^)
  if exist "!WASIGO_NIX!\src\catalog.cc" (
    set WASIGO_NIX_CC="!WASIGO_NIX!\src\catalog.cc"
    echo             WASMNix catalog on EPT ^(CHPT session^)
  )
)
if exist "%WASIGO_ROOT%\WASMDroid\include\droid\bionic_host.hpp" set "WASIGO_DROID=%WASIGO_ROOT%\WASMDroid"
if not defined WASIGO_DROID (
  echo error: wasigocvm requires WASMDroid\ in this repo ^(droid/bionic_host.hpp^)
  exit /b 2
)
set "WASIGO_DROID_I="
set "WASIGO_DROID_CC="
if defined WASIGO_DROID (
  set WASIGO_DROID_I=-I "!WASIGO_DROID!\include"
  echo             -I "!WASIGO_DROID!\include" ^(WASMDroid libc host^)
  if exist "!WASIGO_DROID!\src\catalog.cc" (
    set WASIGO_DROID_CC="!WASIGO_DROID!\src\catalog.cc"
    echo             WASMDroid catalog on EPT ^(CHPT session, Binder root^)
  )
)
if exist "%WASIGO_ROOT%\WASMGocOS\include\gocos\host.hpp" set "WASIGO_GOCOS=%WASIGO_ROOT%\WASMGocOS"
if not defined WASIGO_GOCOS (
  echo error: wasigocvm requires WASMGocOS\ in this repo ^(gocos/host.hpp^)
  exit /b 2
)
set "WASIGO_GOCOS_I="
set "WASIGO_GOCOS_CC="
if defined WASIGO_GOCOS (
  set WASIGO_GOCOS_I=-I "!WASIGO_GOCOS!\include"
  echo             -I "!WASIGO_GOCOS!\include" ^(WASMGocOS edge kernel^)
  if exist "!WASIGO_GOCOS!\src\catalog.cc" (
    set WASIGO_GOCOS_CC="!WASIGO_GOCOS!\src\catalog.cc"
    echo             WASMGocOS catalog on EPT ^(CHPT session, GocDesk^)
  )
)
set "WASIGO_CHROME="
if exist "%USERPROFILE%\WASMChrome\include\chrome\host.hpp" set "WASIGO_CHROME=%USERPROFILE%\WASMChrome"
if not defined WASIGO_CHROME if exist "%WASIGO_ROOT%\WASMChrome\include\chrome\host.hpp" set "WASIGO_CHROME=%WASIGO_ROOT%\WASMChrome"
if not defined WASIGO_CHROME if exist "%WASIGO_ROOT%\..\WASMChrome\include\chrome\host.hpp" for %%I in ("%WASIGO_ROOT%\..\WASMChrome") do set "WASIGO_CHROME=%%~fI"
set "WASIGO_CHROME_I="
set "WASIGO_CHROME_CC="
set "WASIGO_CHROME_D="
if defined WASIGO_CHROME (
  set WASIGO_CHROME_I=-I "!WASIGO_CHROME!\include"
  set WASIGO_CHROME_D=-DWASIGO_HAS_WASMCHROME=1
  set WASIGO_CHROME_CC="!WASIGO_CHROME!\src\catalog.cc" "!WASIGO_CHROME!\src\host.cc" "!WASIGO_CHROME!\src\webui_catalog.cc"
  echo             -I "!WASIGO_CHROME!\include" ^(WASMChrome mojom catalog^)
)
set "WASIGO_WOW="
if exist "%WASIGO_ROOT%\WSMOccpuyWin32\include\wow\occupancy.h" set "WASIGO_WOW=%WASIGO_ROOT%\WSMOccpuyWin32"
if not defined WASIGO_WOW if exist "%USERPROFILE%\WSMOccpuyWin32\include\wow\occupancy.h" set "WASIGO_WOW=%USERPROFILE%\WSMOccpuyWin32"
if not defined WASIGO_WOW if exist "%USERPROFILE%\WSMOccupyWin32\include\wow\occupancy.h" set "WASIGO_WOW=%USERPROFILE%\WSMOccupyWin32"
if not defined WASIGO_WOW if exist "%WASIGO_ROOT%\..\WSMOccpuyWin32\include\wow\occupancy.h" for %%I in ("%WASIGO_ROOT%\..\WSMOccpuyWin32") do set "WASIGO_WOW=%%~fI"
set "WASIGO_WOW_I="
set "WASIGO_WOW_CC="
set "WASIGO_WOW_D="
if defined WASIGO_WOW (
  set WASIGO_WOW_I=-I "!WASIGO_WOW!\include"
  set WASIGO_WOW_D=-DWOW_HAS_WIN32=1 -DWOW_HAS_GOCVM=1 -DWASIGO_GOCVM_BRIDGE=1
  set WASIGO_WOW_CC="!WASIGO_WOW!\src\catalog\catalog.cc" "!WASIGO_WOW!\src\surface\surface.cc" "!WASIGO_WOW!\src\store\store.cc" "!WASIGO_WOW!\src\event\event.cc" "!WASIGO_WOW!\src\occupancy\occupancy.cc" "!WASIGO_WOW!\src\ipc\ipc.cc" "!WASIGO_WOW!\src\harness\harness.cc" "!WASIGO_WOW!\src\gocvm\bridge.cc" "!WASIGO_WOW!\src\gocvm\install.cc" "!WASIGO_WOW!\src\c\system.cc" "!WASIGO_WOW!\src\driver\driver.cc" "!WASIGO_WOW!\src\posix\posix.cc" "!WASIGO_WOW!\src\tty\tty.cc"
  echo             -I "!WASIGO_WOW!\include" ^(WSMOccpuyWin32 occupyCmd / CreateProcessW^)
)
set "WASIGO_SAFE="
if exist "%WASIGO_ROOT%\WASMSafeSpace\src\sandbox\sandbox.cc" set "WASIGO_SAFE=%WASIGO_ROOT%\WASMSafeSpace"
if not defined WASIGO_SAFE (
  echo error: wasigocvm requires WASMSafeSpace\ in this repo ^(sandbox.cc^)
  exit /b 2
)
set "WASIGO_SAFE_I="
set "WASIGO_SAFE_CC="
if defined WASIGO_SAFE (
  set WASIGO_SAFE_I=-I "!WASIGO_SAFE!\include" -I "!WASIGO_SAFE!"
  set WASIGO_SAFE_CC="!WASIGO_SAFE!\src\sandbox\sandbox.cc" "!WASIGO_SAFE!\src\sandbox\external-pointer-table.cc" "!WASIGO_SAFE!\src\sandbox\trusted-pointer-table.cc"
  echo             WASMSafeSpace EPT/TPT ^(exec second address space^)
)
set "WASIGO_V8="
if exist "%WASIGO_ROOT%\WASMv8bindings\include\cppgc\heap.h" set "WASIGO_V8=%WASIGO_ROOT%\WASMv8bindings"
if not defined WASIGO_V8 (
  echo error: wasigocvm requires WASMv8bindings\ in this repo ^(cppgc/heap.h^)
  exit /b 2
)
set "WASIGO_V8_I="
set "WASIGO_V8_CC="
if defined WASIGO_V8 (
  set WASIGO_V8_I=-I "!WASIGO_V8!\include" -I "!WASIGO_V8!"
  set WASIGO_V8_CC="!WASIGO_V8!\src\heap\internal\cppgc-logging.cc" "!WASIGO_V8!\src\heap\internal\free-list.cc" "!WASIGO_V8!\src\heap\internal\gc-info.cc" "!WASIGO_V8!\src\heap\internal\gc-info-table.cc" "!WASIGO_V8!\src\heap\internal\heap.cc" "!WASIGO_V8!\src\heap\internal\platform.cc" "!WASIGO_V8!\src\sandbox\cppheap-pointer-table.cc"
  echo             WASMv8bindings CHPT ^(exec second address space^)
)
set "WASIGO_SSL_I="
set "WASIGO_SSL_L="
set "WASIGO_SSL_D="
set "WASIGO_SSL_INC="
set "WASIGO_SSL_LIB="
if exist "%WASIGO_ROOT%\toolchain\openssl-wasm\lib\libssl.a" (
  if exist "%WASIGO_ROOT%\toolchain\openssl-wasm\include\openssl\ssl.h" (
    set "WASIGO_SSL_INC=%WASIGO_ROOT%\toolchain\openssl-wasm\include"
    set "WASIGO_SSL_LIB=%WASIGO_ROOT%\toolchain\openssl-wasm\lib"
  )
)
if not defined WASIGO_SSL_INC if exist "%WASIGO_SYSROOT%\include\openssl\ssl.h" (
  if exist "%WASIGO_SYSROOT%\lib\%WASIGO_TRIPLE%\libssl.a" (
    set "WASIGO_SSL_INC=%WASIGO_SYSROOT%\include"
    set "WASIGO_SSL_LIB=%WASIGO_SYSROOT%\lib\%WASIGO_TRIPLE%"
  )
)
if defined WASIGO_SSL_INC (
  set WASIGO_SSL_I=-I "!WASIGO_SSL_INC!"
  set WASIGO_SSL_L="!WASIGO_SSL_LIB!\libssl.a" "!WASIGO_SSL_LIB!\libcrypto.a"
  set WASIGO_SSL_D=-DWASIGO_HAS_OPENSSL=1
  echo             OpenSSL wasm ^(WASMLime TlsTransport / memory BIO^)
)
set WASIGO_VOODOO_I=-I "!WASIGO_HERE!\src"
set WASIGO_VOODOO_D=-DWASIGO_HAS_VOODOOMC=1
set WASIGO_VOODOO_CC="!WASIGO_HERE!\src\lexer.cc" "!WASIGO_HERE!\src\parser.cc" "!WASIGO_HERE!\src\module_loader.cc" "!WASIGO_HERE!\src\cpp_generator.cc" "!WASIGO_HERE!\src\type_intern.cc"
echo             -I "!WASIGO_HERE!\src" ^(WASMVoodooCompile voodoomc^)
set "GOCLC_WRAP=-Wl,--wrap=fopen -Wl,--wrap=fclose -Wl,--wrap=fread -Wl,--wrap=fwrite -Wl,--wrap=fflush -Wl,--wrap=fseek -Wl,--wrap=ftell -Wl,--wrap=fileno -Wl,--wrap=fdopen -Wl,--wrap=feof -Wl,--wrap=ferror -Wl,--wrap=clearerr -Wl,--wrap=rewind -Wl,--wrap=stat -Wl,--wrap=lstat -Wl,--wrap=fstat -Wl,--wrap=mkdir -Wl,--wrap=rmdir -Wl,--wrap=unlink -Wl,--wrap=remove -Wl,--wrap=rename -Wl,--wrap=access -Wl,--wrap=getcwd -Wl,--wrap=chdir -Wl,--wrap=opendir -Wl,--wrap=readdir -Wl,--wrap=closedir -Wl,--wrap=pipe -Wl,--wrap=dup -Wl,--wrap=dup2"
"%WASIGO_CLANG%" -O2 -std=c++20 -fuse-ld=lld ^
  -fexceptions -frtti ^
  -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false ^
  -nostdinc++ ^
  -isystem "%EH_INC%" ^
  -isystem "%WASIGO_SYSROOT%\include\%WASIGO_TRIPLE%" ^
  -isystem "%WASIGO_SYSROOT%\include" ^
  -L "%EH_LIB%" -lc++ -lc++abi -lunwind ^
  -Wl,--export=__indirect_function_table ^
  %GOCLC_WRAP% ^
  %WASIGO_PTHREAD_FLAGS% ^
  -I "%GO_DIR%" -I "%WASIGO_SRC_INC%" !WASIGO_WIN32_I! !WASIGO_NIX_I! !WASIGO_DROID_I! !WASIGO_GOCOS_I! !WASIGO_CHROME_I! !WASIGO_VOODOO_I! !WASIGO_WOW_I! !WASIGO_PE_I! !WASIGO_SAFE_I! !WASIGO_V8_I! !WASIGO_SSL_I! ^
  -DWASIGO_GOCVM=1 !WASIGO_CHROME_D! !WASIGO_VOODOO_D! !WASIGO_WOW_D! !WASIGO_SSL_D! -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_GETPID ^
  -lwasi-emulated-mman ^
  -o "%OUTWASM%" "%GENCPP%" "%WASIGO_ROOT%\libc\goclibc.c" !WASIGO_WIN32_CC! !WASIGO_NIX_CC! !WASIGO_DROID_CC! !WASIGO_GOCOS_CC! !WASIGO_CHROME_CC! !WASIGO_VOODOO_CC! !WASIGO_WOW_CC! !WASIGO_SAFE_CC! !WASIGO_V8_CC! !WASIGO_SSL_L!
if errorlevel 1 (
  echo wasigocvm clang++ failed
  exit /b 1
)
echo.
echo wasm: %OUTWASM%
echo run:  wasitime "%OUTWASM%"
exit /b 0

:usage
echo usage: wasigocvm.bat ^<input.go^> [-o out.wasm] [wasigoc flags...]
echo.
echo Same machine as compile.bat ^(the default^). Full libc++, RTTI, WASM EH,
echo Oilpan + type_key, gocvm net. Prefers WASIGO_TOOLCHAIN / toolchain\sysroot.
echo Isolation is WASMSafeSpace in this module. No companion gocvm_host.
echo Stock wasip1 noeh leftover: legacy.bat
exit /b 0

:find_wasigoc
set "WASIGOC_EXE="
if exist "build\%CONFIG%\wasigoc.exe" set "WASIGOC_EXE=%cd%\build\%CONFIG%\wasigoc.exe"
if not defined WASIGOC_EXE if exist "build\wasigoc.exe" set "WASIGOC_EXE=%cd%\build\wasigoc.exe"
if not defined WASIGOC_EXE if exist "build\Release\wasigoc.exe" set "WASIGOC_EXE=%cd%\build\Release\wasigoc.exe"
if not defined WASIGOC_EXE if exist "build-fork\wasigoc.exe" set "WASIGOC_EXE=%cd%\build-fork\wasigoc.exe"
if not defined WASIGOC_EXE if exist "wasigoc.exe" set "WASIGOC_EXE=%cd%\wasigoc.exe"
exit /b 0

:find_toolchain
set "WASIGO_CLANG="
set "WASIGO_SYSROOT="
set "WASIGO_SDK_ROOT="
rem 1) Custom wasigocvm sysroot
if defined WASIGO_TOOLCHAIN if exist "%WASIGO_TOOLCHAIN%\sysroot\include" (
  set "WASIGO_SYSROOT=%WASIGO_TOOLCHAIN%\sysroot"
  set "WASIGO_SDK_ROOT=%WASIGO_TOOLCHAIN%"
)
if not defined WASIGO_SYSROOT if exist "%WASIGO_ROOT%\toolchain\sysroot\include" (
  set "WASIGO_SYSROOT=%WASIGO_ROOT%\toolchain\sysroot"
  set "WASIGO_SDK_ROOT=%WASIGO_ROOT%\toolchain"
)
rem share\wasi-sysroot layout from bootstrap install prefix
if not defined WASIGO_SYSROOT if defined WASIGO_TOOLCHAIN if exist "%WASIGO_TOOLCHAIN%\share\wasi-sysroot\include" (
  set "WASIGO_SYSROOT=%WASIGO_TOOLCHAIN%\share\wasi-sysroot"
  set "WASIGO_SDK_ROOT=%WASIGO_TOOLCHAIN%"
)
rem 2) Stock wasi-sdk borrow
if not defined WASIGO_SYSROOT if defined WASI_SDK_PATH if exist "%WASI_SDK_PATH%\share\wasi-sysroot\include" (
  set "WASIGO_SYSROOT=%WASI_SDK_PATH%\share\wasi-sysroot"
  set "WASIGO_SDK_ROOT=%WASI_SDK_PATH%"
)
if not defined WASIGO_SYSROOT if exist "%USERPROFILE%\wasi-sdk\share\wasi-sysroot\include" (
  set "WASIGO_SYSROOT=%USERPROFILE%\wasi-sdk\share\wasi-sysroot"
  set "WASIGO_SDK_ROOT=%USERPROFILE%\wasi-sdk"
)
if not defined WASIGO_SYSROOT exit /b 0
call :pick_clang "%WASIGO_SDK_ROOT%"
rem A bootstrap.sh --target install run (the default) has a real sysroot
rem but no bin\ of its own (see toolchain\README.md) -- pair that sysroot
rem with the stock wasi-sdk's clang++ instead of giving up.
if not defined WASIGO_CLANG if defined WASI_SDK_PATH (
  call :pick_clang "%WASI_SDK_PATH%"
  if defined WASIGO_CLANG echo [wasigocvm] pairing %WASIGO_SYSROOT% with clang++ from %WASI_SDK_PATH% ^(no bin\ of its own^)
)
if not defined WASIGO_CLANG if exist "%USERPROFILE%\wasi-sdk\bin" (
  call :pick_clang "%USERPROFILE%\wasi-sdk"
  if defined WASIGO_CLANG echo [wasigocvm] pairing %WASIGO_SYSROOT% with clang++ from %USERPROFILE%\wasi-sdk ^(no bin\ of its own^)
)
exit /b 0

:pick_clang
set "WASIGO_CLANG="
if exist "%~1\bin\wasm32-wasigocvm-clang++.exe" set "WASIGO_CLANG=%~1\bin\wasm32-wasigocvm-clang++.exe"
if not defined WASIGO_CLANG if exist "%~1\bin\wasm32-wasigocvm-clang++" set "WASIGO_CLANG=%~1\bin\wasm32-wasigocvm-clang++"
if not defined WASIGO_CLANG if exist "%~1\bin\wasm32-wasip2-clang++.exe" set "WASIGO_CLANG=%~1\bin\wasm32-wasip2-clang++.exe"
if not defined WASIGO_CLANG if exist "%~1\bin\wasm32-wasip2-clang++" set "WASIGO_CLANG=%~1\bin\wasm32-wasip2-clang++"
exit /b 0

:pick_triple
set "WASIGO_TRIPLE="
if exist "%WASIGO_SYSROOT%\include\wasm32-wasigocvm\eh\c++\v1\iostream" set "WASIGO_TRIPLE=wasm32-wasigocvm"
if not defined WASIGO_TRIPLE if exist "%WASIGO_SYSROOT%\include\wasm32-wasip2\eh\c++\v1\iostream" set "WASIGO_TRIPLE=wasm32-wasip2"
exit /b 0
