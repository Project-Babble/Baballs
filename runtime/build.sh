#!/bin/sh
set -e
cd $(dirname $0)
mkdir -p ../StereoBalls/bin/Debug/net9.0/linux-x64
clang -std=gnu23 -fvisibility=hidden -shared -fPIC -Wl,--no-undefined -L$HOME/.nuget/packages/stereokit/0.3.10/runtimes/linux-x64/native -lStereoKitC \
	`pkg-config --cflags --libs libpipewire-0.3 libonnxruntime` -o ../StereoBalls/bin/Debug/net9.0/linux-x64/libruntime.so *.c -Werror -fdiagnostics-absolute-paths
