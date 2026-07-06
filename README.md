# Aliyun SLS Fluent Bit 输出插件

这是一个 Fluent Bit 输出插件，用来把 Fluent Bit 采集到的日志写入阿里云日志服务 SLS。

如果你刚开始接触 Fluent Bit，可以先记住这条链路：

```text
应用/文件/标准输出 -> Fluent Bit -> aliyun_sls 插件 -> 阿里云 SLS Logstore
```

插件会负责把 Fluent Bit 的日志记录转换为 SLS `LogGroup`，并完成签名、LZ4 压缩、批量切分和错误重试结果映射。

## 适用版本

- Fluent Bit：推荐 `3.x`，插件代码面向 Fluent Bit 3.x output API。
- 编译器：需要支持 C++17。
- 系统：Linux/macOS 均可构建；生产运行建议使用 Linux。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `src/` | 插件和 SLS client 源码 |
| `include/aliyun_sls/` | 对外 C/C++ 头文件 |
| `examples/fluent-bit.conf` | Fluent Bit 配置示例 |
| `examples/putlogs_smoke.cpp` | SLS 写入 smoke test 示例 |
| `tests/` | 单元测试 |
| `third_party/lz4/` | 内置 LZ4 源码 |
| `docs/docker-build.md` | Docker 构建示例 |

## 准备 SLS 信息

运行前需要准备以下信息：

| 信息 | 示例 | 从哪里获取 |
| --- | --- | --- |
| `endpoint` | `cn-hangzhou.log.aliyuncs.com` | SLS 控制台对应地域的 endpoint |
| `project` | `your-project` | SLS Project 名称 |
| `logstore` | `your-logstore` | SLS Logstore 名称 |
| `access_key_id` | 不建议写入配置文件 | RAM 用户或 STS 凭证 |
| `access_key_secret` | 不建议写入配置文件 | RAM 用户或 STS 凭证 |

建议使用环境变量传入 AK/SK，避免把密钥写进配置文件或镜像：

```sh
export ALIYUN_SLS_ACCESS_ID='你的 AccessKey ID'
export ALIYUN_SLS_ACCESS_KEY='你的 AccessKey Secret'
```

## 依赖要求

### 必需依赖

| 依赖 | 用途 | 检测命令 |
| --- | --- | --- |
| `git` | 拉取 Fluent Bit 源码 | `git --version` |
| `cmake >= 3.16` | 生成构建工程 | `cmake --version` |
| C/C++ 编译器 | 编译插件和 SLS client | `cc --version`、`c++ --version` |
| `make` 或 `ninja` | 执行构建 | `make --version` 或 `ninja --version` |
| OpenSSL 开发库 | Linux 下计算 MD5/HMAC 签名 | `pkg-config --modversion openssl` |
| libcurl 开发库 | 构建本地测试和 smoke 工具 | `curl-config --version` |
| Fluent Bit 源码和构建目录 | 编译外部 `.so` 插件时需要头文件 | `test -f "$FLUENT_BIT_DIR/build/include/fluent-bit/flb_info.h" && echo ok` |

说明：

- macOS 下签名使用系统的 CommonCrypto，不需要额外安装 OpenSSL 才能完成核心加密逻辑。
- 插件运行在 Fluent Bit 内部时使用 Fluent Bit 原生网络栈，不依赖 libcurl。
- `third_party/lz4` 已经包含 LZ4 源码，不需要安装系统 `liblz4`。

### Ubuntu/Debian 安装依赖

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git pkg-config \
  libssl-dev libcurl4-openssl-dev \
  flex bison libyaml-dev
```

安装后可以检查：

```sh
git --version
cmake --version
c++ --version
pkg-config --modversion openssl
curl-config --version
```

### CentOS/RHEL 安装依赖

```sh
sudo yum install -y \
  gcc gcc-c++ make git pkgconfig \
  openssl-devel libcurl-devel \
  flex bison libyaml-devel
