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

# 呼叫 API；token 失效時重新取得一次再試
function Invoke-DashboardApi([string]$PathAndQuery) {
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        try {
            return Invoke-RestMethod -Uri "$BaseUrl/api/v1$PathAndQuery" `
                -Headers @{ Authorization = "Bearer $(Get-DashboardToken)" }
        } catch {
            if ($_.Exception.Response.StatusCode.value__ -eq 401 -and $attempt -eq 0) {
                $script:Token = $null
                continue
            }
            throw
        }
    }
}

$body = Invoke-DashboardApi '/datasets/aqi_obs/latest?county=63000'
"資料來源：$($body.meta.source.agency)"
foreach ($row in $body.data) {
    "測站 $($row.site_id)`tAQI $($row.aqi)`t$($row.status)"
}
