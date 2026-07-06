# Aliyun SLS Fluent Bit Output Plugin

[简体中文](README.md) | English

This is a Fluent Bit output plugin that sends logs collected by Fluent Bit to
Alibaba Cloud Simple Log Service (SLS).

If you are new to Fluent Bit, the data flow is:

```text
Application/files/stdout -> Fluent Bit -> aliyun_sls plugin -> Alibaba Cloud SLS Logstore
```

The plugin converts Fluent Bit records into SLS `LogGroup` payloads and handles
request signing, LZ4 compression, batch splitting, and retry/error result
mapping.

## Supported Versions

- Fluent Bit: `3.x` is recommended. The plugin targets the Fluent Bit 3.x
  output API.
- Compiler: C++17 support is required.
- OS: Linux and macOS are supported for builds. Linux is recommended for
  production runtime.

## Repository Layout

| Path | Description |
| --- | --- |
| `src/` | Plugin and SLS client source code |
| `include/aliyun_sls/` | Public C/C++ headers |
| `examples/fluent-bit.conf` | Fluent Bit configuration example |
| `examples/putlogs_smoke.cpp` | SLS write smoke test example |
| `tests/` | Unit tests |
| `third_party/lz4/` | Vendored LZ4 source code |
| `docs/docker-build.md` | Docker build example |

## Prepare SLS Information

You need the following SLS information before running the plugin:

| Item | Example | Where to find it |
| --- | --- | --- |
| `endpoint` | `cn-hangzhou.log.aliyuncs.com` | The SLS endpoint for your region |
| `project` | `your-project` | SLS Project name |
| `logstore` | `your-logstore` | SLS Logstore name |
| `access_key_id` | Do not write it into config files | RAM user or STS credential |
| `access_key_secret` | Do not write it into config files | RAM user or STS credential |

Use environment variables for AK/SK. Do not hard-code credentials in config
files or container images:

```sh
export ALIYUN_SLS_ACCESS_ID='your AccessKey ID'
export ALIYUN_SLS_ACCESS_KEY='your AccessKey Secret'
```

## Dependencies

### Required Dependencies

| Dependency | Purpose | Check command |
| --- | --- | --- |
| `git` | Clone Fluent Bit source code | `git --version` |
| `cmake >= 3.16` | Generate build files | `cmake --version` |
| C/C++ compiler | Build the plugin and SLS client | `cc --version`, `c++ --version` |
| `make` or `ninja` | Run builds | `make --version` or `ninja --version` |
| OpenSSL development library | MD5/HMAC signing on Linux | `pkg-config --modversion openssl` |
| libcurl development library | Local tests and smoke tool | `curl-config --version` |
| Fluent Bit source and build tree | Required headers for external `.so` builds | `test -f "$FLUENT_BIT_DIR/build/include/fluent-bit/flb_info.h" && echo ok` |

Notes:

- On macOS, signing uses the system CommonCrypto library, so OpenSSL is not
  required for the core crypto implementation.
- When running inside Fluent Bit, the plugin uses Fluent Bit's native network
  stack and does not depend on libcurl.
- LZ4 source code is vendored under `third_party/lz4`, so system `liblz4` is
  not required.

### Install Dependencies on Ubuntu/Debian

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git pkg-config \
  libssl-dev libcurl4-openssl-dev \
  flex bison libyaml-dev
```

Check the installation:

```sh
git --version
cmake --version
c++ --version
pkg-config --modversion openssl
curl-config --version
```

### Install Dependencies on CentOS/RHEL

```sh
sudo yum install -y \
  gcc gcc-c++ make git pkgconfig \
  openssl-devel libcurl-devel \
  flex bison libyaml-devel
```

If the system CMake version is older than 3.16, install `cmake3` or install a
newer CMake release from the official CMake website:

```sh
cmake --version
cmake3 --version
```

### Install Dependencies on macOS

```sh
xcode-select --install
brew install cmake git curl flex bison
```

Check the installation:

```sh
cmake --version
c++ --version
curl-config --version
```

## Build and Test Locally

First verify that the client library and unit tests build successfully:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To verify that logs can be written to SLS, run the smoke test:

```sh
export ALIYUN_SLS_ENDPOINT=cn-hangzhou.log.aliyuncs.com
export ALIYUN_SLS_ACCESS_ID='your AccessKey ID'
export ALIYUN_SLS_ACCESS_KEY='your AccessKey Secret'

cmake -S . -B build
cmake --build build --target putlogs_smoke
./build/putlogs_smoke
```

By default, `putlogs_smoke` creates a temporary project/logstore, writes one
log, and then cleans them up. To use existing resources:

```sh
export ALIYUN_SLS_PROJECT=your-project
export ALIYUN_SLS_LOGSTORE=your-logstore
./build/putlogs_smoke
```

## Integrate with Fluent Bit

The recommended integration method is to build an external `.so` plugin and
load it dynamically. This does not require modifying Fluent Bit source code.

### Option 1: Build an External `.so` Plugin

First, prepare and build Fluent Bit once. The example below uses `v3.2.10` and
places the source tree under the current user's home directory to avoid `/opt`
permission issues:

```sh
export FLUENT_BIT_DIR="$HOME/fluent-bit"

