# dashboard.tw 開放資料 API 範例程式

[dashboard.tw](https://dashboard.tw) 開放資料 API 的呼叫範例。API 的身分驗證採 **OAuth 2.0 Client Credentials**：

1. 登入 dashboard.tw，到「API 憑證」建立憑證，取得 **Client Id** 與 **Client Secret**。
2. 以 Client Id 與 Client Secret 向 `POST /api/v1/auth/token` 換取 **Access Token**（有效 24 小時）。
3. 呼叫 API 時在標頭帶上 `Authorization: Bearer <Access Token>`。

每個範例都會：取得 token、在 token 快到期前重複使用、收到 `401` 時重新取得一次，再呼叫「臺北市各空品測站的最新 AQI」。

## 範例

| 語言 | 資料夾 | 需求 |
|---|---|---|
| curl | [curl](curl) | curl、jq |
| Python | [python](python) | Python 3.9 以上，只用標準函式庫 |
| JavaScript | [javascript](javascript) | Node.js 18 以上，只用內建的 fetch |
| Go | [go](go) | Go 1.21 以上，只用標準函式庫 |
| C# | [csharp](csharp) | .NET 8 以上 |
| Java | [java](java) | Java 17 以上，只用 JDK 內建的 HttpClient |

## 設定

範例從環境變數讀取憑證，請不要把 Client Secret 寫進程式碼或 commit 到版本控制：

```bash
export DASHBOARD_TW_CLIENT_ID="1a2b3c4d-xxxx-xxxx-xxxx"
export DASHBOARD_TW_CLIENT_SECRET="..."
```

Windows PowerShell：

```powershell
$env:DASHBOARD_TW_CLIENT_ID = "1a2b3c4d-xxxx-xxxx-xxxx"
$env:DASHBOARD_TW_CLIENT_SECRET = "..."
```

`DASHBOARD_TW_BASE_URL` 可以改連其他伺服器，預設是 `https://dashboard.tw`。

## 取得 Access Token

```http
POST /api/v1/auth/token
Content-Type: application/x-www-form-urlencoded

grant_type=client_credentials&client_id=<Client Id>&client_secret=<Client Secret>
```

也可以把 Client Id 與 Client Secret 放在 HTTP Basic 標頭。成功時回應：

```json
{ "access_token": "...", "token_type": "Bearer", "expires_in": 86400 }
```

失敗時依 OAuth 2.0 格式回應，例如 `401 {"error": "invalid_client"}`。

## 額度

每個帳號每天 500 次、每分鐘 5 次，所有憑證共用，台灣時間午夜歸零。超過時回應 `429`，請依 `Retry-After` 標頭等待後再試。換取 token 不計入額度，但請重複使用 token，不要每次呼叫都重新取得。

## 更多資訊

- API 文件：<https://dashboard.tw/developers>
- 使用條款與 API 使用規範：<https://dashboard.tw/terms>

## 授權

範例程式以 [MIT License](LICENSE) 授權，可自由複製、修改並用在你的專案中。API 提供的資料依各來源機關的授權條款使用，請見 dashboard.tw 的資料來源說明。
