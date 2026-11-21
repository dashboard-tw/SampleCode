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

/* 呼叫 API，成功時回傳 JSON 本文（呼叫端負責 free），失敗回傳 NULL。 */
static char *api_get(const char *base, const char *id, const char *secret, const char *path) {
    char url[512], auth[300];
    snprintf(url, sizeof url, "%s/api/v1%s", base, path);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (token[0] == '\0' || time(NULL) >= token_expires) {
            if (!fetch_token(base, id, secret)) return NULL;
        }
        CURL *curl = curl_easy_init();
        if (!curl) return NULL;
        struct curl_slist *headers = NULL;
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth);
        struct buffer body = {0};
        long status = 0;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

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
    char *body = api_get(base, id, secret, "/datasets/aqi_obs/latest?county=63000");
    if (body) {
        puts(body);
        free(body);
    }
    curl_global_cleanup();
    return body ? 0 : 1;
}
