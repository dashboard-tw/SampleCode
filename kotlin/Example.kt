// dashboard.tw 開放資料 API：Kotlin（JVM 17 以上）範例，只用 JDK 內建的 HttpClient。
//
// 編譯與執行：
//   kotlinc Example.kt -include-runtime -d example.jar
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   java -jar example.jar
//
// 為了不需要額外套件，這裡用簡單的正規表示式讀取 token；正式程式建議用 kotlinx.serialization 解析 JSON。

import java.net.URI
import java.net.URLEncoder
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.charset.StandardCharsets
import java.time.Duration
import java.time.Instant

/** 取得並快取 Access Token，token 快到期或失效時自動重新取得。 */
class DashboardTW(baseUrl: String, private val clientId: String, private val clientSecret: String) {
    private val baseUrl = baseUrl.trimEnd('/')
    private val http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(30)).build()
    private var token: String? = null
    private var expiresAt: Instant = Instant.EPOCH

    private fun form(values: Map<String, String>) = values.entries.joinToString("&") { (k, v) ->
        URLEncoder.encode(k, StandardCharsets.UTF_8) + "=" + URLEncoder.encode(v, StandardCharsets.UTF_8)
    }

    @Synchronized
    private fun fetchToken(): String {
        val req = HttpRequest.newBuilder(URI.create("$baseUrl/api/v1/auth/token"))
            .header("Content-Type", "application/x-www-form-urlencoded")
            .POST(HttpRequest.BodyPublishers.ofString(form(mapOf(
                "grant_type" to "client_credentials",
                "client_id" to clientId,
                "client_secret" to clientSecret,
            ))))
            .build()
        val res = http.send(req, HttpResponse.BodyHandlers.ofString())
        check(res.statusCode() == 200) { "取得 token 失敗：${res.statusCode()} ${res.body()}" }
        val body = res.body()
        val accessToken = Regex("\"access_token\"\\s*:\\s*\"([^\"]+)\"").find(body)!!.groupValues[1]
        val expiresIn = Regex("\"expires_in\"\\s*:\\s*(\\d+)").find(body)!!.groupValues[1].toLong()
        token = accessToken
        // 提早 60 秒更新，避免請求途中過期
        expiresAt = Instant.now().plusSeconds(expiresIn - 60)
        return accessToken
    }

    @Synchronized
    fun token(): String = token?.takeIf { Instant.now().isBefore(expiresAt) } ?: fetchToken()

    /** API 回應：本文與 ETag。帶上 ETag 且資料沒變時，本文是 null。 */
    data class Result(val body: String?, val etag: String?)

    /** 呼叫 API。etag 有值且資料沒變時，回應 304，本文是 null。 */
    fun get(pathAndQuery: String, etag: String? = null): Result {
        repeat(2) { attempt ->
            val builder = HttpRequest.newBuilder(URI.create("$baseUrl/api/v1$pathAndQuery"))
                .header("Authorization", "Bearer ${token()}")
                .GET()
            if (etag != null) builder.header("If-None-Match", etag)
            val res = http.send(builder.build(), HttpResponse.BodyHandlers.ofString())
            // 資料沒變：304 不帶內容，也不算流量
            if (res.statusCode() == 304) return Result(null, etag)
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if (res.statusCode() == 401 && attempt == 0) {
                synchronized(this) { token = null }
                return@repeat
            }
            check(res.statusCode() == 200) { "API 錯誤：${res.statusCode()} ${res.body()}" }
            return Result(res.body(), res.headers().firstValue("ETag").orElse(null))
        }
        error("API 錯誤：token 無效")
    }
}

fun main() {
    val api = DashboardTW(
        System.getenv("DASHBOARD_TW_BASE_URL") ?: "https://dashboard.tw",
        System.getenv("DASHBOARD_TW_CLIENT_ID") ?: error("請設定 DASHBOARD_TW_CLIENT_ID"),
        System.getenv("DASHBOARD_TW_CLIENT_SECRET") ?: error("請設定 DASHBOARD_TW_CLIENT_SECRET"),
    )
    val first = api.get("/datasets/aqi_obs/latest?county=63000")
    println(first.body)

    // 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
    val again = api.get("/datasets/aqi_obs/latest?county=63000", first.etag)
    println(if (again.body == null) "資料沒有變動（304）" else "資料已更新")
}
