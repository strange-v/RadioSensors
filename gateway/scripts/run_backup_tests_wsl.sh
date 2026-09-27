#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${MBEDTLS_DIR:?Set MBEDTLS_DIR to a built Mbed TLS 3.x source directory (library/libmbedcrypto.a)}"
json_dir=".pio/libdeps/gateway_waveshare_s3_eth/ArduinoJson/src"
output=/tmp/osk-backup-tests
mkdir -p "$output"
g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Itest/native/stubs -Iinclude -I../shared/RadioProtocol/include -I"$json_dir" -I"$MBEDTLS_DIR/include" \
    test/native/test_backup.cpp src/BackupCodec.cpp src/BackupCrypto.cpp src/BackupService.cpp src/RecoveryService.cpp \
    ../shared/RadioProtocol/src/{GatewayStorage,NodeRegistry,RegistryPersistence}.cpp \
    "$MBEDTLS_DIR/library/libmbedcrypto.a" -o "$output/backup-tests"
ASAN_OPTIONS=detect_leaks=0 "$output/backup-tests"
