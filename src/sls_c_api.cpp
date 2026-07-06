#include "aliyun_sls/sls_c_api.h"

#include "aliyun_sls/sls_client.hpp"
#include "sls_protobuf.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

struct aliyun_sls_client {
    std::unique_ptr<aliyun_sls::Client> impl;
};

struct aliyun_sls_log_group {
    aliyun_sls::LogGroup impl;
    std::size_t encoded_size = 0;
};

struct aliyun_sls_log_item {
    aliyun_sls_log_group* group = nullptr;
    std::size_t index = 0;
};

namespace {

std::string safeString(const char* value) {
    return value == nullptr ? std::string{} : std::string(value);
}

const char* dupString(const std::string& value) {
    char* copy = static_cast<char*>(std::malloc(value.size() + 1));
    if (copy == nullptr) {
        return nullptr;
    }
    std::memcpy(copy, value.data(), value.size());
    copy[value.size()] = '\0';
    return copy;
}

aliyun_sls_result_code_t convertCode(aliyun_sls::ResultCode code) {
    switch (code) {
    case aliyun_sls::ResultCode::Ok:
        return ALIYUN_SLS_OK;
    case aliyun_sls::ResultCode::Retryable:
        return ALIYUN_SLS_RETRYABLE;
    case aliyun_sls::ResultCode::Fatal:
        return ALIYUN_SLS_FATAL;
    }
    return ALIYUN_SLS_FATAL;
}

aliyun_sls_put_result_t makeFatal(const char* message) {
    aliyun_sls_put_result_t result{};
    result.code = ALIYUN_SLS_FATAL;
    result.error_message = dupString(message == nullptr ? "unknown fatal error" : message);
    return result;
}

} // namespace

class CTransportBridge final : public aliyun_sls::Transport {
public:
  CTransportBridge(aliyun_sls_transport_fn fn, void *user_data)
      : fn_(fn), user_data_(user_data) {}

  aliyun_sls::HttpResponse
  send(const aliyun_sls::HttpRequest &request) override {
    std::vector<const char *> keys;
    std::vector<const char *> values;
    for (const auto &[k, v] : request.headers) {
      keys.push_back(k.c_str());
      values.push_back(v.c_str());
    }

    aliyun_sls_http_request_t c_req{};
    c_req.method = request.method.c_str();
    c_req.url = request.url.c_str();
    c_req.connect_host = request.connect_host.c_str();
    c_req.path_with_query = request.path_with_query.c_str();
    c_req.body = request.body.data();
    c_req.body_size = request.body.size();
    c_req.timeout_ms = static_cast<int>(request.timeout.count());
    c_req.header_keys = keys.data();
    c_req.header_values = values.data();
    c_req.header_count = keys.size();

    aliyun_sls_http_response_t c_resp{};
    int rc = fn_(&c_req, &c_resp, user_data_);
    if (rc != 0) {
      aliyun_sls_http_response_cleanup(&c_resp);
      throw std::runtime_error("transport callback failed");
    }

    aliyun_sls::HttpResponse response;
    response.status = c_resp.status;
    if (c_resp.body != nullptr && c_resp.body_size > 0) {
      response.body.assign(c_resp.body, c_resp.body_size);
    }
    for (size_t i = 0; i < c_resp.header_count; ++i) {
      if (c_resp.header_keys[i] && c_resp.header_values[i]) {
        response.headers[c_resp.header_keys[i]] = c_resp.header_values[i];
      }
    }
    aliyun_sls_http_response_cleanup(&c_resp);
    return response;
  }

private:
  aliyun_sls_transport_fn fn_;
  void *user_data_;
};

extern "C" void
aliyun_sls_http_response_cleanup(aliyun_sls_http_response_t *response) {
  if (response == nullptr) {
    return;
  }
  std::free(response->body);
  response->body = nullptr;
  for (size_t i = 0; i < response->header_count; ++i) {
    std::free(response->header_keys[i]);
    std::free(response->header_values[i]);
  }
  std::free(response->header_keys);
  std::free(response->header_values);
  response->header_keys = nullptr;
  response->header_values = nullptr;
  response->header_count = 0;
}

extern "C" aliyun_sls_client_t* aliyun_sls_client_create(const aliyun_sls_client_config_t* config) {
    if (config == nullptr) {
        return nullptr;
    }

    try {
        aliyun_sls::ClientConfig cpp_config;
        cpp_config.endpoint = safeString(config->endpoint);
        cpp_config.project = safeString(config->project);
        cpp_config.logstore = safeString(config->logstore);
        cpp_config.credentials.access_key_id = safeString(config->access_key_id);
        cpp_config.credentials.access_key_secret = safeString(config->access_key_secret);
        cpp_config.credentials.security_token = safeString(config->security_token);
        if (config->request_timeout_ms > 0) {
            cpp_config.request_timeout = std::chrono::milliseconds(config->request_timeout_ms);
        }

        auto* client = new aliyun_sls_client;
        if (config->transport != nullptr) {
          auto transport = std::make_unique<CTransportBridge>(
              config->transport, config->transport_user_data);
          client->impl = std::make_unique<aliyun_sls::Client>(
              std::move(cpp_config), std::move(transport));
        } else {
          client->impl =
              std::make_unique<aliyun_sls::Client>(std::move(cpp_config));
        }
        return client;
    }
    catch (...) {
        return nullptr;
    }
}

