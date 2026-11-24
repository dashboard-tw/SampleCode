// dashboard.tw 開放資料 API：Java 17 以上範例，只用 JDK 內建的 HttpClient。
//
// 用法：
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   java DashboardTW.java
//
// 為了不需要額外套件，這裡用簡單的字串比對讀取 token；正式程式建議改用 Jackson 或 Gson 解析 JSON。

import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

public class DashboardTW {
    private final String baseUrl;
    private final String clientId;
    private final String clientSecret;
    private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(30)).build();

    private String token;
    private Instant expiresAt = Instant.EPOCH;

    public DashboardTW(String baseUrl, String clientId, String clientSecret) {
        this.baseUrl = baseUrl.replaceAll("/+$", "");
        this.clientId = clientId;
        this.clientSecret = clientSecret;
    }

    private static String form(Map<String, String> values) {
        return values.entrySet().stream()
                .map(e -> URLEncoder.encode(e.getKey(), StandardCharsets.UTF_8) + "="
                        + URLEncoder.encode(e.getValue(), StandardCharsets.UTF_8))
                .collect(Collectors.joining("&"));
    }

    private static String jsonString(String json, String key) {
        Matcher m = Pattern.compile("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"").matcher(json);
        return m.find() ? m.group(1) : null;
    }

    private static int jsonInt(String json, String key) {
        Matcher m = Pattern.compile("\"" + key + "\"\\s*:\\s*(\\d+)").matcher(json);
        return m.find() ? Integer.parseInt(m.group(1)) : 0;
    }

    private synchronized String fetchToken() throws Exception {
        HttpRequest req = HttpRequest.newBuilder(URI.create(baseUrl + "/api/v1/auth/token"))
                .header("Content-Type", "application/x-www-form-urlencoded")
                .POST(HttpRequest.BodyPublishers.ofString(form(Map.of(
                        "grant_type", "client_credentials",
                        "client_id", clientId,
                        "client_secret", clientSecret))))
                .build();
        HttpResponse<String> res = http.send(req, HttpResponse.BodyHandlers.ofString());
        if (res.statusCode() != 200) {
            throw new IllegalStateException("取得 token 失敗：" + res.statusCode() + " " + res.body());
        }
        token = jsonString(res.body(), "access_token");
        // 提早 60 秒更新，避免請求途中過期
        expiresAt = Instant.now().plusSeconds(jsonInt(res.body(), "expires_in") - 60L);
        return token;
    }

    public synchronized String token() throws Exception {
        if (token == null || Instant.now().isAfter(expiresAt)) {
            return fetchToken();
        }
        return token;
    }

    /** API 回應：本文與 ETag。帶上 ETag 且資料沒變時，本文是 null。 */
    public record Result(String body, String etag) {
    }

    /** 呼叫 API。etag 可以是 null；有值且資料沒變時，回應 304，本文是 null。 */
    public Result get(String pathAndQuery, String etag) throws Exception {
        for (int attempt = 0; attempt < 2; attempt++) {
            HttpRequest.Builder builder = HttpRequest.newBuilder(URI.create(baseUrl + "/api/v1" + pathAndQuery))
                    .header("Authorization", "Bearer " + token())
                    .GET();
            if (etag != null) {
                builder.header("If-None-Match", etag);
            }
            HttpResponse<String> res = http.send(builder.build(), HttpResponse.BodyHandlers.ofString());
            // 資料沒變：304 不帶內容，也不算流量
            if (res.statusCode() == 304) {
                return new Result(null, etag);
            }
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if (res.statusCode() == 401 && attempt == 0) {
                synchronized (this) {
                    token = null;
                }
                continue;
            }
            if (res.statusCode() != 200) {
                throw new IllegalStateException("API 錯誤：" + res.statusCode() + " " + res.body());
            }
            return new Result(res.body(), res.headers().firstValue("ETag").orElse(null));
        }
        throw new IllegalStateException("API 錯誤：token 無效");
    }

    public static void main(String[] args) throws Exception {
        String base = System.getenv().getOrDefault("DASHBOARD_TW_BASE_URL", "https://dashboard.tw");
        DashboardTW api = new DashboardTW(base,
                System.getenv("DASHBOARD_TW_CLIENT_ID"),
                System.getenv("DASHBOARD_TW_CLIENT_SECRET"));
        Result first = api.get("/datasets/aqi_obs/latest?county=63000", null);
        System.out.println(first.body());

        // 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
        Result again = api.get("/datasets/aqi_obs/latest?county=63000", first.etag());
        System.out.println(again.body() == null ? "資料沒有變動（304）" : "資料已更新");
    }
}
