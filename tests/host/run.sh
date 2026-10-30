#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build-host
g++ -std=c++17 -g -O1 -Wall -Wextra -Wno-unused-variable -Wno-misleading-indentation -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I tests/host/stubs -I main -I /usr/include/cjson '-DCONFIG_FILE_PATH="test-config.json"' \
  tests/host/test_main.cpp main/core/config.cpp -lcjson -lmbedcrypto -o build-host/test-core
ASAN_OPTIONS=detect_leaks=0 ./build-host/test-core
