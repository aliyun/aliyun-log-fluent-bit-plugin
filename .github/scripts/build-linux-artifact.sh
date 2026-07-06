#!/usr/bin/env bash
set -euo pipefail

target="${TARGET:?TARGET is required}"
package_manager="${PACKAGE_MANAGER:?PACKAGE_MANAGER is required}"
arch="${ARCH:?ARCH is required}"
fluent_bit_version="${FLUENT_BIT_VERSION:?FLUENT_BIT_VERSION is required}"
commit_sha="${GITHUB_SHA:?GITHUB_SHA is required}"

export DEBIAN_FRONTEND=noninteractive

restore_workspace_owner() {
    if [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
        chown -R "${HOST_UID}:${HOST_GID}" dist build-plugin 2>/dev/null || true
    fi
}
trap restore_workspace_owner EXIT

if [ "$package_manager" = "apt" ]; then
    apt-get update
    apt-get install -y \
        build-essential cmake git pkg-config ca-certificates \
        libssl-dev libcurl4-openssl-dev \
        flex bison libyaml-dev \
        binutils file tar gzip
else
    yum install -y \
        gcc gcc-c++ make git pkgconfig ca-certificates \
        openssl-devel libcurl-devel \
        flex bison libyaml-devel \
        binutils file tar gzip

    if [ -f /opt/rh/devtoolset-10/enable ]; then
        # manylinux2014 provides a newer GCC through devtoolset while keeping
        # the CentOS 7 / glibc 2.17 runtime baseline.
        # shellcheck disable=SC1091
        set +u
        source /opt/rh/devtoolset-10/enable
        set -u
    fi
fi

cmake --version
git --version
openssl version
cc --version | head -n 1
c++ --version | head -n 1

cmake_version="$(cmake --version | awk 'NR == 1 { print $3 }')"
cmake_major="${cmake_version%%.*}"
cmake_policy_args=()
if [ "${cmake_major:-0}" -ge 4 ]; then
    cmake_policy_args+=("-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
fi

fluent_bit_dir="/tmp/fluent-bit"
artifact="aliyun-log-fluent-bit-plugin-${target}"
package_dir="dist/${artifact}"

rm -rf "$fluent_bit_dir" build-plugin "$package_dir" \
    "dist/${artifact}.tar.gz" "dist/${artifact}.sha256"
mkdir -p dist

git clone --depth 1 --branch "$fluent_bit_version" \
    https://github.com/fluent/fluent-bit.git "$fluent_bit_dir"

cmake -S "$fluent_bit_dir" -B "$fluent_bit_dir/build" \
    "${cmake_policy_args[@]}" \
    -DFLB_RELEASE=On \
    -DFLB_OUT_PROMETHEUS_EXPORTER=Off \
    -DFLB_OUT_VIVO_EXPORTER=Off \
    -DFLB_IN_ELASTICSEARCH=Off \
    -DFLB_EXAMPLES=Off \
    -DFLB_TESTS_RUNTIME=Off \
    -DFLB_TESTS_INTERNAL=Off

cmake --build "$fluent_bit_dir/build" --parallel

test -f "$fluent_bit_dir/include/fluent-bit/flb_info.h"

cmake -S . -B build-plugin \
    -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
    -DALIYUN_SLS_BUILD_TESTS=OFF \
    -DALIYUN_SLS_BUILD_EXAMPLES=OFF \
    -DFLUENT_BIT_SRC="$fluent_bit_dir" \
    -DFLUENT_BIT_BUILD_DIR="$fluent_bit_dir/build"

cmake --build build-plugin --target flb-out_aliyun_sls --parallel
nm -D build-plugin/out_aliyun_sls.so | grep out_aliyun_sls_plugin

mkdir -p "$package_dir"
cp build-plugin/out_aliyun_sls.so "$package_dir/"
cp README.md README_EN.md "$package_dir/"
cp examples/fluent-bit.conf "$package_dir/"
cp docs/docker-build.md "$package_dir/"

{
    echo "name=${artifact}"
    echo "target=${target}"
    echo "arch=${arch}"
    echo "fluent_bit_version=${fluent_bit_version}"
    echo "commit=${commit_sha}"
    echo
    cmake --version | head -n 1
    git --version
    openssl version
    ldd --version | head -n 1 || true
    cc --version | head -n 1
    c++ --version | head -n 1
} > "$package_dir/manifest.txt"

ldd build-plugin/out_aliyun_sls.so > "$package_dir/ldd.txt" 2>&1 || true
readelf -d build-plugin/out_aliyun_sls.so > "$package_dir/readelf-dynamic.txt"
readelf --version-info build-plugin/out_aliyun_sls.so \
    > "$package_dir/readelf-version-info.txt" 2>&1 || true

sha256sum "$package_dir/out_aliyun_sls.so" > "$package_dir/out_aliyun_sls.so.sha256"
tar -C dist -czf "dist/${artifact}.tar.gz" "$artifact"
sha256sum "dist/${artifact}.tar.gz" > "dist/${artifact}.sha256"
