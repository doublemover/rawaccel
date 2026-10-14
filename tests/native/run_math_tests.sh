#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build_dir="${BUILD_DIR:-$(mktemp -d)}"
mkdir -p "$build_dir"
compiler="${CXX:-g++}"
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined,float-cast-overflow \
    -fno-sanitize-recover=all tests/native/math_tests.cpp -o "$build_dir/math_tests"
"$build_dir/math_tests"
"$compiler" -std=c++17 -O2 -fshort-wchar -fsanitize=undefined,float-cast-overflow \
    -fno-sanitize-recover=all tests/native/math_tests.cpp -o "$build_dir/math_tests_windows_wchar"
"$build_dir/math_tests_windows_wchar"
"$compiler" -std=c++17 -O2 -ffast-math -fshort-wchar -fsanitize=undefined,float-cast-overflow \
    -fno-sanitize-recover=all tests/native/math_tests.cpp -o "$build_dir/math_tests_fast"
"$build_dir/math_tests_fast"
