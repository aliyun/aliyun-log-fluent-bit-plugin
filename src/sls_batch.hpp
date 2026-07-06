#pragma once

#include "aliyun_sls/sls_client.hpp"

#include <cstddef>
#include <vector>

namespace aliyun_sls {

struct BatchOptions {
    std::string topic;
    std::string source;
    std::string hash_key;
    std::size_t max_logs_per_batch = 4096;
    std::size_t max_raw_bytes_per_batch = 3 * 1024 * 1024;
};

std::vector<PutLogsRequest> buildPutLogRequests(const std::vector<LogItem>& logs,
                                                const BatchOptions& options);

} // namespace aliyun_sls
