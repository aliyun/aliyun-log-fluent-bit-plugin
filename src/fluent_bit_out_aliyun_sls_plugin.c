/*
 * Aliyun SLS Fluent Bit Output Plugin — pure C implementation.
 *
 * All fluent-bit API interaction lives here (C headers are not C++ compatible).
 * Log event decoding, HTTP transport via flb_http_client, config_map, and
 * cmetrics integration are native. The SLS protocol (signing, protobuf, LZ4)
 * is delegated to the C++ client library through the C API in sls_c_api.h.
 *
 * Targets Fluent Bit >= 3.0 (flb_event_chunk flush, flb_log_event_decoder).
 */

#if defined(ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN)

#include <cfl/cfl_time.h>
#include <cmetrics/cmetrics.h>
#include <cmetrics/cmt_counter.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/flb_log_event_decoder.h>
#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_upstream.h>
#include <fluent-bit/flb_utils.h>
#include <msgpack.h>

#include "aliyun_sls/sls_c_api.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -------------------------------------------------------------------------- */
/* Plugin context */
/* -------------------------------------------------------------------------- */

struct sls_config_props {
  flb_sds_t endpoint;
  flb_sds_t project;
  flb_sds_t logstore;
  flb_sds_t access_key_id;
  flb_sds_t access_key_secret;
  flb_sds_t topic;
  flb_sds_t source;
  flb_sds_t hash_key;
  int port;
  size_t max_raw_bytes_per_batch;
};

struct sls_out_context {
  aliyun_sls_client_t *client;
  struct flb_output_instance *ins;
  struct flb_upstream *upstream;
  int port;
  /* Batch settings */
  flb_sds_t topic;
  flb_sds_t source;
  flb_sds_t hash_key;
  size_t max_raw_bytes_per_batch;
  /* Metrics */
  struct cmt_counter *m_records;
  struct cmt_counter *m_requests;
};

/* -------------------------------------------------------------------------- */
/* flb_upstream transport callback */
/* -------------------------------------------------------------------------- */

