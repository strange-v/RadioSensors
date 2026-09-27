#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
protocol_dir="$(cd "${script_dir}/.." && pwd)"
cd "${protocol_dir}"

source ../shared/scripts/native_unity.sh

common_sources=(
    ../shared/RadioProtocol/src/CommissioningFrames.cpp
    ../shared/RadioProtocol/src/NodeRegistry.cpp
    ../shared/RadioProtocol/src/RegistryPersistence.cpp
    ../shared/RadioProtocol/src/GatewayStorage.cpp
    ../shared/RadioProtocol/src/UserManagement.cpp
    ../shared/RadioProtocol/src/CommandBook.cpp
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
run_suite test_command_book
run_suite test_radio_power
run_suite test_radio_aes
