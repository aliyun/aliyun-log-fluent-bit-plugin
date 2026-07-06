# Aliyun SLS Fluent Bit 输出插件客户使用文档

版本：v0.2.0

发布日期：2026-07-03

## 概述

Aliyun SLS Fluent Bit 输出插件用于将 Fluent Bit 采集到的日志写入阿里云日志服务 SLS。
插件内置一个轻量 C++ SLS client，负责 `PutLogs` 请求构造、签名、LZ4 压缩、批量切分和
错误重试语义映射。

插件支持两种集成方式：

1. **外部动态加载（推荐）**：构建 `out_aliyun_sls.so`，通过 `fluent-bit -e` 加载。
2. **In-tree 编译**：将源码复制到 Fluent Bit 插件目录，编译进二进制。

## 能力范围

- 写入 SLS `PutLogs`。
- `PutLogs` 请求体固定使用 LZ4 压缩。
- 支持 AK/SK 认证。
- 使用 Fluent Bit 原生 `flb_upstream` + `flb_http_client` 网络栈（TLS、keepalive、连接池）。
- 通过 `flb_log_event_decoder` 解码记录，支持 Fluent Bit 2.1+ 和 3.x 事件格式。
- 支持常见 Fluent Bit record 形态：
  - `[timestamp, map]`
  - `[timestamp, metadata, map]`
  - 非 map payload 会写入 `message` 字段
- 支持按 protobuf 估计大小切分批次：`max_raw_bytes_per_batch`，默认 `5242880`（5MB）。
- 支持按 tag 自动填充 `source`，也可以通过配置固定。
- 支持通过 `hash_key` 走 SLS shard route 写入。
- 支持 `workers` 多 flush 线程。
- 自定义 cmetrics 计数器：`fluentbit_output_sls_records_total`、
  `fluentbit_output_sls_requests_total{result=ok|retry|error}`。

## 依赖要求

构建环境需要：

- C++17 编译器（GCC 7+ 或 Clang 5+）。
- CMake >= 3.16。
- OpenSSL crypto 库。
- Fluent Bit >= 3.0 源码树（仅需头文件，运行时不依赖 libcurl）。

插件已经 vendored LZ4 源码在 `third_party/lz4`，不需要系统安装 liblz4。

## 构建方式一：外部 .so（推荐）

```sh
# 1. 构建 Fluent Bit（生成所需头文件）
git clone --depth 1 --branch v3.2.10 https://github.com/fluent/fluent-bit.git /opt/fluent-bit
cmake -S /opt/fluent-bit -B /opt/fluent-bit/build \
  -DFLB_RELEASE=On \
  -DFLB_OUT_PROMETHEUS_EXPORTER=Off \
  -DFLB_OUT_VIVO_EXPORTER=Off \
  -DFLB_IN_ELASTICSEARCH=Off
cmake --build /opt/fluent-bit/build -j$(nproc)

# 2. 构建插件
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC=/opt/fluent-bit \
  -DFLUENT_BIT_BUILD_DIR=/opt/fluent-bit/build
cmake --build build-plugin --target flb-out_aliyun_sls -j$(nproc)

# 3. 验证
nm -D build-plugin/out_aliyun_sls.so | grep out_aliyun_sls_plugin

# 4. 运行
fluent-bit -e build-plugin/out_aliyun_sls.so -c examples/fluent-bit.conf
```

完整的 Docker 构建步骤见 `docs/docker-build.md`。

## 构建方式二：In-tree 编译

将源码复制到 Fluent Bit 插件目录：

```sh
cp -a . /path/to/fluent-bit/plugins/out_aliyun_sls
```

在 Fluent Bit 构建系统中加入以下源码：

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

需要加入 include 路径：

```text
plugins/out_aliyun_sls/include
plugins/out_aliyun_sls/src
plugins/out_aliyun_sls/third_party/lz4
```

需要定义编译宏：

```text
ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=1
```

需要链接：

```text
OpenSSL::Crypto
```

在 Fluent Bit in-tree plugin 注册列表中注册：

```c
extern struct flb_output_plugin out_aliyun_sls_plugin;
```

## Fluent Bit 配置

示例：

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
    source                   edge-node-a
    # hash_key               optional-route-key

    # Batching (default shown)
    max_raw_bytes_per_batch  5242880

    # Networking — handled by Fluent Bit native stack
    tls                      on
    tls.verify               on
    net.keepalive            on
    net.connect_timeout      10
    net.io_timeout           30

    # Parallelism
    workers                  2
    retry_limit              5
