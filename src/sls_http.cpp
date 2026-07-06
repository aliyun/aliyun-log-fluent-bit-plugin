#include "aliyun_sls/sls_client.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#if defined(ALIYUN_SLS_ENABLE_CURL)
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netdb.h>
#endif

namespace aliyun_sls {
namespace {

#if defined(ALIYUN_SLS_ENABLE_CURL)
size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t writeHeader(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(ptr, size * nmemb);
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return size * nmemb;
    }

    auto key = line.substr(0, colon);
    auto value = line.substr(colon + 1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              std::isspace(static_cast<unsigned char>(value.back())))) {
        value.pop_back();
    }
    (*headers)[key] = value;
    return size * nmemb;
}

std::string hostFromUrl(const std::string& url) {
    const auto scheme = url.find("://");
    auto begin = scheme == std::string::npos ? 0 : scheme + 3;
    const auto end = url.find('/', begin);
    auto host = url.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    const auto colon = host.find(':');
    if (colon != std::string::npos) {
        host.resize(colon);
    }
    return host;
}

std::string portFromUrl(const std::string& url) {
    const auto scheme = url.find("://");
    auto begin = scheme == std::string::npos ? 0 : scheme + 3;
    const auto end = url.find('/', begin);
    auto host = url.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    const auto colon = host.find(':');
    if (colon != std::string::npos) {
        return host.substr(colon + 1);
    }
    return url.rfind("https://", 0) == 0 ? "443" : "80";
}

std::string resolveHost(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const int code = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (code != 0) {
        return {};
    }

    char buffer[INET_ADDRSTRLEN] = {};
    for (addrinfo* item = result; item != nullptr; item = item->ai_next) {
        auto* addr = reinterpret_cast<sockaddr_in*>(item->ai_addr);
        if (inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer)) != nullptr) {
            break;
        }
    }
    freeaddrinfo(result);
    return buffer;
}

class CurlTransport final : public Transport {
public:
    CurlTransport() { curl_global_init(CURL_GLOBAL_DEFAULT); }

    ~CurlTransport() override { curl_global_cleanup(); }

    HttpResponse send(const HttpRequest& request) override {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            throw std::runtime_error("failed to initialize curl");
        }

        struct curl_slist* header_list = nullptr;
        for (const auto& [key, value] : request.headers) {
            const std::string header = key + ": " + value;
            header_list = curl_slist_append(header_list, header.c_str());
        }

        struct curl_slist* resolve_list = nullptr;
        const auto url_host = hostFromUrl(request.url);
        if (!request.connect_host.empty() && !url_host.empty() &&
            url_host != request.connect_host) {
            const auto address = resolveHost(request.connect_host);
            if (!address.empty()) {
                const std::string resolve =
                    url_host + ":" + portFromUrl(request.url) + ":" + address;
                resolve_list = curl_slist_append(resolve_list, resolve.c_str());
                curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
            }
        }

        HttpResponse response;
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

        const CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK) {
            const std::string message = curl_easy_strerror(code);
            curl_slist_free_all(resolve_list);
            curl_slist_free_all(header_list);
            curl_easy_cleanup(curl);
            throw std::runtime_error("curl request failed: " + message);
        }

        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        response.status = static_cast<int>(status);

        curl_slist_free_all(resolve_list);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        return response;
    }
};
#endif

} // namespace

std::unique_ptr<Transport> createCurlTransport() {
#if defined(ALIYUN_SLS_ENABLE_CURL)
    return std::make_unique<CurlTransport>();
#else
    throw std::runtime_error("libcurl transport is not enabled");
#endif
}

} // namespace aliyun_sls
