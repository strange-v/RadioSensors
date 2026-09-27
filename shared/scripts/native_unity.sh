# Sourced by the native test scripts: sets unity_dir to the sources of a
# pinned Unity, downloaded once into a cache on the Linux filesystem.

unity_version=2.6.1
unity_sha256=b41a66d45a6b99758fb3202ace6178177014d52fc524bf1f72687d93e9867292

unity_cache_dir="${XDG_CACHE_HOME:-${HOME}/.cache}/osk-sense-native"
unity_dir="${unity_cache_dir}/Unity-${unity_version}/src"

if [[ ! -f "${unity_dir}/unity.c" ]]; then
    mkdir -p "${unity_cache_dir}"
    unity_archive="${unity_cache_dir}/unity-${unity_version}.tar.gz"
    curl -fsSL -o "${unity_archive}.part" \
        "https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v${unity_version}.tar.gz"
    if ! echo "${unity_sha256}  ${unity_archive}.part" | sha256sum --check --status; then
        rm -f "${unity_archive}.part"
        echo "Checksum mismatch for Unity ${unity_version}" >&2
        exit 1
    fi
    mv "${unity_archive}.part" "${unity_archive}"
    tar -xzf "${unity_archive}" -C "${unity_cache_dir}"
fi
