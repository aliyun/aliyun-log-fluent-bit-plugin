#pragma once

#include "aliyun_sls/sls_client.hpp"

#include <cstdint>
#include <vector>

namespace aliyun_sls::protobuf {

std::vector<std::uint8_t> encodeLog(const LogItem &log);
std::vector<std::uint8_t> encodeLogGroup(const LogGroup& group);

} // namespace aliyun_sls::protobuf