git clone --depth 1 --branch v3.2.10 \
  https://github.com/fluent/fluent-bit.git "$FLUENT_BIT_DIR"

cmake -S "$FLUENT_BIT_DIR" -B "$FLUENT_BIT_DIR/build" \
  -DFLB_RELEASE=On \
  -DFLB_OUT_PROMETHEUS_EXPORTER=Off \
  -DFLB_OUT_VIVO_EXPORTER=Off \
  -DFLB_IN_ELASTICSEARCH=Off \
  -DFLB_EXAMPLES=Off \
  -DFLB_TESTS_RUNTIME=Off \
  -DFLB_TESTS_INTERNAL=Off

cmake --build "$FLUENT_BIT_DIR/build" --parallel
```

This step mainly generates Fluent Bit headers, for example:

```sh
test -f "$FLUENT_BIT_DIR/build/include/fluent-bit/flb_info.h" && echo "Fluent Bit build ok"
```

Second, build this plugin:

```sh
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC="$FLUENT_BIT_DIR" \
  -DFLUENT_BIT_BUILD_DIR="$FLUENT_BIT_DIR/build"

cmake --build build-plugin --target flb-out_aliyun_sls --parallel
```

The build should generate:

```text
build-plugin/out_aliyun_sls.so
```

Check that the plugin symbol is exported:

```sh
nm -D build-plugin/out_aliyun_sls.so | grep out_aliyun_sls_plugin
```

Third, load the plugin with Fluent Bit:

```sh
export ALIYUN_SLS_ACCESS_ID='your AccessKey ID'
export ALIYUN_SLS_ACCESS_KEY='your AccessKey Secret'

fluent-bit -e build-plugin/out_aliyun_sls.so -c examples/fluent-bit.conf
```

### Option 2: Build In-tree

If you need to compile the plugin into the Fluent Bit binary, copy this
repository into the Fluent Bit plugin directory:

```sh
cp -a . /path/to/fluent-bit/plugins/out_aliyun_sls
```

Add these source files to the Fluent Bit build system:

```text
plugins/out_aliyun_sls/src/fluent_bit_out_aliyun_sls_plugin.c
plugins/out_aliyun_sls/src/sls_batch.cpp
plugins/out_aliyun_sls/src/sls_client.cpp
plugins/out_aliyun_sls/src/sls_compress.cpp
plugins/out_aliyun_sls/src/sls_crypto.cpp
plugins/out_aliyun_sls/src/sls_http.cpp
plugins/out_aliyun_sls/src/sls_protobuf.cpp
plugins/out_aliyun_sls/src/sls_c_api.cpp
plugins/out_aliyun_sls/third_party/lz4/lz4.c
```

Add these include paths:

```text
plugins/out_aliyun_sls/include
plugins/out_aliyun_sls/src
plugins/out_aliyun_sls/third_party/lz4
```

Define this compile macro:

```text
ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=1
```

On Linux, link OpenSSL crypto:

```text
OpenSSL::Crypto
```

Finally, register the output plugin in Fluent Bit's output plugin registry:

```c
extern struct flb_output_plugin out_aliyun_sls_plugin;
```

## Fluent Bit Configuration Example

`examples/fluent-bit.conf` is a minimal runnable example:

```ini
[SERVICE]
    flush        5
    log_level    info

[INPUT]
    name         dummy
    tag          app.demo
    dummy        {"message": "hello sls", "level": "info"}

[OUTPUT]
    name                     aliyun_sls
    match                    *
    endpoint                 cn-hangzhou.log.aliyuncs.com
    project                  your-project
    logstore                 your-logstore
    access_key_id            ${ALIYUN_SLS_ACCESS_ID}
    access_key_secret        ${ALIYUN_SLS_ACCESS_KEY}
    topic                    fluent-bit
    source                   edge-node-a
    max_raw_bytes_per_batch  5242880
    tls                      on
    tls.verify               on
    net.keepalive            on
    net.connect_timeout      10
    net.io_timeout           30
    workers                  2
    retry_limit              5
