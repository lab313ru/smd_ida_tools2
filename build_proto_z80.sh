#!/bin/bash

protoc -I ./proto --grpc_out=./proto --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin ./proto/debug_proto_z80.proto
protoc -I ./proto --cpp_out=./proto ./proto/debug_proto_z80.proto

