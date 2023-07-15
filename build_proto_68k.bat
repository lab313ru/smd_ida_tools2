@echo off

f:\dev\vcpkg\installed\x64-windows-static\tools\protobuf\protoc -I ./proto --grpc_out=./proto --plugin=protoc-gen-grpc=f:\dev\vcpkg\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe ./proto/debug_proto_68k.proto
f:\dev\vcpkg\installed\x64-windows-static\tools\protobuf\protoc -I ./proto --cpp_out=./proto ./proto/debug_proto_68k.proto

pause
