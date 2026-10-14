#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas -g -O1 \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -Itests/user-mode/win32-stubs tests/user-mode/io_tests.cpp -o "$build_dir/io_tests"
"$build_dir/io_tests"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas -g -O1 \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -Itests/user-mode/win32-stubs -Icommon tests/user-mode/install_tests.cpp -o "$build_dir/install_tests"
"$build_dir/install_tests" "$build_dir/install-fixture"
