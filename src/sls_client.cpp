#include "aliyun_sls/sls_client.hpp"

#include "sls_compress.hpp"
#include "sls_crypto.hpp"
#include "sls_protobuf.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace aliyun_sls {
namespace {

std::string stripScheme(const std::string& endpoint) {
    constexpr const char* https = "https://";
    constexpr const char* http = "http://";
    if (endpoint.rfind(https, 0) == 0) {
        return endpoint.substr(std::char_traits<char>::length(https));
    }
    if (endpoint.rfind(http, 0) == 0) {
        return endpoint.substr(std::char_traits<char>::length(http));
    }
    return endpoint;
}

std::string schemeOf(const std::string& endpoint) {
    if (endpoint.rfind("https://", 0) == 0) {
        return "https";
    }
    return "http";
}

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string buildHost(const ClientConfig& config, bool use_project_host) {
    std::string host = stripScheme(trimTrailingSlash(config.endpoint));
    if (!use_project_host) {
        return host;
    }
    if (host.rfind(config.project + ".", 0) == 0) {
        return host;
    }
    return config.project + "." + host;
}

std::string buildConnectHost(const ClientConfig& config) {
    return stripScheme(trimTrailingSlash(config.endpoint));
}

std::string buildUrl(const ClientConfig& config, const std::string& path, bool use_project_host) {
    return schemeOf(config.endpoint) + "://" + buildHost(config, use_project_host) + path;
}

bool isRetryableStatus(int status) {
    return status == 408 || status == 429 || status == 500 || status == 502 || status == 503 ||
           status == 504;
}

std::string headerValue(const std::map<std::string, std::string>& headers, const std::string& key) {
    for (const auto& [candidate, value] : headers) {
        if (crypto::lower(candidate) == crypto::lower(key)) {
            return value;
        }
    }
    return {};
}

std::string extractJsonString(const std::string& body, const std::string& key);

PutLogsResult resultFromResponse(const HttpResponse& response) {
    PutLogsResult result;
    result.http_status = response.status;
    result.request_id = headerValue(response.headers, "x-log-requestid");
    result.error_code = extractJsonString(response.body, "errorCode");
    result.error_message = extractJsonString(response.body, "errorMessage");

    if (response.status >= 200 && response.status < 300) {
        result.code = ResultCode::Ok;
    }
    else if (isRetryableStatus(response.status)) {
        result.code = ResultCode::Retryable;
    }
    else {
        result.code = ResultCode::Fatal;
    }
    return result;
}

std::string extractJsonString(const std::string& body, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    const auto key_pos = body.find(quoted_key);
    if (key_pos == std::string::npos) {
        return {};
    }
    const auto colon = body.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) {
        return {};
    }
    auto begin = body.find('"', colon + 1);
    if (begin == std::string::npos) {
        return {};
    }
    ++begin;
    std::string out;
    bool escaped = false;
    for (auto pos = begin; pos < body.size(); ++pos) {
        const char c = body[pos];
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c);
            }
            else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

std::vector<std::uint8_t> bytesOf(const std::string& value) { return {value.begin(), value.end()}; }

void validateConfig(const ClientConfig& config) {
    if (config.endpoint.empty()) {
        throw std::invalid_argument("SLS endpoint is required");
    }
    if (config.project.empty()) {
        throw std::invalid_argument("SLS project is required");
    }
    if (config.logstore.empty()) {
        throw std::invalid_argument("SLS logstore is required");
    }
    if (config.credentials.access_key_id.empty()) {
        throw std::invalid_argument("SLS access key id is required");
    }
    if (config.credentials.access_key_secret.empty()) {
        throw std::invalid_argument("SLS access key secret is required");
    }
}

HttpRequest buildSignedRequest(const ClientConfig& config, const std::string& method,
                               const std::string& path, const std::vector<std::uint8_t>& body,
                               const std::string& content_type,
                               const std::map<std::string, std::string>& extra_headers,
                               bool use_project_host = true) {
    const std::string date = crypto::rfc1123Now();
    const std::string content_md5 = body.empty() ? std::string{} : crypto::md5HexUpper(body);
    const std::string host = buildHost(config, use_project_host);

    std::map<std::string, std::string> headers;
    headers["Accept"] = "application/json";
    if (!content_md5.empty()) {
        headers["Content-MD5"] = content_md5;
    }
    if (!content_type.empty()) {
        headers["Content-Type"] = content_type;
    }
    headers["Date"] = date;
    headers["Host"] = host;
    headers["User-Agent"] = config.user_agent;
    headers["x-log-apiversion"] = "0.6.0";
    headers["x-log-bodyrawsize"] = std::to_string(body.size());
    headers["x-log-signaturemethod"] = "hmac-sha1";
    if (!config.credentials.security_token.empty()) {
        headers["x-acs-security-token"] = config.credentials.security_token;
    }
    for (const auto& [key, value] : extra_headers) {
        headers[key] = value;
    }

    const std::string string_to_sign =
        crypto::buildStringToSign(method, content_md5, content_type, date, headers, path);
    headers["Authorization"] =
        "LOG " + config.credentials.access_key_id + ":" +
        crypto::hmacSha1Base64(config.credentials.access_key_secret, string_to_sign);

    HttpRequest request;
    request.method = method;
    request.url = buildUrl(config, path, use_project_host);
    request.connect_host = buildConnectHost(config);
    request.path_with_query = path;
    request.headers = std::move(headers);
    request.body = body;
    request.timeout = config.request_timeout;
    return request;
}

} // namespace

