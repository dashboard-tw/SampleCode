/*
 * dashboard.tw 開放資料 API：C 範例，使用 libcurl。
 *
 * 編譯與執行：
 *   cc example.c -o example $(curl-config --cflags --libs)
 *   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
 *   export DASHBOARD_TW_CLIENT_SECRET=...
 *   ./example
 *
 * 為了不需要額外套件，這裡用簡單的字串搜尋讀取 token；正式程式建議用 cJSON 等函式庫解析 JSON。
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct buffer {
    char *data;
    size_t len;
};

static size_t collect(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct buffer *b = userdata;
    size_t n = size * nmemb;
    char *grown = realloc(b->data, b->len + n + 1);
    if (!grown) return 0;
    b->data = grown;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

/* 取出 "key":"value" 的 value，寫到 out。 */
static int json_string(const char *json, const char *key, char *out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0;
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end || (size_t)(end - start) >= out_size) return 0;
    memcpy(out, start, end - start);
    out[end - start] = '\0';
    return 1;
}

static long json_number(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *start = strstr(json, pattern);
    return start ? strtol(start + strlen(pattern), NULL, 10) : 0;
}

/* 從回應標頭取出 ETag。 */
struct etag_out {
    char *buf;
    size_t size;
};

static size_t on_header(char *line, size_t size, size_t nitems, void *userdata) {
    size_t n = size * nitems;
    struct etag_out *out = userdata;
    const char *name = "etag:";
    if (out->buf && n > 5) {
        size_t i = 0;
        while (i < 5 && (line[i] | 0x20) == name[i]) i++;
        if (i == 5) {
            size_t start = 5, end = n;
            while (start < end && (line[start] == ' ' || line[start] == '\t')) start++;
            while (end > start && (line[end - 1] == '\r' || line[end - 1] == '\n' || line[end - 1] == ' ')) end--;
            if (end - start < out->size) {
                memcpy(out->buf, line + start, end - start);
                out->buf[end - start] = '\0';
            }
        }
    }
    return n;
}

static char token[256];
static time_t token_expires;

/* 以 Client Id 與 Client Secret 換取 Access Token。成功回傳 1。 */
static int fetch_token(const char *base, const char *id, const char *secret) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    char url[512], *id_enc = curl_easy_escape(curl, id, 0), *secret_enc = curl_easy_escape(curl, secret, 0);
    char form[1024];
    snprintf(url, sizeof url, "%s/api/v1/auth/token", base);
    snprintf(form, sizeof form, "grant_type=client_credentials&client_id=%s&client_secret=%s", id_enc, secret_enc);
    curl_free(id_enc);
    curl_free(secret_enc);

    struct buffer body = {0};
    long status = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    int ok = rc == CURLE_OK && status == 200 && body.data &&
             json_string(body.data, "access_token", token, sizeof token);
    if (ok) {
        /* 提早 60 秒更新，避免請求途中過期 */
        token_expires = time(NULL) + json_number(body.data, "expires_in") - 60;
    } else {
        fprintf(stderr, "取得 token 失敗：%ld %s\n", status, body.data ? body.data : curl_easy_strerror(rc));
    }
    free(body.data);
    return ok;
}

/*
 * 呼叫 API，成功時回傳 JSON 本文（呼叫端負責 free），失敗回傳 NULL。
 * etag 有值時帶上 If-None-Match；資料沒變時回應 304，回傳 NULL 並把 *not_modified 設為 1。
 * etag_out 有值時寫入回應的 ETag。
 */
static char *api_get(const char *base, const char *id, const char *secret, const char *path, const char *etag,
                     char *etag_out, size_t etag_out_size, int *not_modified) {
    char url[512], auth[300], match[200];
    snprintf(url, sizeof url, "%s/api/v1%s", base, path);
    *not_modified = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (token[0] == '\0' || time(NULL) >= token_expires) {
            if (!fetch_token(base, id, secret)) return NULL;
        }
        CURL *curl = curl_easy_init();
        if (!curl) return NULL;
        struct curl_slist *headers = NULL;
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth);
        if (etag && etag[0]) {
            snprintf(match, sizeof match, "If-None-Match: %s", etag);
            headers = curl_slist_append(headers, match);
        }
        struct buffer body = {0};
        struct etag_out tag = {etag_out, etag_out_size};
        long status = 0;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &tag);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        /* 資料沒變：304 不帶內容，也不算流量 */
        if (rc == CURLE_OK && status == 304) {
            *not_modified = 1;
            free(body.data);
            return NULL;
        }
        /* token 失效（例如憑證重新產生 Secret）：重新取得後再試一次 */
        if (rc == CURLE_OK && status == 401 && attempt == 0) {
            token[0] = '\0';
            free(body.data);
            continue;
        }
        if (rc != CURLE_OK || status != 200) {
            fprintf(stderr, "API 錯誤：%ld %s\n", status, body.data ? body.data : curl_easy_strerror(rc));
            free(body.data);
            return NULL;
        }
        return body.data;
    }
    return NULL;
}

int main(void) {
    const char *base = getenv("DASHBOARD_TW_BASE_URL");
    const char *id = getenv("DASHBOARD_TW_CLIENT_ID");
    const char *secret = getenv("DASHBOARD_TW_CLIENT_SECRET");
    if (!base) base = "https://dashboard.tw";
    if (!id || !secret) {
        fprintf(stderr, "請設定 DASHBOARD_TW_CLIENT_ID 與 DASHBOARD_TW_CLIENT_SECRET\n");
        return 1;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const char *path = "/datasets/aqi_obs/latest?county=63000";
    char etag[128] = "";
    int not_modified = 0;
    char *body = api_get(base, id, secret, path, NULL, etag, sizeof etag, &not_modified);
    if (!body) {
        curl_global_cleanup();
        return 1;
    }
    puts(body);
    free(body);

    /* 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量 */
    char *again = api_get(base, id, secret, path, etag, NULL, 0, &not_modified);
    if (not_modified) {
        puts("資料沒有變動（304）");
    } else if (again) {
        puts("資料已更新");
        free(again);
    }
    curl_global_cleanup();
    return 0;
}
