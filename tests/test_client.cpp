#include "aliyun_sls/sls_client.hpp"

#include "lz4.h"
#include "sls_crypto.hpp"
#include "sls_protobuf.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class CapturingTransport final : public aliyun_sls::Transport {
public:
    aliyun_sls::HttpResponse send(const aliyun_sls::HttpRequest& request) override {
        last_request = request;
        ++send_count;

        aliyun_sls::HttpResponse response;
        response.status = 200;
        response.headers["x-log-requestid"] = "request-id";
        return response;
    }

    aliyun_sls::HttpRequest last_request;
    int send_count = 0;
};

aliyun_sls::ClientConfig makeConfig() {
    aliyun_sls::ClientConfig config;
    config.endpoint = "https://cn-test.log.aliyuncs.com";
    config.project = "project";
    config.logstore = "logstore";
    config.credentials.access_key_id = "ak";
    config.credentials.access_key_secret = "secret";
    return config;
}

aliyun_sls::LogGroup makeGroup() {
    aliyun_sls::LogGroup group;
    group.topic = "topic";
    group.source = "source";

    aliyun_sls::LogItem item;
    item.time = 123;
    item.time_ns = 456;
    item.contents.push_back({"message", "hello lz4"});
    item.contents.push_back({"level", "info"});
    group.logs.push_back(std::move(item));

    return group;
}

std::vector<std::uint8_t> decompressLz4(const std::vector<std::uint8_t>& compressed,
                                        std::size_t raw_size) {
    std::vector<std::uint8_t> out(raw_size);
    const int decoded = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed.data()), reinterpret_cast<char*>(out.data()),
        static_cast<int>(compressed.size()), static_cast<int>(out.size()));

    if (decoded < 0 || static_cast<std::size_t>(decoded) != raw_size) {
        return {};
    }
    return out;
}

void assertLz4PutLogsRequest(const aliyun_sls::HttpRequest& http_request,
                             const std::vector<std::uint8_t>& raw_body) {
    const auto decoded = decompressLz4(http_request.body, raw_body.size());
    assert(http_request.body != raw_body);
    assert(decoded == raw_body);
    assert(http_request.headers.at("x-log-compresstype") == "lz4");
    assert(http_request.headers.at("x-log-bodyrawsize") == std::to_string(raw_body.size()));
    assert(http_request.headers.at("Content-MD5") ==
           aliyun_sls::crypto::md5HexUpper(http_request.body));
    assert(http_request.headers.at("Authorization").rfind("LOG ak:", 0) == 0);
}

void testDefaultLz4PutLogsRequest() {
    auto config = makeConfig();
    auto* transport = new CapturingTransport;
    aliyun_sls::Client client(config, std::unique_ptr<aliyun_sls::Transport>(transport));

    aliyun_sls::PutLogsRequest request;
    request.group = makeGroup();
    const auto raw_body = aliyun_sls::protobuf::encodeLogGroup(request.group);

    const auto result = client.putLogs(request);
    assert(result.ok());
    assert(result.http_status == 200);
    assert(result.request_id == "request-id");
    assert(transport->send_count == 1);

    const auto& http_request = transport->last_request;
    assert(http_request.method == "POST");
    assert(http_request.path_with_query == "/logstores/logstore/shards/lb");
    assert(http_request.url ==
           "https://project.cn-test.log.aliyuncs.com/logstores/logstore/shards/lb");
    assert(http_request.connect_host == "cn-test.log.aliyuncs.com");
    assert(http_request.headers.at("Host") == "project.cn-test.log.aliyuncs.com");
    assertLz4PutLogsRequest(http_request, raw_body);
}

void testResourceRequests() {
    auto config = makeConfig();
    config.endpoint = "cn-test.log.aliyuncs.com";
    auto* transport = new CapturingTransport;
    aliyun_sls::Client client(config, std::unique_ptr<aliyun_sls::Transport>(transport));

    auto result = client.createProject("test project");
    assert(result.ok());
    assert(transport->last_request.method == "POST");
    assert(transport->last_request.path_with_query == "/");
    assert(transport->last_request.url == "http://project.cn-test.log.aliyuncs.com/");
    assert(transport->last_request.connect_host == "cn-test.log.aliyuncs.com");
    assert(transport->last_request.headers.at("Host") == "project.cn-test.log.aliyuncs.com");
    assert(std::string(transport->last_request.body.begin(), transport->last_request.body.end()) ==
           "{\"projectName\":\"project\",\"description\":\"test project\"}");
    assert(transport->last_request.headers.at("Content-Type") == "application/json");
    assert(transport->last_request.headers.at("x-log-bodyrawsize") ==
           std::to_string(transport->last_request.body.size()));

    result = client.createLogStore(7, 2);
    assert(result.ok());
    assert(transport->last_request.method == "POST");
    assert(transport->last_request.path_with_query == "/logstores");
    assert(transport->last_request.url == "http://project.cn-test.log.aliyuncs.com/logstores");
    assert(transport->last_request.headers.at("Host") == "project.cn-test.log.aliyuncs.com");
    assert(std::string(transport->last_request.body.begin(), transport->last_request.body.end()) ==
           "{\"logstoreName\":\"logstore\",\"ttl\":7,\"shardCount\":2}");

    result = client.deleteLogStore();
    assert(result.ok());
    assert(transport->last_request.method == "DELETE");
    assert(transport->last_request.path_with_query == "/logstores/logstore");
    assert(transport->last_request.headers.count("Content-Type") == 0);
    assert(transport->last_request.headers.count("Content-MD5") == 0);
    assert(transport->last_request.headers.at("x-log-bodyrawsize") == "0");

    result = client.deleteProject();
    assert(result.ok());
    assert(transport->last_request.method == "DELETE");
    assert(transport->last_request.path_with_query == "/");
    assert(transport->last_request.url == "http://project.cn-test.log.aliyuncs.com/");
    assert(transport->last_request.headers.at("Host") == "project.cn-test.log.aliyuncs.com");
}

} // namespace

int main() {
    testDefaultLz4PutLogsRequest();
    testResourceRequests();

    std::cout << "client request test passed\n";
    return 0;
}
