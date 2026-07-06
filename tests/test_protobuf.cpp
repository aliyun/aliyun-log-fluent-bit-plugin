#include "aliyun_sls/sls_client.hpp"

#include "sls_protobuf.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    aliyun_sls::LogGroup group;
    group.topic = "t";
    group.source = "s";

    aliyun_sls::LogItem item;
    item.time = 1;
    item.contents.push_back({"foo", "bar"});
    group.logs.push_back(std::move(item));

    const auto encoded = aliyun_sls::protobuf::encodeLogGroup(group);
    const std::vector<std::uint8_t> expected = {
        0x0a, 0x0e,
        0x08, 0x01,
        0x12, 0x0a,
        0x0a, 0x03, 'f', 'o', 'o',
        0x12, 0x03, 'b', 'a', 'r',
        0x1a, 0x01, 't',
        0x22, 0x01, 's',
    };

    assert(encoded == expected);
    std::cout << "protobuf encoder test passed\n";
    return 0;
}

