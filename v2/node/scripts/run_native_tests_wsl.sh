#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
node_dir="$(cd "${script_dir}/.." && pwd)"
protocol_dir="$(cd "${node_dir}/../protocol" && pwd)"
cd "${node_dir}"

unity_dir="${protocol_dir}/.pio/libdeps/native/Unity/src"
if [[ ! -f "${unity_dir}/unity.c" ]]; then
    echo "Unity dependency is missing. Resolve the v2/protocol native test dependencies first." >&2
    exit 2
fi

output="/tmp/radiosensors_test_node_storage"
g++ -std=c++17 -Ilib/NodeCore/include -I"${unity_dir}" \
    test/test_node_storage/test_main.cpp "${unity_dir}/unity.c" -o "${output}"
"${output}"
