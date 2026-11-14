@echo off
setlocal
rem Arguments: output directory, protoc.exe, grpc_cpp_plugin.exe.
rem All defaults are relative to this script, not the caller's working directory.
set "ROOT=%~dp0."
set "CPU=z80"
set "OUTPUT=%~1"
if not defined OUTPUT set "OUTPUT=%ROOT%\proto"
if not "%~2"=="" set "PROTOC=%~2"
if not "%~3"=="" set "GRPC_CPP_PLUGIN=%~3"
if not defined PROTOC set "PROTOC=%ROOT%\vcpkg_installed\x64-windows-static\x64-windows-static\tools\protobuf\protoc.exe"
if not defined GRPC_CPP_PLUGIN set "GRPC_CPP_PLUGIN=%ROOT%\vcpkg_installed\x64-windows-static\x64-windows\tools\grpc\grpc_cpp_plugin.exe"
if not exist "%PROTOC%" (
  echo ERROR: protoc.exe not found at "%PROTOC%". Install vcpkg dependencies or set PROTOC. 1>&2
  exit /b 1
)
if not exist "%GRPC_CPP_PLUGIN%" (
  echo ERROR: grpc_cpp_plugin.exe not found at "%GRPC_CPP_PLUGIN%". Install vcpkg dependencies or set GRPC_CPP_PLUGIN. 1>&2
  exit /b 1
)
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
if not exist "%OUTPUT%" exit /b 1
"%PROTOC%" --proto_path="%ROOT%\proto" --cpp_out="%OUTPUT%" --grpc_out="%OUTPUT%" --plugin=protoc-gen-grpc="%GRPC_CPP_PLUGIN%" "%ROOT%\proto\debug_proto_%CPU%.proto"
exit /b %errorlevel%
