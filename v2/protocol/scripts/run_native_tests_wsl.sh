#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
protocol_dir="$(cd "${script_dir}/.." && pwd)"
cd "${protocol_dir}"

unity_dir=".pio/libdeps/native/Unity/src"
if [[ ! -f "${unity_dir}/unity.c" ]]; then
    echo "Unity dependency is missing. Run 'platformio test -e native' once to let PlatformIO resolve test dependencies, then rerun this script." >&2
    exit 2
fi

common_sources=(
    ../shared/RadioProtocol/src/CommissioningFrames.cpp
    ../shared/RadioProtocol/src/NodeRegistry.cpp
    ../shared/RadioProtocol/src/RegistryPersistence.cpp
    ../shared/RadioProtocol/src/GatewayStorage.cpp
    ../shared/RadioProtocol/src/UserManagement.cpp
    "${unity_dir}/unity.c"
)
common_flags=(
    -std=c++17
    -I../shared/RadioProtocol/include
    -I"${unity_dir}"
)

run_suite() {
    local suite="$1"
    local output="/tmp/radiosensors_${suite}"
    g++ "${common_flags[@]}" "test/${suite}/test_main.cpp" \
        "${common_sources[@]}" -o "${output}"
    "${output}"
}

run_suite test_radio_protocol
run_suite test_node_registry
run_suite test_commissioning_frames
run_suite test_gateway_storage
