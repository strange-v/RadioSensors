#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
node_dir="$(cd "${script_dir}/.." && pwd)"
cd "${node_dir}"

source ../shared/scripts/native_unity.sh

output="/tmp/radiosensors_test_node_storage"
g++ -std=c++17 -Ilib/NodeCore/include -I"${unity_dir}" \
    test/test_node_storage/test_main.cpp "${unity_dir}/unity.c" -o "${output}"
"${output}"