extern "C" void aliyun_sls_client_destroy(aliyun_sls_client_t* client) { delete client; }

extern "C" aliyun_sls_log_group_t* aliyun_sls_log_group_create(const char* topic,
                                                               const char* source) {
    try {
        auto* group = new aliyun_sls_log_group;
        group->impl.topic = safeString(topic);
        group->impl.source = safeString(source);
        return group;
    }
    catch (...) {
        return nullptr;
    }
}

extern "C" void aliyun_sls_log_group_destroy(aliyun_sls_log_group_t* group) { delete group; }

extern "C" aliyun_sls_log_item_t*
aliyun_sls_log_group_add_log(aliyun_sls_log_group_t* group, uint32_t time_sec, uint32_t time_ns) {
    if (group == nullptr) {
        return nullptr;
    }

    try {
        group->impl.logs.emplace_back();
        auto& log = group->impl.logs.back();
        log.time = time_sec;
        log.time_ns = time_ns;

        auto* item = new aliyun_sls_log_item;
        item->group = group;
        item->index = group->impl.logs.size() - 1;
        return item;
    }
    catch (...) {
        return nullptr;
    }
}

extern "C" void aliyun_sls_log_item_destroy(aliyun_sls_log_item_t *item) {
  if (item == nullptr) {
    return;
  }
  if (item->group != nullptr && item->index < item->group->impl.logs.size()) {
    try {
      auto encoded =
          aliyun_sls::protobuf::encodeLog(item->group->impl.logs[item->index]);
      // Wire contribution: field tag (1 byte) + varint(length) + encoded bytes
      std::size_t len = encoded.size();
      std::size_t varint_len = 1;
      for (std::size_t v = len; v >= 0x80; v >>= 7)
        ++varint_len;
      item->group->encoded_size += 1 + varint_len + len;
    } catch (...) {
    }
  }
  delete item;
}

extern "C" int aliyun_sls_log_item_add_content(aliyun_sls_log_item_t* item, const char* key,
                                               const char* value) {
    if (item == nullptr || item->group == nullptr || key == nullptr ||
        item->index >= item->group->impl.logs.size()) {
        return -1;
    }

    try {
        item->group->impl.logs[item->index].contents.push_back(
            {safeString(key), safeString(value)});
        return 0;
    }
    catch (...) {
        return -1;
    }
}

extern "C" size_t
aliyun_sls_log_group_encoded_size(const aliyun_sls_log_group_t *group) {
  if (group == nullptr) {
    return 0;
  }
  // Running sum of per-log wire sizes (accumulated in log_item_destroy)
  std::size_t size = group->encoded_size;
  // Add topic/source string overhead (tag + varint length + bytes)
  const auto &topic = group->impl.topic;
  const auto &source = group->impl.source;
  if (!topic.empty()) {
    std::size_t tlen = topic.size();
    std::size_t tv = 1;
    for (std::size_t v = tlen; v >= 0x80; v >>= 7)
      ++tv;
    size += 1 + tv + tlen;
  }
  if (!source.empty()) {
    std::size_t slen = source.size();
    std::size_t sv = 1;
    for (std::size_t v = slen; v >= 0x80; v >>= 7)
      ++sv;
    size += 1 + sv + slen;
  }
  return size;
}

extern "C" aliyun_sls_put_result_t aliyun_sls_client_put_logs(aliyun_sls_client_t* client,
                                                              aliyun_sls_log_group_t* group,
                                                              const char* hash_key) {
    if (client == nullptr || group == nullptr || !client->impl) {
        return makeFatal("invalid SLS client or log group");
    }

    try {
        aliyun_sls::PutLogsRequest request;
        request.group = group->impl;
        request.hash_key = safeString(hash_key);
        const auto cpp_result = client->impl->putLogs(request);

        aliyun_sls_put_result_t result{};
        result.code = convertCode(cpp_result.code);
        result.http_status = cpp_result.http_status;
        result.request_id = dupString(cpp_result.request_id);
        result.error_code = dupString(cpp_result.error_code);
        result.error_message = dupString(cpp_result.error_message);
        return result;
    }
    catch (const std::exception& e) {
        return makeFatal(e.what());
    }
    catch (...) {
        return makeFatal("unknown fatal error");
    }
}

extern "C" void aliyun_sls_put_result_destroy(aliyun_sls_put_result_t* result) {
    if (result == nullptr) {
        return;
    }
    std::free(const_cast<char*>(result->request_id));
    std::free(const_cast<char*>(result->error_code));
    std::free(const_cast<char*>(result->error_message));
    result->request_id = nullptr;
    result->error_code = nullptr;
    result->error_message = nullptr;
}
