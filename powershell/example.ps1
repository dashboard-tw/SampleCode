# dashboard.tw 開放資料 API：PowerShell 7 範例。
#
# 用法：
#   $env:DASHBOARD_TW_CLIENT_ID = "1a2b3c4d-xxxx-xxxx-xxxx"
#   $env:DASHBOARD_TW_CLIENT_SECRET = "..."
#   ./example.ps1

$ErrorActionPreference = 'Stop'
$BaseUrl = ($env:DASHBOARD_TW_BASE_URL ?? 'https://dashboard.tw').TrimEnd('/')
if (-not $env:DASHBOARD_TW_CLIENT_ID -or -not $env:DASHBOARD_TW_CLIENT_SECRET) {
    throw '請設定 DASHBOARD_TW_CLIENT_ID 與 DASHBOARD_TW_CLIENT_SECRET'
}

$script:Token = $null
$script:ExpiresAt = [datetime]::MinValue

# 以 Client Id 與 Client Secret 換取 Access Token，快到期才重新取得
function Get-DashboardToken {
    if ($script:Token -and (Get-Date) -lt $script:ExpiresAt) { return $script:Token }
    $answer = Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/v1/auth/token" -Body @{
        grant_type    = 'client_credentials'
        client_id     = $env:DASHBOARD_TW_CLIENT_ID
        client_secret = $env:DASHBOARD_TW_CLIENT_SECRET
    }
    $script:Token = $answer.access_token
    # 提早 60 秒更新，避免請求途中過期
    $script:ExpiresAt = (Get-Date).AddSeconds($answer.expires_in - 60)
    return $script:Token
}

# 呼叫 API，回傳 @{ Body; ETag }。帶上 ETag 且資料沒變時，Body 是 $null。
# token 失效時重新取得一次再試。
function Invoke-DashboardApi([string]$PathAndQuery, [string]$ETag) {
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        $headers = @{ Authorization = "Bearer $(Get-DashboardToken)" }
        if ($ETag) { $headers['If-None-Match'] = $ETag }
        $res = Invoke-WebRequest -Uri "$BaseUrl/api/v1$PathAndQuery" -Headers $headers -SkipHttpErrorCheck
        # 資料沒變：304 不帶內容，也不算流量
        if ($res.StatusCode -eq 304) { return @{ Body = $null; ETag = $ETag } }
        # token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
        if ($res.StatusCode -eq 401 -and $attempt -eq 0) {
            $script:Token = $null
            continue
        }
        if ($res.StatusCode -ne 200) { throw "API 錯誤：$($res.StatusCode) $($res.Content)" }
        return @{ Body = ($res.Content | ConvertFrom-Json); ETag = "$($res.Headers['ETag'])" }
    }
}

$path = '/datasets/aqi_obs/latest?county=63000'
$first = Invoke-DashboardApi $path
"資料來源：$($first.Body.meta.source.agency)"
foreach ($row in $first.Body.data) {
    "測站 $($row.site_id)`tAQI $($row.aqi)`t$($row.status)"
}

# 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
$again = Invoke-DashboardApi $path $first.ETag
if ($null -eq $again.Body) { '資料沒有變動（304）' } else { '資料已更新' }
