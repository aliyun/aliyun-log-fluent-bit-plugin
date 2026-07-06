#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aliyun_sls_client aliyun_sls_client_t;
typedef struct aliyun_sls_log_group aliyun_sls_log_group_t;
typedef struct aliyun_sls_log_item aliyun_sls_log_item_t;

typedef enum aliyun_sls_result_code {
    ALIYUN_SLS_OK = 0,
    ALIYUN_SLS_RETRYABLE = 1,
    ALIYUN_SLS_FATAL = 2
} aliyun_sls_result_code_t;

typedef struct aliyun_sls_http_request {
  const char *method;
  const char *url;
  const char *connect_host;
  const char *path_with_query;
  const uint8_t *body;
  size_t body_size;
  int timeout_ms;
  const char **header_keys;
  const char **header_values;
  size_t header_count;
} aliyun_sls_http_request_t;

typedef struct aliyun_sls_http_response {
  int status;
  char *body;
  size_t body_size;
  char **header_keys;
  char **header_values;
  size_t header_count;
} aliyun_sls_http_response_t;

typedef int (*aliyun_sls_transport_fn)(const aliyun_sls_http_request_t *request,
                                       aliyun_sls_http_response_t *response,
                                       void *user_data);

typedef struct aliyun_sls_client_config {
    const char* endpoint;
    const char* project;
    const char* logstore;
    const char* access_key_id;
    const char* access_key_secret;
    const char* security_token;
    int request_timeout_ms;
    aliyun_sls_transport_fn transport;
    void *transport_user_data;
} aliyun_sls_client_config_t;

typedef struct aliyun_sls_put_result {
    aliyun_sls_result_code_t code;
    int http_status;
    const char* request_id;
    const char* error_code;
    const char* error_message;
} aliyun_sls_put_result_t;

aliyun_sls_client_t* aliyun_sls_client_create(const aliyun_sls_client_config_t* config);

void aliyun_sls_http_response_cleanup(aliyun_sls_http_response_t *response);
void aliyun_sls_client_destroy(aliyun_sls_client_t* client);

aliyun_sls_log_group_t* aliyun_sls_log_group_create(const char* topic, const char* source);
void aliyun_sls_log_group_destroy(aliyun_sls_log_group_t* group);

aliyun_sls_log_item_t* aliyun_sls_log_group_add_log(aliyun_sls_log_group_t* group,
                                                    uint32_t time_sec, uint32_t time_ns);
void aliyun_sls_log_item_destroy(aliyun_sls_log_item_t* item);
int aliyun_sls_log_item_add_content(aliyun_sls_log_item_t* item, const char* key,
                                    const char* value);

size_t aliyun_sls_log_group_encoded_size(const aliyun_sls_log_group_t *group);

aliyun_sls_put_result_t aliyun_sls_client_put_logs(aliyun_sls_client_t* client,
                                                   aliyun_sls_log_group_t* group,
                                                   const char* hash_key);
void aliyun_sls_put_result_destroy(aliyun_sls_put_result_t* result);

#ifdef __cplusplus
}
#endif
