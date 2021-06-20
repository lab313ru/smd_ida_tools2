@echo off

protoc -I ./proto --grpc_out=./proto --plugin=protoc-gen-grpc=c:\dev\vcpkg\packages\grpc_x64-windows\tools\grpc\grpc_cpp_plugin.exe ./proto/debug_proto_z80.proto
protoc -I ./proto --cpp_out=./proto ./proto/debug_proto_z80.proto

pause