static int sls_transport_cb(const aliyun_sls_http_request_t *request,
                            aliyun_sls_http_response_t *response,
                            void *user_data) {
  struct sls_out_context *ctx = (struct sls_out_context *)user_data;
  struct flb_connection *conn;
  struct flb_http_client *hc;
  size_t bytes_sent = 0;
  size_t i;
  int ret;
  const char *host = NULL;
  int method = FLB_HTTP_POST;

  conn = flb_upstream_conn_get(ctx->upstream);
  if (conn == NULL) {
    return -1;
  }

  /* Determine HTTP method */
  if (strcmp(request->method, "GET") == 0) {
    method = FLB_HTTP_GET;
  } else if (strcmp(request->method, "DELETE") == 0) {
    method = FLB_HTTP_DELETE;
  } else if (strcmp(request->method, "PUT") == 0) {
    method = FLB_HTTP_PUT;
  }

  /* Find Host header */
  for (i = 0; i < request->header_count; i++) {
    if (strcasecmp(request->header_keys[i], "Host") == 0) {
      host = request->header_values[i];
      break;
    }
  }

  hc = flb_http_client(conn, method, request->path_with_query,
                       (const char *)request->body, request->body_size, host,
                       ctx->port, NULL, 0);
  if (hc == NULL) {
    flb_upstream_conn_release(conn);
    return -1;
  }

  /* Add all request headers (skip Host which was already passed) */
  for (i = 0; i < request->header_count; i++) {
    if (strcasecmp(request->header_keys[i], "Host") == 0) {
      continue;
    }
    flb_http_add_header(
        hc, request->header_keys[i], strlen(request->header_keys[i]),
        request->header_values[i], strlen(request->header_values[i]));
  }

  ret = flb_http_do(hc, &bytes_sent);
  if (ret != 0) {
    flb_http_client_destroy(hc);
    flb_upstream_conn_release(conn);
    return -1;
  }

  /* Fill response */
  memset(response, 0, sizeof(*response));
  response->status = hc->resp.status;

  if (hc->resp.payload != NULL && hc->resp.payload_size > 0) {
    response->body = (char *)malloc(hc->resp.payload_size);
    if (response->body != NULL) {
      memcpy(response->body, hc->resp.payload, hc->resp.payload_size);
      response->body_size = hc->resp.payload_size;
    }
  }

  /* Parse response headers for x-log-requestid etc. */
  if (hc->resp.data != NULL && hc->resp.data_len > 0) {
    const char *hdr_end =
        hc->resp.payload ? hc->resp.payload : hc->resp.data + hc->resp.data_len;
    /* Count header lines */
    size_t count = 0;
    const char *p = hc->resp.data;
    int first = 1;
    while (p < hdr_end) {
      const char *nl = memchr(p, '\n', (size_t)(hdr_end - p));
      if (nl == NULL)
        break;
      if (!first && nl - p > 1) {
        const char *colon = memchr(p, ':', (size_t)(nl - p));
        if (colon)
          count++;
      }
      first = 0;
      p = nl + 1;
    }

    if (count > 0) {
      response->header_keys = (char **)calloc(count, sizeof(char *));
      response->header_values = (char **)calloc(count, sizeof(char *));
      if (response->header_keys && response->header_values) {
        p = hc->resp.data;
        first = 1;
        size_t idx = 0;
        while (p < hdr_end && idx < count) {
          const char *nl = memchr(p, '\n', (size_t)(hdr_end - p));
          if (nl == NULL)
            break;
          if (!first && nl - p > 1) {
            const char *colon = memchr(p, ':', (size_t)(nl - p));
            if (colon) {
              size_t klen = (size_t)(colon - p);
              const char *vstart = colon + 1;
              while (vstart < nl && (*vstart == ' ' || *vstart == '\t'))
                vstart++;
              const char *vend = nl;
              if (vend > vstart && *(vend - 1) == '\r')
                vend--;
              size_t vlen = (size_t)(vend - vstart);
              response->header_keys[idx] = strndup(p, klen);
              response->header_values[idx] = strndup(vstart, vlen);
              idx++;
            }
          }
          first = 0;
          p = nl + 1;
        }
        response->header_count = idx;
      }
    }
  }

  flb_http_client_destroy(hc);
  flb_upstream_conn_release(conn);
  return 0;
}

/* -------------------------------------------------------------------------- */
/* Msgpack value to string helper (for non-map record values) */
/* -------------------------------------------------------------------------- */

