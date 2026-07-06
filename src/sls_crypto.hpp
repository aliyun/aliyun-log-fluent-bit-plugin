#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aliyun_sls::crypto {

std::string md5Base64(const std::vector<std::uint8_t>& data);
std::string md5HexUpper(const std::vector<std::uint8_t>& data);
std::string hmacSha1Base64(const std::string& key, const std::string& data);
std::string rfc1123Now();
std::string urlEncode(const std::string& value);
std::string lower(std::string value);
std::string trim(const std::string& value);

std::string canonicalizedLogHeaders(const std::map<std::string, std::string>& headers);
std::string buildStringToSign(const std::string& method, const std::string& content_md5,
                              const std::string& content_type, const std::string& date,
                              const std::map<std::string, std::string>& headers,
                              const std::string& canonical_resource);

} // namespace aliyun_sls::crypto
