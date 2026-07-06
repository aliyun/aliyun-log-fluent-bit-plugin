#include "sls_crypto.hpp"

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace aliyun_sls::crypto {
namespace {

std::string base64Encode(const unsigned char* data, std::size_t len) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3) {
        const std::uint32_t octet_a = data[i];
        const std::uint32_t octet_b = (i + 1 < len) ? data[i + 1] : 0;
        const std::uint32_t octet_c = (i + 2 < len) ? data[i + 2] : 0;
        const std::uint32_t triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

        out.push_back(table[(triple >> 18U) & 0x3fU]);
        out.push_back(table[(triple >> 12U) & 0x3fU]);
        out.push_back(i + 1 < len ? table[(triple >> 6U) & 0x3fU] : '=');
        out.push_back(i + 2 < len ? table[triple & 0x3fU] : '=');
    }
    return out;
}

std::string hexUpper(const unsigned char* data, std::size_t len) {
    static constexpr char table[] = "0123456789ABCDEF";
    std::string out(len * 2, '0');
    for (std::size_t i = 0; i < len; ++i) {
        out[i * 2] = table[data[i] >> 4U];
        out[i * 2 + 1] = table[data[i] & 0x0FU];
    }
    return out;
}

} // namespace

std::string md5Base64(const std::vector<std::uint8_t>& data) {
#if defined(__APPLE__)
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5(data.data(), static_cast<CC_LONG>(data.size()), digest);
#pragma clang diagnostic pop
    return base64Encode(digest, sizeof(digest));
#else
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(data.data(), data.size(), digest);
    return base64Encode(digest, sizeof(digest));
#endif
}

std::string md5HexUpper(const std::vector<std::uint8_t>& data) {
#if defined(__APPLE__)
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5(data.data(), static_cast<CC_LONG>(data.size()), digest);
#pragma clang diagnostic pop
    return hexUpper(digest, sizeof(digest));
#else
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(data.data(), data.size(), digest);
    return hexUpper(digest, sizeof(digest));
#endif
}

std::string hmacSha1Base64(const std::string& key, const std::string& data) {
#if defined(__APPLE__)
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA1, key.data(), key.size(), data.data(), data.size(), digest);
    return base64Encode(digest, sizeof(digest));
#else
    unsigned int len = EVP_MAX_MD_SIZE;
    unsigned char digest[EVP_MAX_MD_SIZE];
    if (HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest,
             &len) == nullptr) {
        throw std::runtime_error("failed to calculate hmac-sha1");
    }
    return base64Encode(digest, len);
#endif
}

std::string rfc1123Now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &now_time);
#else
    gmtime_r(&now_time, &gmt);
#endif

    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &gmt) == 0) {
        throw std::runtime_error("failed to format GMT date");
    }
    return buffer;
}

std::string urlEncode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << ch;
        }
        else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }

    return std::string(begin, end);
}

std::string canonicalizedLogHeaders(const std::map<std::string, std::string>& headers) {
    std::map<std::string, std::string> normalized;
    for (const auto& [key, value] : headers) {
        const auto lower_key = lower(key);
        if (lower_key.rfind("x-log-", 0) == 0 || lower_key.rfind("x-acs-", 0) == 0) {
            normalized[lower_key] = trim(value);
        }
    }

    std::ostringstream out;
    for (const auto& [key, value] : normalized) {
        out << key << ':' << value << '\n';
    }
    return out.str();
}

std::string buildStringToSign(const std::string& method, const std::string& content_md5,
                              const std::string& content_type, const std::string& date,
                              const std::map<std::string, std::string>& headers,
                              const std::string& canonical_resource) {
    std::ostringstream out;
    out << method << '\n'
        << content_md5 << '\n'
        << content_type << '\n'
        << date << '\n'
        << canonicalizedLogHeaders(headers) << canonical_resource;
    return out.str();
}

} // namespace aliyun_sls::crypto
