# Docker Build Guide

Build and test the plugin in Docker using any Linux image with GCC.

## Prerequisites

A Docker-capable host. No special base image required — any Linux distribution works.

## Quick Start

```sh
docker run --rm -it -v $(pwd):/src -w /src ubuntu:22.04 bash
```

Inside the container, install build dependencies and run the full build:

```sh
apt-get update && apt-get install -y \
  gcc g++ cmake make git \
  libssl-dev libcurl4-openssl-dev \
  flex bison libyaml-dev

# 1. Build & test the client library
cmake -S . -B build -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=OFF
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. Clone and build Fluent Bit (needed for plugin headers)
git clone --depth 1 --branch v3.2.10 \
  https://github.com/fluent/fluent-bit.git /opt/fluent-bit

cmake -S /opt/fluent-bit -B /opt/fluent-bit/build \
  -DFLB_RELEASE=On \
  -DFLB_OUT_PROMETHEUS_EXPORTER=Off \
  -DFLB_OUT_VIVO_EXPORTER=Off \
  -DFLB_IN_ELASTICSEARCH=Off \
  -DFLB_EXAMPLES=Off \
  -DFLB_TESTS_RUNTIME=Off \
  -DFLB_TESTS_INTERNAL=Off

cmake --build /opt/fluent-bit/build -j$(nproc)

# 3. Build the Fluent Bit plugin
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC=/opt/fluent-bit \
  -DFLUENT_BIT_BUILD_DIR=/opt/fluent-bit/build

cmake --build build-plugin --target flb-out_aliyun_sls -j$(nproc)

# Verify the plugin symbol is exported
nm -D build-plugin/out_aliyun_sls.so | grep out_aliyun_sls_plugin
```

## Verify

After step 1, all unit tests should pass:

```
test_protobuf  - SLS LogGroup protobuf encoding
test_crypto    - HMAC-SHA1, MD5, signature
test_client    - Client request construction
test_batch     - LogGroup batch splitting
```

After step 3, `out_aliyun_sls.so` should contain the plugin registration symbol:

```
T out_aliyun_sls_plugin
```

## Notes

- Fluent Bit's full build is only needed to generate headers (`flb_info.h`, `cfl_info.h`, etc.).
  The resulting fluent-bit binary is not used.
- The plugin `.so` links statically against `aliyun_sls_client`, so at runtime it only needs
  `libssl` and standard C/C++ libraries (no libcurl inside Fluent Bit).
- To use a different Fluent Bit version, change the `--branch` tag. Any 3.x release should work.
- On CentOS/RHEL, replace `apt-get` with `yum install gcc gcc-c++ cmake3 make git openssl-devel
  libcurl-devel flex bison libyaml-devel`.
- Disabled Fluent Bit plugins (`PROMETHEUS_EXPORTER`, `VIVO_EXPORTER`, `IN_ELASTICSEARCH`) avoid
  pulling optional dependencies; they are not needed for building the plugin.
