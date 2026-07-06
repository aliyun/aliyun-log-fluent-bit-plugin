#include "sls_batch.hpp"

#include "sls_protobuf.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

aliyun_sls::LogItem makeLog(std::uint32_t index, std::size_t value_size = 5) {
    aliyun_sls::LogItem log;
    log.time = 1000 + index;
    log.time_ns = index;
    log.contents.push_back({"message", std::string(value_size, static_cast<char>('a' + index))});
    return log;
}

void testSplitByCount() {
    std::vector<aliyun_sls::LogItem> logs;
    for (std::uint32_t i = 0; i < 5; ++i) {
        logs.push_back(makeLog(i));
    }

    aliyun_sls::BatchOptions options;
    options.topic = "topic";
    options.source = "source";
    options.hash_key = "hash";
    options.max_logs_per_batch = 2;

    const auto requests = aliyun_sls::buildPutLogRequests(logs, options);
    assert(requests.size() == 3);
    assert(requests[0].group.logs.size() == 2);
    assert(requests[1].group.logs.size() == 2);
    assert(requests[2].group.logs.size() == 1);
    assert(requests[0].group.topic == "topic");
    assert(requests[0].group.source == "source");
    assert(requests[0].hash_key == "hash");
}

void testSplitByRawBytes() {
    std::vector<aliyun_sls::LogItem> logs;
    logs.push_back(makeLog(0, 128));
    logs.push_back(makeLog(1, 128));
    logs.push_back(makeLog(2, 128));

    aliyun_sls::BatchOptions options;
    options.max_raw_bytes_per_batch = 190;

    const auto requests = aliyun_sls::buildPutLogRequests(logs, options);
    assert(requests.size() == 3);
    for (const auto& request : requests) {
        assert(request.group.logs.size() == 1);
        assert(aliyun_sls::protobuf::encodeLogGroup(request.group).size() <=
               options.max_raw_bytes_per_batch);
    }
}

void testEmptyAndInvalidOptions() {
    aliyun_sls::BatchOptions options;
    assert(aliyun_sls::buildPutLogRequests({}, options).empty());

    options.max_logs_per_batch = 0;
    bool threw = false;
    try {
        (void)aliyun_sls::buildPutLogRequests({makeLog(0)}, options);
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

int main() {
    testSplitByCount();
    testSplitByRawBytes();
    testEmptyAndInvalidOptions();

    std::cout << "batch test passed\n";
    return 0;
}
