# Release Notes: Aliyun SLS Fluent Bit Plugin v0.1.0

发布日期：2026-07-02

## 发布类型

首次正式源码集成发布。

该版本面向 Fluent Bit in-tree output plugin 集成，不提供 `dlopen` 动态插件产物。

## 核心能力

- 提供 Fluent Bit output plugin：`aliyun_sls`。
- 支持将 Fluent Bit msgpack chunk 转换为 SLS `LogGroup` 并写入 `PutLogs`。
- `PutLogs` 请求体固定使用 LZ4 压缩。
- 内置 SLS request signing：
  - `Content-MD5`
  - `x-log-bodyrawsize`
  - `x-log-signaturemethod`
  - `Authorization`
- 支持 AK/SK 和 STS token。
- 支持 batch split：
  - `Max_Logs_Per_Batch`
  - `Max_Raw_Bytes_Per_Batch`
- 支持 retry/fatal 结果映射到 Fluent Bit：
  - 成功：`FLB_OK`
  - 可重试：`FLB_RETRY`
  - 不可重试：`FLB_ERROR`
- vendored LZ4 源码，不依赖系统 liblz4。

## 发布产物

| 文件 | 说明 |
| --- | --- |
| `aliyun-sls-fluent-bit-plugin-v0.1.0-src.tar.gz` | 源码集成发布包 |
| `aliyun-sls-fluent-bit-plugin-v0.1.0-src.tar.gz.sha256` | 发布包 SHA256 校验文件 |
| `aliyun-sls-fluent-bit-plugin-v0.1.0-manifest.json` | 发布 manifest，包含版本、commit 和校验信息 |

## 客户可见配置

```ini
[OUTPUT]
    Name                    aliyun_sls
    Match                   *
    Endpoint                cn-hangzhou.log.aliyuncs.com
    Project                 your-project
    Logstore                your-logstore
    Access_Key_Id           ${ALIYUN_SLS_ACCESS_ID}
    Access_Key_Secret       ${ALIYUN_SLS_ACCESS_KEY}
    Security_Token          ${ALIYUN_SLS_SECURITY_TOKEN}
    Topic                   fluent-bit
    Source                  edge-node-a
    Hash_Key                optional-route-key
    Request_Timeout_Ms      10000
    Max_Logs_Per_Batch      4096
    Max_Raw_Bytes_Per_Batch 3145728
```

## 验证记录

本版本发布前完成以下验证：

- `test_client`
- `test_crypto`
- `test_protobuf`
- `test_batch`
- `putlogs_smoke` 编译验证
- 使用真实 SLS endpoint 执行 LZ4 `PutLogs` smoke：
  - create project：200
  - create logstore：200
  - put logs：200
  - delete logstore：200
  - delete project：200

## 已知限制

- 仅发布源码集成包，需要客户在 Fluent Bit 源码树中编译进 Fluent Bit binary。
- 不支持 `dlopen` 动态加载。
- 当前 HTTP transport 使用 libcurl；后续版本可按目标 Fluent Bit 版本替换为 Fluent Bit upstream/http transport。

## 升级兼容性

这是首个正式发布版本，没有历史版本升级兼容要求。
