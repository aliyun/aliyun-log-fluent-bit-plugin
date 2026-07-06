#pragma once

#include <cstdint>
#include <vector>

namespace aliyun_sls::compress {

std::vector<std::uint8_t> encodeLz4(const std::vector<std::uint8_t>& data);

} // namespace aliyun_sls::compress
