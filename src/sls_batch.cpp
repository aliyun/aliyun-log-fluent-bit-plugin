#include "sls_batch.hpp"

#include "sls_protobuf.hpp"

#include <stdexcept>

namespace aliyun_sls {
namespace {

void validateOptions(const BatchOptions& options) {
    if (options.max_logs_per_batch == 0) {
        throw std::invalid_argument("max logs per SLS batch must be greater than zero");
    }
    if (options.max_raw_bytes_per_batch == 0) {
        throw std::invalid_argument("max raw bytes per SLS batch must be greater than zero");
    }
}

PutLogsRequest makeRequest(const BatchOptions& options) {
    PutLogsRequest request;
    request.group.topic = options.topic;
    request.group.source = options.source;
    request.hash_key = options.hash_key;
    return request;
}

std::size_t rawSize(const PutLogsRequest& request) {
    return protobuf::encodeLogGroup(request.group).size();
}

} // namespace

std::vector<PutLogsRequest> buildPutLogRequests(const std::vector<LogItem>& logs,
                                                const BatchOptions& options) {
    validateOptions(options);

    std::vector<PutLogsRequest> requests;
    PutLogsRequest current = makeRequest(options);

    for (const auto& log : logs) {
        PutLogsRequest candidate = current;
        candidate.group.logs.push_back(log);

        const bool too_many_logs = candidate.group.logs.size() > options.max_logs_per_batch;
        const bool too_many_bytes =
            !current.group.logs.empty() && rawSize(candidate) > options.max_raw_bytes_per_batch;
        if (too_many_logs || too_many_bytes) {
            requests.push_back(std::move(current));
            current = makeRequest(options);
        }
        current.group.logs.push_back(log);
    }

    if (!current.group.logs.empty()) {
        requests.push_back(std::move(current));
    }

    return requests;
}

} // namespace aliyun_sls
