#include "sls_compress.hpp"

#include "lz4.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace aliyun_sls::compress {

std::vector<std::uint8_t> encodeLz4(const std::vector<std::uint8_t>& data) {
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::logic_error("lz4 input is too large");
    }

    const int source_size = static_cast<int>(data.size());
    const int max_dest_size = LZ4_compressBound(source_size);
    if (max_dest_size <= 0) {
        throw std::logic_error("failed to calculate lz4 output bound");
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(max_dest_size));
    const int compressed_size =
        LZ4_compress_default(reinterpret_cast<const char*>(data.data()),
                             reinterpret_cast<char*>(out.data()), source_size, max_dest_size);
    if (compressed_size <= 0) {
        throw std::logic_error("failed to lz4-compress request body");
    }

    out.resize(static_cast<std::size_t>(compressed_size));
    return out;
}

} // namespace aliyun_sls::compress
