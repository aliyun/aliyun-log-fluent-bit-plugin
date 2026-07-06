# Aliyun SLS Fluent Bit Plugin

Aliyun SLS output plugin implementation for Fluent Bit, backed by a small C++17 SLS client and
a C wrapper. The `aliyun/aliyun-log-cpp-sdk` repository is used as a protocol reference only; this
code builds its own requests and does not depend on the SDK.

## What is included

- `aliyun_sls::Client`: PutLogs client plus project/logstore helpers.
- `aliyun_sls/sls_c_api.h`: opaque C wrapper for C callers and Fluent Bit adapter code.
- Manual SLS `LogGroup` protobuf encoder.
- SLS request signing with `Content-MD5`, `x-log-bodyrawsize`, `x-log-signaturemethod`, and
  `Authorization`.
- LZ4 PutLogs request body compression from vendored `third_party/lz4` source.
- Injectable transport interface with two backends: a standalone libcurl transport (used by the
  client library, tests and the smoke tool) and a native `flb_upstream` + `flb_http_client`
  transport used inside Fluent Bit.
- Fluent Bit 3.x output adapter in `src/fluent_bit_out_aliyun_sls_plugin.c` (pure C):
  `flb_event_chunk` flush, `flb_log_event_decoder` decoding, `config_map` properties, `cmetrics`
  counters, native `flb_upstream` transport, and retry/error mapping.
- Unit tests for protobuf encoding, crypto helpers, client request construction, and batching.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On macOS, crypto uses CommonCrypto. On Linux, crypto uses OpenSSL.

LZ4 compression is built from vendored source under `third_party/lz4`; no system liblz4 runtime or
development package is required.

## Smoke test

`putlogs_smoke` can create a temporary project and logstore, write one log, and clean them up:

```sh
export ALIYUN_SLS_ENDPOINT=cn-hangzhou.log.aliyuncs.com
export ALIYUN_SLS_ACCESS_ID=...
export ALIYUN_SLS_ACCESS_KEY=...

./build/putlogs_smoke
```

PutLogs always uses LZ4 compression. Set `ALIYUN_SLS_PROJECT` and `ALIYUN_SLS_LOGSTORE` to use
existing resources instead of creating temporary ones.

## Fluent Bit Plugin

Targets the Fluent Bit >= 3.0 output API. Build it as a shared object against a Fluent Bit source
tree that has already been built once (so `flb_info.h` exists):

```sh
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC=/path/to/fluent-bit \
  -DFLUENT_BIT_BUILD_DIR=/path/to/fluent-bit/build
cmake --build build-plugin --target flb-out_aliyun_sls
fluent-bit -e build-plugin/out_aliyun_sls.so -c examples/fluent-bit.conf
```

To build in-tree instead, copy `src/` into `plugins/out_aliyun_sls/`, add the sources plus
`third_party/lz4/lz4.c` to the Fluent Bit target with `ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=1`, and
register `out_aliyun_sls_plugin` in the in-tree plugin list.

Networking (TLS, keepalive, timeouts, proxy) and retries are handled by Fluent Bit's own stack, so
use the standard `tls`, `net.*`, `workers` and `retry_limit` properties. Custom metrics
`fluentbit_output_sls_records_total` and `fluentbit_output_sls_requests_total{result=...}` are
exported via the metrics endpoint.

Supported output properties (see `examples/fluent-bit.conf` for a full sample):

```ini
[OUTPUT]
    name                     aliyun_sls
    match                    *
    endpoint                 cn-hangzhou.log.aliyuncs.com
    project                  your-project
    logstore                 your-logstore
    access_key_id            ${ALIYUN_SLS_ACCESS_ID}
    access_key_secret        ${ALIYUN_SLS_ACCESS_KEY}
    topic                    fluent-bit
    source                   edge-node-a          # defaults to the record tag
    hash_key                 optional-route-key   # enables KeyHash write mode
    port                     0                    # 0 -> 443 with TLS, 80 without
    max_raw_bytes_per_batch  5242880
    tls                      on
    workers                  2
```

## Customer documentation and release

- Customer guide: `docs/customer-guide.md`
- Release notes: `docs/release-notes-v0.1.0.md`

Create formal release artifacts from a clean commit:

```sh
scripts/package_release.sh v0.1.0
```