static flb_sds_t msgpack_object_to_sds(const msgpack_object *obj, int depth) {
  flb_sds_t s;
  char buf[64];
  uint32_t i;

  if (depth > 8)
    return flb_sds_create("<nested>");
  if (obj == NULL)
    return flb_sds_create("");

  switch (obj->type) {
  case MSGPACK_OBJECT_NIL:
    return flb_sds_create("");
  case MSGPACK_OBJECT_BOOLEAN:
    return flb_sds_create(obj->via.boolean ? "true" : "false");
  case MSGPACK_OBJECT_POSITIVE_INTEGER:
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)obj->via.u64);
    return flb_sds_create(buf);
  case MSGPACK_OBJECT_NEGATIVE_INTEGER:
    snprintf(buf, sizeof(buf), "%lld", (long long)obj->via.i64);
    return flb_sds_create(buf);
  case MSGPACK_OBJECT_FLOAT32:
  case MSGPACK_OBJECT_FLOAT64:
    snprintf(buf, sizeof(buf), "%g", obj->via.f64);
    return flb_sds_create(buf);
  case MSGPACK_OBJECT_STR:
    return flb_sds_create_len(obj->via.str.ptr, obj->via.str.size);
  case MSGPACK_OBJECT_BIN:
    return flb_sds_create_len(obj->via.bin.ptr, obj->via.bin.size);
  case MSGPACK_OBJECT_MAP:
    s = flb_sds_create("{");
    if (s == NULL)
      return NULL;
    for (i = 0; i < obj->via.map.size; i++) {
      flb_sds_t k = msgpack_object_to_sds(&obj->via.map.ptr[i].key, depth + 1);
      flb_sds_t v = msgpack_object_to_sds(&obj->via.map.ptr[i].val, depth + 1);
      if (k == NULL || v == NULL) {
        flb_sds_destroy(k);
        flb_sds_destroy(v);
        flb_sds_destroy(s);
        return NULL;
      }
      if (i > 0)
        s = flb_sds_cat(s, ",", 1);
      s = flb_sds_cat(s, k, flb_sds_len(k));
      s = flb_sds_cat(s, ":", 1);
      s = flb_sds_cat(s, v, flb_sds_len(v));
      flb_sds_destroy(k);
      flb_sds_destroy(v);
      if (s == NULL)
        return NULL;
    }
    s = flb_sds_cat(s, "}", 1);
    return s;
  case MSGPACK_OBJECT_ARRAY:
    s = flb_sds_create("[");
    if (s == NULL)
      return NULL;
    for (i = 0; i < obj->via.array.size; i++) {
      flb_sds_t e = msgpack_object_to_sds(&obj->via.array.ptr[i], depth + 1);
      if (e == NULL) {
        flb_sds_destroy(s);
        return NULL;
      }
      if (i > 0)
        s = flb_sds_cat(s, ",", 1);
      s = flb_sds_cat(s, e, flb_sds_len(e));
      flb_sds_destroy(e);
      if (s == NULL)
        return NULL;
    }
    s = flb_sds_cat(s, "]", 1);
    return s;
  default:
    return flb_sds_create("");
  }
}

/* -------------------------------------------------------------------------- */
/* Metrics helpers */
/* -------------------------------------------------------------------------- */

static void metric_inc(struct cmt_counter *c, double val, const char *label) {
  uint64_t ts;
  if (c == NULL)
    return;
  ts = cfl_time_now();
  if (label == NULL) {
    cmt_counter_add(c, ts, val, 0, NULL);
  } else {
    char *labels[1] = {(char *)label};
    cmt_counter_add(c, ts, val, 1, labels);
  }
}

/* -------------------------------------------------------------------------- */
/* Plugin callbacks */
/* -------------------------------------------------------------------------- */

