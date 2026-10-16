#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
bash tools/setup-native-dependencies.sh >/dev/null
python3 tools/verify-forwarder-template.py
sdk="$PWD/.deps/native/ps5-payload-sdk"
output="$PWD/build/forwarder/opennow-forwarder-elevator.elf"
mkdir -p "$(dirname "$output")"
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang++" -std=c++20 -O2 -g -Wall -Wextra -Werror \
    '-DELEVATION_TARGET_TITLE_ID="PPSA99082"' -Wl,--build-id=none \
    examples/sandbox-elevation/payload/main.cpp -o "$output"
python3 tools/validate-elevation-helper.py "$output"
