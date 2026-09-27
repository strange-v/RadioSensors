#!/usr/bin/env bash
# Builds and runs the native backup/recovery tests. Pinned Mbed TLS and
# ArduinoJson are downloaded once into a cache on the Linux filesystem, where
# compiling Mbed TLS is much faster than on a /mnt/c checkout.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
gateway_dir="$(cd "${script_dir}/.." && pwd)"
cd "${gateway_dir}"

mbedtls_version=3.6.3
mbedtls_sha256=64cd73842cdc05e101172f7b437c65e7312e476206e1dbfd644433d11bc56327
# Must match bblanchon/ArduinoJson in platformio.ini.
arduinojson_version=7.4.2
arduinojson_sha256=a05ac98f4481d2398c103ca5ffcce8e2fcd7fa2fa1d8d9f38d5757454f448d97

cache_dir="${XDG_CACHE_HOME:-${HOME}/.cache}/osk-sense-native"
output_dir=.pio/native-tests
mkdir -p "${cache_dir}" "${output_dir}"

if ! grep -Eq "bblanchon/ArduinoJson @ ${arduinojson_version}[[:space:]]*$" platformio.ini; then
    echo "ArduinoJson ${arduinojson_version} pinned here differs from platformio.ini; update this script." >&2
    exit 2
fi

download() {
    local url="$1" sha256="$2" target="$3"
    curl -fsSL -o "${target}.part" "${url}"
    if ! echo "${sha256}  ${target}.part" | sha256sum --check --status; then
        rm -f "${target}.part"
        echo "Checksum mismatch for ${url}" >&2
        exit 1
    fi
    mv "${target}.part" "${target}"
}

mbedtls_dir="${MBEDTLS_DIR:-${cache_dir}/mbedtls-${mbedtls_version}}"
if [[ ! -f "${mbedtls_dir}/library/libmbedcrypto.a" ]]; then
    if [[ -n "${MBEDTLS_DIR:-}" ]]; then
        echo "MBEDTLS_DIR has no library/libmbedcrypto.a: ${MBEDTLS_DIR}" >&2
        exit 2
    fi
    echo "Building Mbed TLS ${mbedtls_version} into ${cache_dir} (once)..."
    archive="${cache_dir}/mbedtls-${mbedtls_version}.tar.bz2"
    # The release asset, unlike a tag archive, contains the generated sources.
    download \
        "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${mbedtls_version}/mbedtls-${mbedtls_version}.tar.bz2" \
        "${mbedtls_sha256}" "${archive}"
    rm -rf "${mbedtls_dir}"
    tar -xjf "${archive}" -C "${cache_dir}"
    make -C "${mbedtls_dir}/library" -j"$(nproc)" libmbedcrypto.a >/dev/null
fi

json_dir="${cache_dir}/arduinojson-${arduinojson_version}"
if [[ ! -f "${json_dir}/ArduinoJson.h" ]]; then
    mkdir -p "${json_dir}"
    download \
        "https://github.com/bblanchon/ArduinoJson/releases/download/v${arduinojson_version}/ArduinoJson-v${arduinojson_version}.h" \
        "${arduinojson_sha256}" "${json_dir}/ArduinoJson.h"
fi

g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Itest/native/stubs -Iinclude -I../shared/RadioProtocol/include \
    -I"${json_dir}" -I"${mbedtls_dir}/include" \
    test/native/test_backup.cpp \
    src/BackupCodec.cpp src/BackupCrypto.cpp src/BackupService.cpp src/RecoveryService.cpp \
    ../shared/RadioProtocol/src/{GatewayStorage,NodeRegistry,RegistryPersistence}.cpp \
    "${mbedtls_dir}/library/libmbedcrypto.a" -o "${output_dir}/backup-tests"

fixture="${output_dir}/test.oskbackup"
ASAN_OPTIONS=detect_leaks=0 "${output_dir}/backup-tests" "${fixture}"

# Decrypts the fixture with an independent implementation. Node.js on the
# Windows side is reachable from WSL as node.exe; the relative path works for both.
node_command="$(command -v node || command -v node.exe || true)"
if [[ -z "${node_command}" ]]; then
    echo "Node.js is required for the interoperability check (test/native/check_interop.mjs)." >&2
    exit 1
fi
"${node_command}" test/native/check_interop.mjs "${fixture}"