Client::Client(ClientConfig config) : Client(std::move(config), createCurlTransport()) {}

Client::Client(ClientConfig config, std::unique_ptr<Transport> transport)
    : config_(std::move(config)), transport_(std::move(transport)) {
    validateConfig(config_);
    if (!transport_) {
        throw std::invalid_argument("SLS transport must not be null");
    }
}

Client::~Client() = default;

PutLogsResult Client::putLogs(const PutLogsRequest& request) {
    try {
        const auto raw_body = protobuf::encodeLogGroup(request.group);
        const auto raw_size = raw_body.size();
        auto body = compress::encodeLz4(raw_body);

        std::string path = "/logstores/" + crypto::urlEncode(config_.logstore) + "/shards/lb";
        if (config_.write_mode == WriteMode::HashKey || !request.hash_key.empty()) {
            path = "/logstores/" + crypto::urlEncode(config_.logstore) +
                   "/shards/route?key=" + crypto::urlEncode(request.hash_key);
        }

        std::map<std::string, std::string> extra_headers;
        extra_headers["x-log-bodyrawsize"] = std::to_string(raw_size);
        extra_headers["x-log-compresstype"] = "lz4";

        const auto http_request = buildSignedRequest(config_, "POST", path, body,
                                                     "application/x-protobuf", extra_headers);
        const auto response = transport_->send(http_request);
        return resultFromResponse(response);
    }
    catch (const std::runtime_error& e) {
        PutLogsResult result;
        result.code = ResultCode::Retryable;
        result.error_message = e.what();
        return result;
    }
    catch (const std::exception& e) {
        PutLogsResult result;
        result.code = ResultCode::Fatal;
        result.error_message = e.what();
        return result;
    }
}

ResourceResult Client::createProject(const std::string& description) {
    try {
        const std::string body = "{\"projectName\":\"" + jsonEscape(config_.project) +
                                 "\",\"description\":\"" + jsonEscape(description) + "\"}";
        const auto request =
            buildSignedRequest(config_, "POST", "/", bytesOf(body), "application/json", {});
        return resultFromResponse(transport_->send(request));
    }
    catch (const std::runtime_error& e) {
        ResourceResult result;
        result.code = ResultCode::Retryable;
        result.error_message = e.what();
        return result;
    }
    catch (const std::exception& e) {
        ResourceResult result;
        result.code = ResultCode::Fatal;
        result.error_message = e.what();
        return result;
    }
}

ResourceResult Client::deleteProject() {
    try {
        const auto request = buildSignedRequest(config_, "DELETE", "/", {}, "", {});
        return resultFromResponse(transport_->send(request));
    }
    catch (const std::runtime_error& e) {
        ResourceResult result;
        result.code = ResultCode::Retryable;
        result.error_message = e.what();
        return result;
    }
    catch (const std::exception& e) {
        ResourceResult result;
        result.code = ResultCode::Fatal;
        result.error_message = e.what();
        return result;
    }
}

ResourceResult Client::createLogStore(int ttl_days, int shard_count) {
    try {
        const std::string body = "{\"logstoreName\":\"" + jsonEscape(config_.logstore) +
                                 "\",\"ttl\":" + std::to_string(ttl_days) +
                                 ",\"shardCount\":" + std::to_string(shard_count) + "}";
        const auto request = buildSignedRequest(config_, "POST", "/logstores", bytesOf(body),
                                                "application/json", {});
        return resultFromResponse(transport_->send(request));
    }
    catch (const std::runtime_error& e) {
        ResourceResult result;
        result.code = ResultCode::Retryable;
        result.error_message = e.what();
        return result;
    }
    catch (const std::exception& e) {
        ResourceResult result;
        result.code = ResultCode::Fatal;
        result.error_message = e.what();
        return result;
    }
}

ResourceResult Client::deleteLogStore() {
    try {
        const std::string path = "/logstores/" + crypto::urlEncode(config_.logstore);
        const auto request = buildSignedRequest(config_, "DELETE", path, {}, "", {});
        return resultFromResponse(transport_->send(request));
    }
    catch (const std::runtime_error& e) {
        ResourceResult result;
        result.code = ResultCode::Retryable;
        result.error_message = e.what();
        return result;
    }
    catch (const std::exception& e) {
        ResourceResult result;
        result.code = ResultCode::Fatal;
        result.error_message = e.what();
        return result;
    }
}

} // namespace aliyun_sls
