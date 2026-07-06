# Release Notes: Aliyun SLS Fluent Bit Plugin v0.2.0

发布日期：2026-07-03

## 发布类型

重大重构版本。插件完全重写为 Fluent Bit 3.x 原生 API。

## 与 v0.1.0 主要变化

| 项目 | v0.1.0 | v0.2.0 |
| --- | --- | --- |
| 集成方式 | 仅 in-tree | **外部 .so (推荐)** + in-tree |
| 网络栈 | libcurl | Fluent Bit native `flb_upstream` + `flb_http_client` |
| 事件解码 | 手动 msgpack 解析 | `flb_log_event_decoder` |
| 配置声明 | 硬编码 | `config_map` |
| 观测性 | 无 | `cmetrics` 计数器 |
| 并发 | 不支持 | `workers` 多线程 flush |
| 批次切分 | 估算 raw bytes | **精确 protobuf wire size** |
| STS | 不支持 | 不支持 |
| 超时 | 自定义 `request_timeout_ms` | Fluent Bit `net.io_timeout`/`net.connect_timeout` |
| 插件语言 | C++ (与 fluent-bit headers 不兼容) | 纯 C + C API bridge |

## 核心能力

- 提供 Fluent Bit output plugin：`aliyun_sls`。
- 支持外部 `.so` 动态加载：`fluent-bit -e out_aliyun_sls.so`。
- 支持 AK/SK 认证。
- 批次按精确 protobuf 编码大小切分（`max_raw_bytes_per_batch`，默认 5 MB）。
- Fluent Bit 原生网络：TLS、keepalive、连接池、代理、超时由 `net.*` 控制。
- 多 flush worker 并行。
- cmetrics：`fluentbit_output_sls_records_total`、`fluentbit_output_sls_requests_total{result=ok|retry|error}`。
- At-least-once 语义（多 batch chunk 重试时可能重复写入）。

## 不兼容变化

以下 v0.1.0 配置项已移除，迁移时需修改配置文件：

| 移除配置 | 替代方案 |
| --- | --- |
| `Request_Timeout_Ms` | 使用 Fluent Bit `net.io_timeout` |
| `Max_Logs_Per_Batch` | 移除，仅按大小切分 |

配置项名称统一为小写（`access_key_id`，非 `Access_Key_Id`）。

## 客户可见配置

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
    max_raw_bytes_per_batch  5242880
    tls                      on
    workers                  2
    retry_limit              5
```

## 构建方式

外部 `.so`（推荐）：

```sh
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC=/path/to/fluent-bit
cmake --build build-plugin --target flb-out_aliyun_sls
```

`ALIYUN_SLS_ENABLE_CURL` 在插件构建模式下自动关闭，插件 `.so` 不依赖 libcurl。

Docker 构建步骤见 `docs/docker-build.md`。

## 验证记录

- `test_protobuf` — SLS LogGroup protobuf 编码
- `test_crypto` — HMAC-SHA1、MD5、签名
- `test_client` — Client 请求构造
- `test_batch` — LogGroup 批次切分
- Docker 环境编译出 `out_aliyun_sls.so`（762 KB），`nm -D` 确认符号导出
- `ldd` 确认无 libcurl 依赖

## 已知限制

- 插件 `.so` 依赖运行环境有 `libcrypto`（OpenSSL）和匹配的 `libstdc++`。
- At-least-once 语义：多 batch chunk 中前批成功后批重试时，前批会重复写入。

## 升级兼容性

从 v0.1.0 升级需要：
1. 修改配置文件中的属性名为小写格式。
2. 移除 `Request_Timeout_Ms`，改用 `net.io_timeout`。
3. 移除 `Max_Logs_Per_Batch`。
4. 如使用外部 `.so` 方式，不再需要 in-tree 编译集成。
