@echo off

.\vcpkg_installed\x64-windows-static\x64-windows-static\tools\protobuf\protoc -I ./proto --grpc_out=./proto --plugin=protoc-gen-grpc=.\vcpkg_installed\x64-windows-static\x64-windows\tools\grpc\grpc_cpp_plugin.exe ./proto/debug_proto_z80.proto
.\vcpkg_installed\x64-windows-static\x64-windows-static\tools\protobuf\protoc -I ./proto --cpp_out=./proto ./proto/debug_proto_z80.proto

pause
