#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build_dir="${BUILD_DIR:-$(mktemp -d)}"
mkdir -p "$build_dir"
compiler="${CXX:-g++}"
for flags in '-O2' '-O2 -ffast-math'; do
  "$compiler" -Icommon -std=c++17 $flags -Wall -Wextra -Werror -fsanitize=undefined,float-cast-overflow \
    -fno-sanitize-recover=all tests/native/input_tests.cpp -o "$build_dir/input_tests"
  "$build_dir/input_tests"
done
