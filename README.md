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
| Kotlin | [kotlin](kotlin) | Kotlin 1.9 以上，JVM 17 以上 |
| Rust | [rust](rust) | Rust 1.70 以上，reqwest、serde_json |
| C | [c](c) | C99、libcurl |
| C++ | [cpp](cpp) | C++17、libcurl、nlohmann/json、CMake |
| PHP | [php](php) | PHP 8.1 以上、curl 擴充功能 |
| Ruby | [ruby](ruby) | Ruby 3.0 以上，只用標準函式庫 |
| Swift | [swift](swift) | Swift 5.7 以上，只用 Foundation |
| PowerShell | [powershell](powershell) | PowerShell 7 以上 |
| R | [r](r) | R 4.1 以上、httr2 |

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

每個帳號每天 600 次、300 MB 流量，每分鐘 30 次，所有憑證共用，臺灣時間午夜歸零。超過時回應 `429`，請依 `Retry-After` 標頭等待後再試。換取 token 不計入額度，但請重複使用 token，不要每次呼叫都重新取得。

流量以回應內容的大小計算。定時查詢最新資料時，請記下回應的 `ETag`，下次帶上 `If-None-Match` 標頭。資料沒變會回應 `304`，不帶內容。304 一樣算一次，但不算流量。

應用上架到 [應用活化](https://dashboard.tw/showcase/app) 後，帳號會自動改用合作額度：每天 3,000 次、1.5 GB 流量，每分鐘 60 次。名下已沒有上架中的應用時，例如應用被下架或刪除，額度會自動恢復為一般額度。

## 用 AI 工具連接（MCP）

Claude、Claude Code、ChatGPT、Cursor、Windsurf、VS Code、Cline 等支援 MCP 的 AI 工具，可以直接查詢 dashboard.tw 的資料與 API 文件，不用寫程式。

在 AI 工具新增 MCP 伺服器，填入網址 `https://dashboard.tw/mcp`，按下連接後登入 dashboard.tw 並授權即可。以 Claude Code 為例：

```bash
claude mcp add --transport http dashboard-tw https://dashboard.tw/mcp
```

不支援登入授權的工具，可以把 API 憑證直接放在標頭：

```http
Authorization: Bearer <Client Id>:<Client Secret>
```

獲取資料的工具會跟 API 共用每日額度，搜尋與文件類工具不算額度。各工具的設定方式見 <https://dashboard.tw/developers/mcp>。

## 更多資訊

- API 文件：<https://dashboard.tw/developers>
- 使用條款與 API 使用規範：<https://dashboard.tw/terms>

## 授權

範例程式以 [MIT License](LICENSE) 授權，可自由複製、修改並用在你的專案中。API 提供的資料依各來源機關的授權條款使用，請見 dashboard.tw 的資料來源說明。
