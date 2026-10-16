#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
flags=(-O1 -g -fsanitize=address,undefined)
cppflags=(-std=c++20 -Wall -Wextra -Werror "${flags[@]}" -Isrc)
python3 tools/verify-forwarder-template.py
${CC:-cc} -std=c11 "${flags[@]}" -c src/vendor/cJSON.c -o build/host-tests/forwarder-cJSON.o
${CC:-cc} -std=c99 -D_DEFAULT_SOURCE "${flags[@]}" -Ivendor/ps5-forwarder-format/include \
    -c vendor/ps5-forwarder-format/src/psfwd.c -o build/host-tests/psfwd.o
${CXX:-c++} "${cppflags[@]}" tests/launch_args_test.cpp src/forwarder/launch_args.cpp -o build/host-tests/launch-args-test
build/host-tests/launch-args-test
${CXX:-c++} "${cppflags[@]}" tests/forward_launch_test.cpp tests/host_entropy.cpp src/random.cpp src/cloud.cpp src/gfn.cpp build/host-tests/forwarder-cJSON.o -o build/host-tests/forward-launch-test
build/host-tests/forward-launch-test
${CXX:-c++} -std=c++20 "${flags[@]}" -Isrc -ffunction-sections -fdata-sections -DOPENNOW_HOST_PREVIEW -DOPENNOW_PS5=1 \
    -Wl,--gc-sections tests/forward_launch_worker_test.cpp tests/host_entropy.cpp src/random.cpp src/cloud.cpp src/gfn.cpp src/forwarder/launch_args.cpp \
    build/host-tests/forwarder-cJSON.o -pthread -o build/host-tests/forward-launch-worker-test
build/host-tests/forward-launch-worker-test
${CXX:-c++} "${cppflags[@]}" tests/forwarder_image_test.cpp src/forwarder/image.cpp -o build/host-tests/forwarder-image-test
build/host-tests/forwarder-image-test
${CXX:-c++} "${cppflags[@]}" tests/forwarder_test.cpp src/forwarder/forwarder.cpp src/forwarder/image.cpp \
    src/forwarder/launch_args.cpp build/host-tests/forwarder-cJSON.o build/host-tests/psfwd.o -lcrypto -pthread -o build/host-tests/forwarder-test
build/host-tests/forwarder-test