static int cb_sls_init(struct flb_output_instance *ins,
                       struct flb_config *config, void *data) {
  struct sls_config_props props;
  struct sls_out_context *ctx;
  struct flb_upstream *upstream;
  int use_tls, port;
  char connect_host[512];
  const char *p;
  aliyun_sls_client_config_t sls_cfg;
  char *label_names[1] = {"result"};

  (void)data;

  memset(&props, 0, sizeof(props));
  if (flb_output_config_map_set(ins, &props) == -1) {
    flb_plg_error(ins, "failed to load aliyun_sls configuration");
    return -1;
  }

  use_tls = ins->use_tls;
  port = props.port > 0 ? props.port : (use_tls ? 443 : 80);

  /* Extract connect host from endpoint */
  p = props.endpoint;
  if (p == NULL) {
    flb_plg_error(ins, "endpoint is required");
    return -1;
  }
  if (strncmp(p, "https://", 8) == 0)
    p += 8;
  else if (strncmp(p, "http://", 7) == 0)
    p += 7;
  snprintf(connect_host, sizeof(connect_host), "%s", p);
  {
    char *slash = strchr(connect_host, '/');
    if (slash)
      *slash = '\0';
    char *colon = strchr(connect_host, ':');
    if (colon)
      *colon = '\0';
  }

  upstream = flb_upstream_create(config, connect_host, port,
                                 use_tls ? FLB_IO_TLS : FLB_IO_TCP, ins->tls);
  if (upstream == NULL) {
    flb_plg_error(ins, "failed to create upstream to %s:%d", connect_host,
                  port);
    return -1;
  }
  flb_output_upstream_set(upstream, ins);

  ctx = (struct sls_out_context *)flb_calloc(1, sizeof(*ctx));
  if (ctx == NULL) {
    flb_upstream_destroy(upstream);
    return -1;
  }
  ctx->ins = ins;
  ctx->upstream = upstream;
  ctx->port = port;
  ctx->topic = props.topic ? flb_sds_create(props.topic) : NULL;
  ctx->source = props.source ? flb_sds_create(props.source) : NULL;
  ctx->hash_key = props.hash_key ? flb_sds_create(props.hash_key) : NULL;
  ctx->max_raw_bytes_per_batch = props.max_raw_bytes_per_batch > 0
                                     ? props.max_raw_bytes_per_batch
                                     : (5 * 1024 * 1024);

  /* Create SLS client with custom transport */
  memset(&sls_cfg, 0, sizeof(sls_cfg));
  sls_cfg.endpoint = props.endpoint;
  sls_cfg.project = props.project;
  sls_cfg.logstore = props.logstore;
  sls_cfg.access_key_id = props.access_key_id;
  sls_cfg.access_key_secret = props.access_key_secret;
  sls_cfg.transport = sls_transport_cb;
  sls_cfg.transport_user_data = ctx;

  ctx->client = aliyun_sls_client_create(&sls_cfg);
  if (ctx->client == NULL) {
    flb_plg_error(ins, "failed to create SLS client");
    flb_upstream_destroy(upstream);
    flb_free(ctx);
    return -1;
  }

  /* Register cmetrics */
  ctx->m_records =
      cmt_counter_create(ins->cmt, "fluentbit", "output", "sls_records_total",
                         "Total log records forwarded to SLS", 0, NULL);
  ctx->m_requests =
      cmt_counter_create(ins->cmt, "fluentbit", "output", "sls_requests_total",
                         "SLS PutLogs requests by result", 1, label_names);

  flb_output_set_context(ins, ctx);
  flb_plg_info(
      ins, "aliyun_sls output ready: endpoint=%s project=%s logstore=%s tls=%s",
      connect_host, props.project ? props.project : "",
      props.logstore ? props.logstore : "", use_tls ? "on" : "off");
  return 0;
}