```

配置项说明：

| 配置项 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `endpoint` | 是 | 无 | SLS endpoint，例如 `cn-hangzhou.log.aliyuncs.com` |
| `project` | 是 | 无 | SLS project |
| `logstore` | 是 | 无 | SLS logstore |
| `access_key_id` | 是 | 无 | 访问 SLS 的 AccessKey ID |
| `access_key_secret` | 是 | 无 | 访问 SLS 的 AccessKey Secret |
| `topic` | 否 | 空 | SLS LogGroup topic |
| `source` | 否 | Fluent Bit tag | SLS LogGroup source |
| `hash_key` | 否 | 空 | 配置后使用 SLS shard route 写入 |
| `port` | 否 | 443 (TLS) / 80 | 覆盖 endpoint 端口 |
| `max_raw_bytes_per_batch` | 否 | `5242880` | 单个 PutLogs 请求压缩前最大字节数 |
| `tls` | 否 | `off` | 启用 TLS（推荐 `on`） |
| `workers` | 否 | `0` | flush 并发数，0 表示使用默认引擎线程 |
| `retry_limit` | 否 | `1` | Fluent Bit 重试次数 |

超时通过 Fluent Bit 原生网络配置控制：`net.connect_timeout`、`net.io_timeout`。

说明：

- `PutLogs` 请求体固定使用 LZ4 压缩，没有非压缩配置项。
- 不要在配置文件中明文写 AK/SK，建议通过环境变量或密钥管理系统注入。
- 如果单条日志超过 `max_raw_bytes_per_batch`，插件仍会发送（单条独立成批），由 SLS 返回真实结果。

## 写入字段映射

Fluent Bit record payload 是 map 时，map 的 key/value 会转换为 SLS log contents。

示例 Fluent Bit record：

```json
{
  "level": "info",
  "message": "service started",
  "trace_id": "abc"
}
```

写入 SLS 后 contents：

```text
level=info
message=service started
trace_id=abc
```

非字符串类型会转换为字符串；嵌套 map/array 会以紧凑字符串形式写入。

## 重试语义

插件根据 SLS client 返回结果映射 Fluent Bit flush 结果：

| SLS client 结果 | Fluent Bit 结果 | 说明 |
| --- | --- | --- |
| 2xx | `FLB_OK` | 写入成功 |
| 408/429/5xx 或传输异常 | `FLB_RETRY` | 交给 Fluent Bit 重试 |
| 其他 4xx 或请求构造失败 | `FLB_ERROR` | 不再重试 |

**At-least-once 语义**：如果一个 chunk 被切分为多个 batch，前面的 batch 成功但后续
batch 需要重试时，Fluent Bit 会重试整个 chunk，已成功的 batch 会重复写入。这是
Fluent Bit output 插件的标准行为，SLS 服务端会对重复数据做去重处理。

## 本地 smoke 验证

发布包内带有 `putlogs_smoke` 示例，可创建临时 project/logstore，写入一条日志并清理资源。

```sh
export ALIYUN_SLS_ENDPOINT=cn-hangzhou.log.aliyuncs.com
export ALIYUN_SLS_ACCESS_ID=...
export ALIYUN_SLS_ACCESS_KEY=...

cmake -S . -B build
cmake --build build --target putlogs_smoke
./build/putlogs_smoke
```

如果要使用已有资源：

```sh
export ALIYUN_SLS_PROJECT=your-project
export ALIYUN_SLS_LOGSTORE=your-logstore
./build/putlogs_smoke
```

## 排查建议

- `403`：检查 AK/SK、RAM 权限和 project/logstore 权限。
- `404`：检查 project/logstore 是否存在，endpoint 是否属于对应地域。
- `400`：检查 logstore 名称、字段 key 是否为空、请求大小是否超限。
- `429` 或 `5xx`：属于可重试错误，检查 SLS 服务端限流、网络和 Fluent Bit retry 配置。
- 无日志写入：确认 output `Match` 是否匹配 input tag，确认 Fluent Bit chunk 是否进入该 output。

## 安全说明

- 不要将 AK/SK 写入镜像或配置仓库。
- 日志中不会打印 AK/SK。

## 当前限制

- 插件 `.so` 依赖运行环境有 `libcrypto`（OpenSSL）和匹配的 `libstdc++`。如需分发
  预编译产物，需固定构建镜像或改为客户本地构建。
