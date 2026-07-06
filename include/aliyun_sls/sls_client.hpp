#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aliyun_sls {

enum class WriteMode {
    LoadBalance,
    HashKey,
};

enum class ResultCode {
    Ok,
    Retryable,
    Fatal,
};

struct Credentials {
    std::string access_key_id;
    std::string access_key_secret;
    std::string security_token;
};

struct ClientConfig {
    std::string endpoint;
    std::string project;
    std::string logstore;
    Credentials credentials;
    WriteMode write_mode = WriteMode::LoadBalance;
    std::string user_agent = "aliyun-sls-fluent-bit/0.2.0";
    std::chrono::milliseconds request_timeout{10000};
};

struct LogContent {
    std::string key;
    std::string value;
};

struct LogItem {
    std::uint32_t time = 0;
    std::uint32_t time_ns = 0;
    std::vector<LogContent> contents;
};

struct LogGroup {
    std::string topic;
    std::string source;
    std::vector<LogItem> logs;
};

struct PutLogsRequest {
    LogGroup group;
    std::string hash_key;
};

struct PutLogsResult {
    ResultCode code = ResultCode::Fatal;
    int http_status = 0;
    std::string request_id;
    std::string error_code;
    std::string error_message;

    bool ok() const { return code == ResultCode::Ok; }
    bool retryable() const { return code == ResultCode::Retryable; }
    bool fatal() const { return code == ResultCode::Fatal; }
};

using ResourceResult = PutLogsResult;

struct HttpRequest {
    std::string method;
    std::string url;
    std::string connect_host;
    std::string path_with_query;
    std::map<std::string, std::string> headers;
    std::vector<std::uint8_t> body;
    std::chrono::milliseconds timeout{10000};
};

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

class Client {
public:
    explicit Client(ClientConfig config);
    Client(ClientConfig config, std::unique_ptr<Transport> transport);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    PutLogsResult putLogs(const PutLogsRequest& request);
    ResourceResult createProject(const std::string& description);
    ResourceResult deleteProject();
    ResourceResult createLogStore(int ttl_days, int shard_count);
    ResourceResult deleteLogStore();

private:
    ClientConfig config_;
    std::unique_ptr<Transport> transport_;
};

std::unique_ptr<Transport> createCurlTransport();

} // namespace aliyun_sls