```

如果系统自带 CMake 版本低于 3.16，请安装 `cmake3` 或从 CMake 官网安装新版：

```sh
cmake --version
cmake3 --version
```

### macOS 安装依赖

```sh
xcode-select --install
brew install cmake git curl flex bison
```

检查命令：

```sh
cmake --version
c++ --version
curl-config --version
```

## 本地编译和测试

先确认普通 client 和单元测试可以通过：

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

如果只想验证是否能真实写入 SLS，可以运行 smoke test：

```sh
export ALIYUN_SLS_ENDPOINT=cn-hangzhou.log.aliyuncs.com
export ALIYUN_SLS_ACCESS_ID='你的 AccessKey ID'
export ALIYUN_SLS_ACCESS_KEY='你的 AccessKey Secret'

cmake -S . -B build
cmake --build build --target putlogs_smoke
./build/putlogs_smoke
```

默认情况下，`putlogs_smoke` 会创建临时 project/logstore，写入一条日志后清理。如果要使用已有资源：

```sh
export ALIYUN_SLS_PROJECT=your-project
export ALIYUN_SLS_LOGSTORE=your-logstore
./build/putlogs_smoke
```

## 集成到 Fluent Bit

推荐使用外部 `.so` 动态加载方式，不需要修改 Fluent Bit 源码。

### 方式一：构建外部 `.so` 插件

第一步，准备 Fluent Bit 源码并构建一次。这里以 `v3.2.10` 为例，源码放在当前用户目录下，避免写 `/opt` 时遇到权限问题：

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

这一步的主要目的是生成 Fluent Bit 头文件，例如：

```sh
test -f "$FLUENT_BIT_DIR/build/include/fluent-bit/flb_info.h" && echo "Fluent Bit build ok"
```

第二步，编译本插件：

```sh
cmake -S . -B build-plugin \
  -DALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON \
  -DFLUENT_BIT_SRC="$FLUENT_BIT_DIR" \
  -DFLUENT_BIT_BUILD_DIR="$FLUENT_BIT_DIR/build"

cmake --build build-plugin --target flb-out_aliyun_sls --parallel
```

编译完成后应生成：

```text
build-plugin/out_aliyun_sls.so
```

可以用下面的命令确认插件符号存在：

```sh
nm -D build-plugin/out_aliyun_sls.so | grep out_aliyun_sls_plugin
```

第三步，用 Fluent Bit 加载插件：

```sh
export ALIYUN_SLS_ACCESS_ID='你的 AccessKey ID'
export ALIYUN_SLS_ACCESS_KEY='你的 AccessKey Secret'

