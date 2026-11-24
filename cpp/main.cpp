// dashboard.tw 開放資料 API：C++17 範例，使用 libcurl 與 nlohmann/json。
//
// 編譯與執行：
//   cmake -B build && cmake --build build
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   ./build/dashboard_tw_sample
#include <curl/curl.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

size_t collect(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

struct Response {
    long status = 0;
    std::string body;
    std::string etag;
};

// 從回應標頭取出 ETag。
size_t onHeader(char* line, size_t size, size_t nitems, void* userdata) {
    std::string h(line, size * nitems);
    if (h.size() > 5 && (h[0] | 0x20) == 'e' && (h[1] | 0x20) == 't' && (h[2] | 0x20) == 'a' &&
        (h[3] | 0x20) == 'g' && h[4] == ':') {
        std::string v = h.substr(5);
        v.erase(0, v.find_first_not_of(" \t"));
        v.erase(v.find_last_not_of(" \r\n") + 1);
        static_cast<Response*>(userdata)->etag = v;
    }
    return size * nitems;
}

// 送出一個請求。form 有值時用 POST（application/x-www-form-urlencoded）。
Response request(const std::string& url, const std::optional<std::string>& form,
                 const std::optional<std::string>& bearer, const std::optional<std::string>& etag = std::nullopt) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init 失敗");
    Response res;
    curl_slist* headers = nullptr;
    if (bearer) headers = curl_slist_append(headers, ("Authorization: Bearer " + *bearer).c_str());
    if (etag) headers = curl_slist_append(headers, ("If-None-Match: " + *etag).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (form) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form->c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) throw std::runtime_error(curl_easy_strerror(rc));
    return res;
}

std::string escape(const std::string& s) {
    CURL* curl = curl_easy_init();
    char* e = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
    std::string out = e;
    curl_free(e);
    curl_easy_cleanup(curl);
    return out;
}

// 取得並快取 Access Token，token 快到期或失效時自動重新取得。
class DashboardTW {
public:
    DashboardTW(std::string base, std::string id, std::string secret)
        : base_(std::move(base)), id_(std::move(id)), secret_(std::move(secret)) {}

    // 呼叫 API。etag 有值且資料沒變時，回應 304，body 是 std::nullopt。
    struct Result {
        std::optional<json> body;
        std::optional<std::string> etag;
    };

    Result get(const std::string& path_and_query, const std::optional<std::string>& etag = std::nullopt) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            Response res = request(base_ + "/api/v1" + path_and_query, std::nullopt, token(), etag);
            // 資料沒變：304 不帶內容，也不算流量
            if (res.status == 304) return {std::nullopt, etag};
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if (res.status == 401 && attempt == 0) {
                token_.reset();
                continue;
            }
            if (res.status != 200) throw std::runtime_error("API 錯誤：" + std::to_string(res.status) + " " + res.body);
            std::optional<std::string> tag;
            if (!res.etag.empty()) tag = res.etag;
            return {json::parse(res.body), tag};
        }
        throw std::runtime_error("API 錯誤：token 無效");
    }

private:
    std::string token() {
        if (!token_ || Clock::now() >= expires_) fetchToken();
        return *token_;
    }

    void fetchToken() {
        std::string form = "grant_type=client_credentials&client_id=" + escape(id_) +
                           "&client_secret=" + escape(secret_);
        Response res = request(base_ + "/api/v1/auth/token", form, std::nullopt);
        if (res.status != 200) throw std::runtime_error("取得 token 失敗：" + std::to_string(res.status) + " " + res.body);
        json answer = json::parse(res.body);
        token_ = answer.at("access_token").get<std::string>();
        // 提早 60 秒更新，避免請求途中過期
        expires_ = Clock::now() + std::chrono::seconds(answer.at("expires_in").get<int>() - 60);
    }

    std::string base_, id_, secret_;
    std::optional<std::string> token_;
    Clock::time_point expires_;
};

std::string env(const char* name, const char* fallback = nullptr) {
    const char* v = std::getenv(name);
    if (v) return v;
    if (fallback) return fallback;
    throw std::runtime_error(std::string("請設定 ") + name);
}

}  // namespace

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    try {
        std::string base = env("DASHBOARD_TW_BASE_URL", "https://dashboard.tw");
        while (!base.empty() && base.back() == '/') base.pop_back();
        DashboardTW api(base, env("DASHBOARD_TW_CLIENT_ID"), env("DASHBOARD_TW_CLIENT_SECRET"));

        const std::string path = "/datasets/aqi_obs/latest?county=63000";
        auto first = api.get(path);
        json& body = *first.body;
        std::cout << "資料來源：" << body["meta"]["source"]["agency"].get<std::string>() << "\n";
        for (const auto& row : body["data"]) {
            std::cout << "測站 " << row["site_id"] << "\tAQI " << row["aqi"] << "\t"
                      << row.value("status", "") << "\n";
        }

        // 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
        auto again = api.get(path, first.etag);
        std::cout << (again.body ? "資料已更新" : "資料沒有變動（304）") << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }
    curl_global_cleanup();
    return 0;
}