```

## Configuration Parameters

| Parameter | Source | Required | Default | Example | Description |
| --- | --- | --- | --- | --- | --- |
| `name` | Fluent Bit | Yes | None | `aliyun_sls` | Output plugin name. Must be `aliyun_sls`. |
| `match` | Fluent Bit | Yes | None | `*`, `app.*` | Selects which tags are sent to this output. |
| `endpoint` | This plugin | Yes | None | `cn-hangzhou.log.aliyuncs.com` | SLS endpoint. Do not include `https://`. |
| `project` | This plugin | Yes | None | `your-project` | SLS Project name. |
| `logstore` | This plugin | Yes | None | `your-logstore` | SLS Logstore name. |
| `access_key_id` | This plugin | Yes | None | `${ALIYUN_SLS_ACCESS_ID}` | AccessKey ID for SLS. Environment variables are recommended. |
| `access_key_secret` | This plugin | Yes | None | `${ALIYUN_SLS_ACCESS_KEY}` | AccessKey Secret for SLS. Environment variables are recommended. |
| `topic` | This plugin | No | Empty | `fluent-bit` | SLS LogGroup topic. |
| `source` | This plugin | No | Fluent Bit tag | `edge-node-a` | SLS LogGroup source. If omitted, the current record tag is used. |
| `hash_key` | This plugin | No | Empty | `user-123` | Enables SLS KeyHash routing to a shard. Most users can leave it unset. |
| `port` | This plugin | No | `0` | `443` | Overrides the endpoint port. `0` auto-selects 443 when TLS is on, otherwise 80. |
| `max_raw_bytes_per_batch` | This plugin | No | `5242880` | `5242880` | Maximum uncompressed bytes per PutLogs request. Default is 5 MB. |
| `tls` | Fluent Bit | No | `off` | `on` | Enables TLS. Recommended for public SLS endpoints. |
| `tls.verify` | Fluent Bit | No | `on` | `on` | Verifies TLS certificates. Keep it on in production. |
| `net.keepalive` | Fluent Bit | No | Fluent Bit default | `on` | Reuses network connections. Recommended. |
| `net.connect_timeout` | Fluent Bit | No | Fluent Bit default | `10` | Connection timeout in seconds. |
| `net.io_timeout` | Fluent Bit | No | Fluent Bit default | `30` | Read/write timeout in seconds. |
| `workers` | Fluent Bit | No | `0` | `2` | Number of flush workers. `0` means Fluent Bit default scheduling. |
| `retry_limit` | Fluent Bit | No | Fluent Bit default | `5` | Number of retries after write failures. |

## Field Mapping

When a Fluent Bit record is a map, each key/value pair is written as SLS log
contents.

Input example:

```json
{
  "level": "info",
  "message": "service started",
  "trace_id": "abc"
}
```

The SLS contents will look like:

```text
level=info
message=service started
trace_id=abc
```

Non-string values are converted to strings. Nested maps and arrays are written
as compact string values.

## FAQ

| Symptom | Possible cause | Solution |
| --- | --- | --- |
| `endpoint is required` | `endpoint` is missing | Check whether `[OUTPUT]` contains `endpoint`. |
| SLS returns `403` | Invalid AK/SK or insufficient permission | Check environment variables, RAM permissions, and Project/Logstore permissions. |
| SLS returns `404` | Project/Logstore does not exist or region does not match | Check that `endpoint`, `project`, and `logstore` belong to the same region. |
| SLS returns `400` | Invalid request parameter or log content | Check the Logstore name, field keys, and request size. |
| SLS returns `429` or `5xx` | Server throttling or temporary server error | Let Fluent Bit retry, or adjust `retry_limit`. |
| No logs are written | `match` does not match the input tag | Check the `[INPUT]` `tag` and `[OUTPUT]` `match`. |
| `flb_info.h` is missing | Fluent Bit was not built first | Run `cmake --build "$FLUENT_BIT_DIR/build"` first. |

## GitHub Actions Builds

This repository includes two workflows:

| Workflow | Trigger | Purpose |
| --- | --- | --- |
| `CI` | Pull requests and pushes to `master` | Builds and runs unit tests on `ubuntu-22.04` and `ubuntu-24.04`. |
| `Build Release Artifacts` | Manual dispatch and `v*` tags | Builds the Linux `out_aliyun_sls.so`, packages README files, example config, and sha256 checksum files, then uploads them as Actions artifacts. |

To build artifacts manually, open the GitHub Actions page, choose
`Build Release Artifacts`, and enter a Fluent Bit tag such as `v3.2.10`. After
the workflow finishes, download the package from the Artifacts section of the
workflow run.

## Security Recommendations

- Do not hard-code AK/SK in `fluent-bit.conf`, Dockerfiles, container images, or
  Git repositories.
- Use environment variables, Kubernetes Secret, a host secret management system,
  or temporary STS credentials.
- The plugin does not intentionally print AK/SK.
- Startup logs include endpoint, project, and logstore. If these resource names
  are also sensitive in your environment, control who can read runtime logs.

## More Documents

- Docker build example: [docs/docker-build.md](docs/docker-build.md)
- Customer guide: [docs/customer-guide.md](docs/customer-guide.md)
- Release notes: [docs/release-notes-v0.2.0.md](docs/release-notes-v0.2.0.md)
