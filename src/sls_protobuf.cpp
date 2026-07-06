#include "sls_protobuf.hpp"

#include <stdexcept>

namespace aliyun_sls::protobuf {
namespace {

constexpr std::uint32_t kWireVarint = 0;
constexpr std::uint32_t kWireFixed32 = 5;
constexpr std::uint32_t kWireLengthDelimited = 2;

void writeVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

void writeFixed32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

void writeTag(std::vector<std::uint8_t>& out, std::uint32_t field, std::uint32_t wire_type) {
    writeVarint(out, (field << 3U) | wire_type);
}

void writeString(std::vector<std::uint8_t>& out, std::uint32_t field, const std::string& value) {
    if (value.empty()) {
        return;
    }
    writeTag(out, field, kWireLengthDelimited);
    writeVarint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void writeBytes(std::vector<std::uint8_t>& out,
                std::uint32_t field,
                const std::vector<std::uint8_t>& bytes) {
    writeTag(out, field, kWireLengthDelimited);
    writeVarint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::vector<std::uint8_t> encodeContent(const LogContent& content) {
    std::vector<std::uint8_t> out;
    writeString(out, 1, content.key);
    writeString(out, 2, content.value);
    return out;
}

} // namespace

std::vector<std::uint8_t> encodeLog(const LogItem& log) {
    if (log.contents.empty()) {
        throw std::invalid_argument("SLS log item must contain at least one key/value pair");
    }

    std::vector<std::uint8_t> out;
    writeTag(out, 1, kWireVarint);
    writeVarint(out, log.time);

    for (const auto& content : log.contents) {
        if (content.key.empty()) {
            throw std::invalid_argument("SLS log content key must not be empty");
        }
        writeBytes(out, 2, encodeContent(content));
    }

    if (log.time_ns != 0) {
        writeTag(out, 4, kWireFixed32);
        writeFixed32(out, log.time_ns);
    }

    return out;
}

std::vector<std::uint8_t> encodeLogGroup(const LogGroup& group) {
    if (group.logs.empty()) {
        throw std::invalid_argument("SLS LogGroup must contain at least one log");
    }

    std::vector<std::uint8_t> out;
    for (const auto& log : group.logs) {
        writeBytes(out, 1, encodeLog(log));
    }

    writeString(out, 3, group.topic);
    writeString(out, 4, group.source);
    return out;
}

} // namespace aliyun_sls::protobuf

