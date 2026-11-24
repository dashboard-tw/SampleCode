// dashboard.tw 開放資料 API：C#（.NET 8 以上）範例。
//
// 用法：
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   dotnet run

using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;

var baseUrl = (Environment.GetEnvironmentVariable("DASHBOARD_TW_BASE_URL") ?? "https://dashboard.tw").TrimEnd('/');
var api = new DashboardTW(
    baseUrl,
    Environment.GetEnvironmentVariable("DASHBOARD_TW_CLIENT_ID") ?? "",
    Environment.GetEnvironmentVariable("DASHBOARD_TW_CLIENT_SECRET") ?? "");

var (body, etag) = await api.GetAsync("/datasets/aqi_obs/latest?county=63000");
var root = body!.RootElement;
Console.WriteLine($"資料來源：{root.GetProperty("meta").GetProperty("source").GetProperty("agency").GetString()}");
foreach (var row in root.GetProperty("data").EnumerateArray())
{
    var aqi = row.GetProperty("aqi").ValueKind == JsonValueKind.Null ? "—" : row.GetProperty("aqi").ToString();
    Console.WriteLine($"測站 {row.GetProperty("site_id")}\tAQI {aqi}\t{row.GetProperty("status").GetString()}");
}

// 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
var (again, _) = await api.GetAsync("/datasets/aqi_obs/latest?county=63000", etag);
Console.WriteLine(again is null ? "資料沒有變動（304）" : "資料已更新");

/// <summary>取得並快取 Access Token，token 快到期或失效時自動重新取得。</summary>
class DashboardTW(string baseUrl, string clientId, string clientSecret)
{
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(30) };
    private readonly SemaphoreSlim gate = new(1, 1);
    private string? token;
    private DateTimeOffset expiresAt;

    private async Task<string> FetchTokenAsync()
    {
        var form = new FormUrlEncodedContent(new Dictionary<string, string>
        {
            ["grant_type"] = "client_credentials",
            ["client_id"] = clientId,
            ["client_secret"] = clientSecret,
        });
        using var res = await http.PostAsync($"{baseUrl}/api/v1/auth/token", form);
        using var answer = JsonDocument.Parse(await res.Content.ReadAsStringAsync());
        if (!res.IsSuccessStatusCode)
        {
            throw new HttpRequestException($"取得 token 失敗：{(int)res.StatusCode} {answer.RootElement.GetProperty("error")}");
        }
        token = answer.RootElement.GetProperty("access_token").GetString();
        // 提早 60 秒更新，避免請求途中過期
        expiresAt = DateTimeOffset.UtcNow.AddSeconds(answer.RootElement.GetProperty("expires_in").GetInt32() - 60);
        return token!;
    }

    public async Task<string> TokenAsync()
    {
        await gate.WaitAsync();
        try
        {
            return token is null || DateTimeOffset.UtcNow >= expiresAt ? await FetchTokenAsync() : token;
        }
        finally
        {
            gate.Release();
        }
    }

    /// <summary>呼叫 API，回傳 JSON 本文與 ETag。帶上 etag 且資料沒變時，本文是 null。</summary>
    public async Task<(JsonDocument? Body, string? ETag)> GetAsync(string pathAndQuery, string? etag = null)
    {
        for (var attempt = 0; attempt < 2; attempt++)
        {
            using var req = new HttpRequestMessage(HttpMethod.Get, $"{baseUrl}/api/v1{pathAndQuery}");
            req.Headers.Authorization = new AuthenticationHeaderValue("Bearer", await TokenAsync());
            if (etag is not null)
            {
                req.Headers.TryAddWithoutValidation("If-None-Match", etag);
            }
            using var res = await http.SendAsync(req);
            // 資料沒變：304 不帶內容，也不算流量
            if (res.StatusCode == HttpStatusCode.NotModified)
            {
                return (null, etag);
            }
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if (res.StatusCode == HttpStatusCode.Unauthorized && attempt == 0)
            {
                token = null;
                continue;
            }
            var text = await res.Content.ReadAsStringAsync();
            if (!res.IsSuccessStatusCode)
            {
                throw new HttpRequestException($"API 錯誤：{(int)res.StatusCode} {text}");
            }
            return (JsonDocument.Parse(text), res.Headers.ETag?.ToString());
        }
        throw new HttpRequestException("API 錯誤：token 無效");
    }
}