fluent-bit -e build-plugin/out_aliyun_sls.so -c examples/fluent-bit.conf
```

### 方式二：In-tree 编译

如果你需要把插件编译进 Fluent Bit 二进制，可以把本仓库复制到 Fluent Bit 插件目录：

```sh
cp -a . /path/to/fluent-bit/plugins/out_aliyun_sls
```

然后在 Fluent Bit 构建系统中加入这些源码：

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

Linux 下还需要链接 OpenSSL crypto：

```text
OpenSSL::Crypto
```

最后在 Fluent Bit 的 output plugin 注册列表中注册：

```c
extern struct flb_output_plugin out_aliyun_sls_plugin;
```

## Fluent Bit 配置示例

`examples/fluent-bit.conf` 是一个最小可运行示例：

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

## 配置参数说明

| 参数 | 来源 | 必填 | 默认值 | 示例 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `name` | Fluent Bit | 是 | 无 | `aliyun_sls` | 输出插件名称，必须写成 `aliyun_sls`。 |
| `match` | Fluent Bit | 是 | 无 | `*`、`app.*` | 选择哪些 tag 的日志发送到该 output。 |
| `endpoint` | 本插件 | 是 | 无 | `cn-hangzhou.log.aliyuncs.com` | SLS endpoint，不需要写 `https://`。 |
| `project` | 本插件 | 是 | 无 | `your-project` | SLS Project 名称。 |
| `logstore` | 本插件 | 是 | 无 | `your-logstore` | SLS Logstore 名称。 |
| `access_key_id` | 本插件 | 是 | 无 | `${ALIYUN_SLS_ACCESS_ID}` | 访问 SLS 的 AccessKey ID，建议使用环境变量。 |
| `access_key_secret` | 本插件 | 是 | 无 | `${ALIYUN_SLS_ACCESS_KEY}` | 访问 SLS 的 AccessKey Secret，建议使用环境变量。 |
| `topic` | 本插件 | 否 | 空 | `fluent-bit` | 写入 SLS LogGroup 的 topic。 |
| `source` | 本插件 | 否 | Fluent Bit tag | `edge-node-a` | 写入 SLS LogGroup 的 source；不配置时使用当前日志 tag。 |
| `hash_key` | 本插件 | 否 | 空 | `user-123` | 配置后使用 SLS KeyHash 路由写入指定 shard。一般用户可不配置。 |
| `port` | 本插件 | 否 | `0` | `443` | 覆盖 endpoint 端口。`0` 表示自动选择：TLS 开启时 443，否则 80。 |
| `max_raw_bytes_per_batch` | 本插件 | 否 | `5242880` | `5242880` | 单个 PutLogs 请求压缩前最大字节数，默认 5 MB。 |
| `tls` | Fluent Bit | 否 | `off` | `on` | 是否启用 TLS。访问公网 SLS endpoint 时建议开启。 |
| `tls.verify` | Fluent Bit | 否 | `on` | `on` | 是否校验证书。生产环境建议保持开启。 |
| `net.keepalive` | Fluent Bit | 否 | Fluent Bit 默认值 | `on` | 是否复用连接。建议开启。 |
| `net.connect_timeout` | Fluent Bit | 否 | Fluent Bit 默认值 | `10` | 建立连接超时时间，单位为秒。 |
| `net.io_timeout` | Fluent Bit | 否 | Fluent Bit 默认值 | `30` | 读写超时时间，单位为秒。 |
| `workers` | Fluent Bit | 否 | `0` | `2` | flush worker 数量。`0` 表示使用 Fluent Bit 默认调度。 |
| `retry_limit` | Fluent Bit | 否 | Fluent Bit 默认值 | `5` | 写入失败后的重试次数。 |

## 写入字段映射

当 Fluent Bit record 是 map 时，map 中的 key/value 会写入 SLS log contents。

输入示例：

```json
{
  "level": "info",
  "message": "service started",
  "trace_id": "abc"
}
```

写入 SLS 后类似：

```text
level=info
message=service started
trace_id=abc
```

非字符串类型会被转换为字符串；嵌套 map/array 会以紧凑字符串形式写入。

## 常见问题

| 现象 | 可能原因 | 处理方式 |
| --- | --- | --- |
| `endpoint is required` | 配置里缺少 `endpoint` | 检查 `[OUTPUT]` 是否配置了 `endpoint`。 |
| SLS 返回 `403` | AK/SK 错误或权限不足 | 检查环境变量、RAM 权限、Project/Logstore 权限。 |
| SLS 返回 `404` | Project/Logstore 不存在或地域不匹配 | 检查 `endpoint`、`project`、`logstore` 是否对应同一地域。 |
| SLS 返回 `400` | 请求参数或日志内容不合法 | 检查 Logstore 名称、字段 key、请求大小。 |
| SLS 返回 `429` 或 `5xx` | 服务端限流或临时异常 | 交给 Fluent Bit 重试，也可调整 `retry_limit`。 |
| 没有日志写入 | `match` 没匹配 input tag | 检查 `[INPUT]` 的 `tag` 和 `[OUTPUT]` 的 `match`。 |
| 找不到 `flb_info.h` | Fluent Bit 没有先构建 | 先执行 `cmake --build "$FLUENT_BIT_DIR/build"`。 |

## 安全建议

- 不要把 AK/SK 写死在 `fluent-bit.conf`、Dockerfile、镜像或 Git 仓库里。
- 推荐使用环境变量、Kubernetes Secret、主机密钥管理系统或临时 STS 凭证注入密钥。
- 插件不会主动打印 AK/SK。
- 初始化日志会打印 endpoint、project、logstore；如果这些资源名也属于敏感信息，请在生产日志采集策略里注意控制可见范围。

## 更多文档

- Docker 构建示例：[docs/docker-build.md](docs/docker-build.md)
- 客户使用文档：[docs/customer-guide.md](docs/customer-guide.md)
- 发布说明：[docs/release-notes-v0.2.0.md](docs/release-notes-v0.2.0.md)
