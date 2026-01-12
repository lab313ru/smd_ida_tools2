#!/bin/bash

export IDA_SDK=~/ida-sdk/src

mkdir -p ./linux_gensida
GRPCXX_FLAGS=`pkg-config --cflags --static protobuf grpc++`
GRPCXX_LDFLAGS=`pkg-config --libs --static protobuf grpc++`

$IDA_SDK/build/qt-install/libexec/moc ./Gensida/ida/paintform.h -o ./Gensida/ida/paintform_moc.cpp
g++ -D__LINUX__ -D__IDP__ -D__EA64__ -D__X64__ -DDEBUG_68K -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NAMESPACE=QT -DQT_THREAD_SUPPORT -DQT_WIDGETS_LIB -shared -O3 -fPIC -Wno-format -o ./linux_gensida/gensida_m68k.so -I./ -I$IDA_SDK/include/ $GRPCXX_FLAGS -I$IDA_SDK/build/qt-install/include/ -L$IDA_SDK/lib/x64_linux_gcc_64/ -L$IDA_SDK/build/qt-install/lib ./Gensida/ida/ida_debug.cpp ./Gensida/ida/ida_plugin.cpp ./Gensida/ida/paintform_moc.cpp ./Gensida/ida/paintform.cpp ./proto/debug_proto_68k.grpc.pb.cc ./proto/debug_proto_68k.pb.cc -lida $GRPCXX_LDFLAGS -lQt6Core -lQt6Gui -lQt6Widgets
g++ -D__LINUX__ -D__IDP__ -D__EA64__ -D__X64__ -DDEBUG_Z80                                                                                   -shared -O3 -fPIC -Wno-format -o ./linux_gensida/gensida_z80.so  -I./ -I$IDA_SDK/include/ $GRPCXX_FLAGS                                      -L$IDA_SDK/lib/x64_linux_gcc_64/                                 ./Gensida/ida/ida_debug.cpp ./Gensida/ida/ida_plugin.cpp                                                             ./proto/debug_proto_z80.grpc.pb.cc ./proto/debug_proto_z80.pb.cc -lida $GRPCXX_LDFLAGS