static void cb_sls_flush(struct flb_event_chunk *event_chunk,
                         struct flb_output_flush *out_flush,
                         struct flb_input_instance *i_ins, void *out_context,
                         struct flb_config *config) {
  struct sls_out_context *ctx = (struct sls_out_context *)out_context;
  struct flb_log_event_decoder decoder;
  struct flb_log_event event;
  aliyun_sls_log_group_t *group = NULL;
  aliyun_sls_log_item_t *item;
  aliyun_sls_put_result_t result;
  const char *source;
  int log_count = 0;
  uint32_t i;

  (void)out_flush;
  (void)i_ins;
  (void)config;

  if (ctx == NULL || ctx->client == NULL) {
    FLB_OUTPUT_RETURN(FLB_ERROR);
  }

  source =
      ctx->source ? ctx->source : (event_chunk->tag ? event_chunk->tag : "");

  if (flb_log_event_decoder_init(&decoder, (char *)event_chunk->data,
                                 event_chunk->size) !=
      FLB_EVENT_DECODER_SUCCESS) {
    FLB_OUTPUT_RETURN(FLB_ERROR);
  }

  group = aliyun_sls_log_group_create(ctx->topic ? ctx->topic : "", source);
  if (group == NULL) {
    flb_log_event_decoder_destroy(&decoder);
    FLB_OUTPUT_RETURN(FLB_ERROR);
  }

  while (flb_log_event_decoder_next(&decoder, &event) ==
         FLB_EVENT_DECODER_SUCCESS) {
    uint32_t sec = (uint32_t)event.timestamp.tm.tv_sec;
    uint32_t nsec = (uint32_t)event.timestamp.tm.tv_nsec;
    if (sec == 0) {
      sec = (uint32_t)time(NULL);
    }

    item = aliyun_sls_log_group_add_log(group, sec, nsec);
    if (item == NULL)
      continue;

    if (event.body && event.body->type == MSGPACK_OBJECT_MAP) {
      const msgpack_object_map *map = &event.body->via.map;
      for (i = 0; i < map->size; i++) {
        flb_sds_t key = msgpack_object_to_sds(&map->ptr[i].key, 0);
        flb_sds_t val = msgpack_object_to_sds(&map->ptr[i].val, 0);
        if (key && flb_sds_len(key) > 0) {
          aliyun_sls_log_item_add_content(item, key, val ? val : "");
        }
        if (key)
          flb_sds_destroy(key);
        if (val)
          flb_sds_destroy(val);
      }
    } else if (event.body) {
      flb_sds_t val = msgpack_object_to_sds(event.body, 0);
      aliyun_sls_log_item_add_content(item, "message", val ? val : "");
      if (val)
        flb_sds_destroy(val);
    } else {
      aliyun_sls_log_item_add_content(item, "message", "");
    }

    aliyun_sls_log_item_destroy(item);
    log_count++;

    /* Flush batch if at size capacity (exact protobuf wire size) */
    if (aliyun_sls_log_group_encoded_size(group) >=
        ctx->max_raw_bytes_per_batch) {
      result = aliyun_sls_client_put_logs(ctx->client, group,
                                          ctx->hash_key ? ctx->hash_key : NULL);
      metric_inc(ctx->m_records, (double)log_count, NULL);
      if (result.code == ALIYUN_SLS_RETRYABLE) {
        metric_inc(ctx->m_requests, 1, "retry");
        flb_plg_warn(
            ctx->ins, "SLS PutLogs retryable: status=%d request_id=%s error=%s",
            result.http_status, result.request_id ? result.request_id : "",
            result.error_message ? result.error_message : "");
        aliyun_sls_put_result_destroy(&result);
        aliyun_sls_log_group_destroy(group);
        flb_log_event_decoder_destroy(&decoder);
        FLB_OUTPUT_RETURN(FLB_RETRY);
      }
      if (result.code == ALIYUN_SLS_FATAL) {
        metric_inc(ctx->m_requests, 1, "error");
        flb_plg_error(
            ctx->ins, "SLS PutLogs fatal: status=%d request_id=%s error=%s",
            result.http_status, result.request_id ? result.request_id : "",
            result.error_message ? result.error_message : "");
        aliyun_sls_put_result_destroy(&result);
        aliyun_sls_log_group_destroy(group);
        flb_log_event_decoder_destroy(&decoder);
        FLB_OUTPUT_RETURN(FLB_ERROR);
      }
      metric_inc(ctx->m_requests, 1, "ok");
      aliyun_sls_put_result_destroy(&result);

      /* Reset for next batch */
      aliyun_sls_log_group_destroy(group);
      group = aliyun_sls_log_group_create(ctx->topic ? ctx->topic : "", source);
      if (group == NULL) {
        flb_log_event_decoder_destroy(&decoder);
        FLB_OUTPUT_RETURN(FLB_ERROR);
      }
      log_count = 0;
    }
  }

  flb_log_event_decoder_destroy(&decoder);

  /* Flush remaining */
  if (log_count > 0 && group != NULL) {
    result = aliyun_sls_client_put_logs(ctx->client, group,
                                        ctx->hash_key ? ctx->hash_key : NULL);
    metric_inc(ctx->m_records, (double)log_count, NULL);
    if (result.code == ALIYUN_SLS_RETRYABLE) {
      metric_inc(ctx->m_requests, 1, "retry");
      flb_plg_warn(
          ctx->ins, "SLS PutLogs retryable: status=%d request_id=%s error=%s",
          result.http_status, result.request_id ? result.request_id : "",
          result.error_message ? result.error_message : "");
      aliyun_sls_put_result_destroy(&result);
      aliyun_sls_log_group_destroy(group);
      FLB_OUTPUT_RETURN(FLB_RETRY);
    }
    if (result.code == ALIYUN_SLS_FATAL) {
      metric_inc(ctx->m_requests, 1, "error");
      flb_plg_error(
          ctx->ins, "SLS PutLogs fatal: status=%d request_id=%s error=%s",
          result.http_status, result.request_id ? result.request_id : "",
          result.error_message ? result.error_message : "");
      aliyun_sls_put_result_destroy(&result);
      aliyun_sls_log_group_destroy(group);
      FLB_OUTPUT_RETURN(FLB_ERROR);
    }
    metric_inc(ctx->m_requests, 1, "ok");
    aliyun_sls_put_result_destroy(&result);
  }

  aliyun_sls_log_group_destroy(group);
  FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_sls_exit(void *data, struct flb_config *config) {
  struct sls_out_context *ctx = (struct sls_out_context *)data;
  (void)config;

  if (ctx == NULL)
    return 0;
  if (ctx->client)
    aliyun_sls_client_destroy(ctx->client);
  if (ctx->upstream)
    flb_upstream_destroy(ctx->upstream);
  if (ctx->topic)
    flb_sds_destroy(ctx->topic);
  if (ctx->source)
    flb_sds_destroy(ctx->source);
  if (ctx->hash_key)
    flb_sds_destroy(ctx->hash_key);
  flb_free(ctx);
  return 0;
}

/* -------------------------------------------------------------------------- */
/* config_map */
/* -------------------------------------------------------------------------- */

static struct flb_config_map config_map[] = {
    {FLB_CONFIG_MAP_STR, "endpoint", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, endpoint),
     "SLS endpoint host, e.g. cn-hangzhou.log.aliyuncs.com"},
    {FLB_CONFIG_MAP_STR, "project", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, project), "SLS project name"},
    {FLB_CONFIG_MAP_STR, "logstore", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, logstore), "SLS logstore name"},
    {FLB_CONFIG_MAP_STR, "access_key_id", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, access_key_id), "Aliyun access key id"},
    {FLB_CONFIG_MAP_STR, "access_key_secret", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, access_key_secret),
     "Aliyun access key secret"},
    {FLB_CONFIG_MAP_STR, "topic", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, topic), "LogGroup topic"},
    {FLB_CONFIG_MAP_STR, "source", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, source),
     "LogGroup source; defaults to the record tag"},
    {FLB_CONFIG_MAP_STR, "hash_key", NULL, 0, FLB_TRUE,
     offsetof(struct sls_config_props, hash_key),
     "Route by hash key (enables KeyHash write mode)"},
    {FLB_CONFIG_MAP_INT, "port", "0", 0, FLB_TRUE,
     offsetof(struct sls_config_props, port),
     "Override endpoint port; defaults to 443 with TLS or 80 without"},
    {FLB_CONFIG_MAP_SIZE, "max_raw_bytes_per_batch", "5242880", 0, FLB_TRUE,
     offsetof(struct sls_config_props, max_raw_bytes_per_batch),
     "Maximum uncompressed bytes per LogGroup"},
    {0}};

/* -------------------------------------------------------------------------- */
/* Plugin registration */
/* -------------------------------------------------------------------------- */

struct flb_output_plugin out_aliyun_sls_plugin = {
    .name = "aliyun_sls",
    .description = "Aliyun SLS output",
    .cb_init = cb_sls_init,
    .cb_flush = cb_sls_flush,
    .cb_exit = cb_sls_exit,
    .config_map = config_map,
    .flags = FLB_OUTPUT_NET | FLB_IO_OPT_TLS,
};

#endif /* ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN */
