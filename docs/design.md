# Aliyun SLS Fluent Bit Output

This package implements an Aliyun SLS client and a Fluent Bit output adapter.

## Current scope

- A C++17 `aliyun_sls::Client`.
- A C wrapper around the C++ client.
- Manual protobuf encoding for the SLS `LogGroup` write path.
- SLS request signing with `Content-MD5`, `x-log-bodyrawsize`, and `HMAC-SHA1`.
- LZ4 PutLogs request body compression.
- A default libcurl transport plus an injectable `Transport` interface.
- A Fluent Bit output adapter targeting the Fluent Bit 3.x plugin API, keeping the C ABI at the
  plugin boundary.

## Fluent Bit 3.x integration

The adapter is written against the current Fluent Bit output API:

- `cb_flush` uses the `flb_event_chunk` / `flb_output_flush` signature.
- Records are decoded with `flb_log_event_decoder`, so the 2.1+ record layout
  (`[[timestamp, metadata], body]`) and grouped chunks are handled by the engine rather than by a
  hand-rolled msgpack walk.
- Properties are declared through a `config_map`; unknown keys are validated and defaults documented.
- Networking is native: a shared `flb_upstream` connection pool plus `flb_http_client` per request.
  TLS, keepalive, connect/read timeouts and proxy come from the Fluent Bit instance config
  (`tls`, `net.*`), and the plugin declares `FLB_OUTPUT_NET | FLB_IO_OPT_TLS`.
- `workers` are supported: the upstream pool is shared and safe across flush workers.
- Custom `cmetrics` counters are registered: `fluentbit_output_sls_records_total` and
  `fluentbit_output_sls_requests_total{result=ok|retry|error}`.

The plugin registration struct lives in `fluent_bit_out_aliyun_sls_plugin.c` (C99 designated
initializers are order-independent, so they are resilient to the exact `flb_output_plugin` layout);
the C++ callbacks are exposed to it through C-linkage trampolines. The libcurl transport is retained
for the standalone client/tests and is not used inside Fluent Bit.

`aliyun/aliyun-log-cpp-sdk` is used as a protocol reference only. The client here owns its request
building and does not depend on that SDK.

LZ4 compression vendors `lz4` v1.10.0 source under `third_party/lz4`. Keep its license file with the
source when upgrading.

## Fluent Bit flush path

```text
Fluent Bit cb_flush (flb_event_chunk)
  -> flb_log_event_decoder
  -> map each event to LogItem (timestamp from flb_time, body map -> contents)
  -> split into LogGroup batches
  -> Client::putLogs (flb_upstream + flb_http_client)
  -> FLB_OK / FLB_RETRY / FLB_ERROR
```

Each event's timestamp comes from `flb_log_event.timestamp` (seconds + nanoseconds), so the adapter
no longer parses raw msgpack timestamp encodings. A non-map body is sent as the `message` field. If a
timestamp is missing or non-positive, the adapter uses the current time.

## Batch limits

The flush loop splits requests by estimated protobuf size:

- `max_raw_bytes_per_batch`, default `5242880` (5 MB)

If one single log is larger than the raw byte limit, it is still sent alone so Fluent Bit can receive
the real SLS response and apply the correct retry/error decision.

## Build

Client library, tests and examples (no Fluent Bit needed):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Fluent Bit output plugin (`out_aliyun_sls.so`), against a Fluent Bit >= 3.0 source tree that has
already been built once (so the generated `flb_info.h` exists):

```sh
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC=/path/to/fluent-bit \
  -DFLUENT_BIT_BUILD_DIR=/path/to/fluent-bit/build
cmake --build build-plugin --target flb-out_aliyun_sls
fluent-bit -e build-plugin/out_aliyun_sls.so -c examples/fluent-bit.conf
```

Alternatively, build it in-tree by copying `src/` into `fluent-bit/plugins/out_aliyun_sls/` and
registering the target with the Fluent Bit CMake.

## Example

```sh
export ALIYUN_SLS_ENDPOINT=cn-hangzhou.log.aliyuncs.com
export ALIYUN_SLS_ACCESS_ID=...
export ALIYUN_SLS_ACCESS_KEY=...
./build/putlogs_smoke
```
