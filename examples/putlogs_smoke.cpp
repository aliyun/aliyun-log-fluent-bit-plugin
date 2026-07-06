#include "aliyun_sls/sls_client.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <thread>

namespace {

std::string envOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

std::string firstEnvOrDefault(const char* primary, const char* fallback,
                              const char* default_value = "") {
    auto value = envOrEmpty(primary);
    if (!value.empty()) {
        return value;
    }
    value = envOrEmpty(fallback);
    if (!value.empty()) {
        return value;
    }
    return default_value;
}

std::string generatedName(const char* prefix) {
    return std::string(prefix) + "-" + std::to_string(static_cast<long long>(std::time(nullptr)));
}

void printResult(const char* operation, const aliyun_sls::ResourceResult& result) {
    std::cout << operation << " status=" << result.http_status
              << " code=" << static_cast<int>(result.code) << " request_id=" << result.request_id
              << " error_code=" << result.error_code << " error_message=" << result.error_message
              << "\n";
}

} // namespace

int main() {
    try {
        aliyun_sls::ClientConfig config;
        config.endpoint = firstEnvOrDefault("ALIYUN_SLS_ENDPOINT", "SLS_ENDPOINT");
        config.project = firstEnvOrDefault("ALIYUN_SLS_PROJECT", "SLS_PROJECT");
        config.logstore = firstEnvOrDefault("ALIYUN_SLS_LOGSTORE", "SLS_LOGSTORE");
        config.credentials.access_key_id =
            firstEnvOrDefault("ALIYUN_SLS_ACCESS_ID", "ALIYUN_ACCESS_KEY_ID");
        config.credentials.access_key_secret =
            firstEnvOrDefault("ALIYUN_SLS_ACCESS_KEY", "ALIYUN_ACCESS_KEY_SECRET");
        config.credentials.security_token =
            firstEnvOrDefault("ALIYUN_SLS_SECURITY_TOKEN", "ALIYUN_SECURITY_TOKEN");

        const bool create_project = config.project.empty();
        const bool create_logstore = config.logstore.empty();
        if (create_project) {
            config.project = generatedName("sls-fluent-bit-plugin");
        }
        if (create_logstore) {
            config.logstore = "logs";
        }

        aliyun_sls::Client client(config);

        std::cout << "endpoint=" << config.endpoint << " project=" << config.project
                  << " logstore=" << config.logstore << " compression=lz4\n";

        if (create_project) {
            const auto result = client.createProject("aliyun-sls-fluent-bit-plugin smoke test");
            printResult("create_project", result);
            if (!result.ok()) {
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        if (create_logstore) {
            const auto result = client.createLogStore(1, 1);
            printResult("create_logstore", result);
            if (!result.ok()) {
                if (create_project) {
                    printResult("delete_project", client.deleteProject());
                }
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }

        aliyun_sls::LogItem log;
        log.time = static_cast<std::uint32_t>(std::time(nullptr));
        log.contents.push_back({"message", "hello from aliyun_sls::Client"});
        log.contents.push_back({"language", "c++"});

        aliyun_sls::PutLogsRequest request;
        request.group.topic = "client-example";
        request.group.source = "local";
        request.group.logs.push_back(std::move(log));

        const auto result = client.putLogs(request);
        printResult("put_logs", result);

        if (create_logstore) {
            printResult("delete_logstore", client.deleteLogStore());
        }
        if (create_project) {
            printResult("delete_project", client.deleteProject());
        }

        return result.ok() ? 0 : 1;
    }
    catch (const std::exception& e) {
        std::cerr << "putlogs_smoke failed: " << e.what() << "\n";
        return 2;
    }
}
