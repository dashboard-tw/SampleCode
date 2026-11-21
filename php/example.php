<?php
// dashboard.tw 開放資料 API：PHP 8 範例，使用內建的 curl 擴充功能。
//
// 用法：
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   php example.php

declare(strict_types=1);

/** 取得並快取 Access Token，token 快到期或失效時自動重新取得。 */
final class DashboardTW
{
    private ?string $token = null;
    private int $expiresAt = 0;

    public function __construct(
        private string $baseUrl,
        private string $clientId,
        private string $clientSecret,
    ) {
        $this->baseUrl = rtrim($baseUrl, '/');
    }

    /** @return array{0:int,1:string} */
    private function request(string $url, ?array $form = null, ?string $bearer = null): array
    {
        $ch = curl_init($url);
        curl_setopt_array($ch, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 30,
            CURLOPT_HTTPHEADER => $bearer ? ["Authorization: Bearer {$bearer}"] : [],
        ]);
        if ($form !== null) {
            curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($form));
        }
        $body = curl_exec($ch);
        if ($body === false) {
            throw new RuntimeException(curl_error($ch));
        }
        $status = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
        curl_close($ch);
        return [$status, $body];
    }

    private function fetchToken(): string
    {
        [$status, $body] = $this->request("{$this->baseUrl}/api/v1/auth/token", [
            'grant_type' => 'client_credentials',
            'client_id' => $this->clientId,
            'client_secret' => $this->clientSecret,
        ]);
        if ($status !== 200) {
            throw new RuntimeException("取得 token 失敗：{$status} {$body}");
        }
        $answer = json_decode($body, true, flags: JSON_THROW_ON_ERROR);
        $this->token = $answer['access_token'];
        // 提早 60 秒更新，避免請求途中過期
        $this->expiresAt = time() + $answer['expires_in'] - 60;
        return $this->token;
    }

    public function token(): string
    {
        return ($this->token === null || time() >= $this->expiresAt) ? $this->fetchToken() : $this->token;
    }

    public function get(string $path, array $params = []): array
    {
        $url = "{$this->baseUrl}/api/v1{$path}" . ($params ? '?' . http_build_query($params) : '');
        for ($attempt = 0; $attempt < 2; $attempt++) {
            [$status, $body] = $this->request($url, bearer: $this->token());
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if ($status === 401 && $attempt === 0) {
                $this->token = null;
                continue;
            }
            if ($status !== 200) {
                throw new RuntimeException("API 錯誤：{$status} {$body}");
            }
            return json_decode($body, true, flags: JSON_THROW_ON_ERROR);
        }
        throw new RuntimeException('API 錯誤：token 無效');
    }
}

$api = new DashboardTW(
    getenv('DASHBOARD_TW_BASE_URL') ?: 'https://dashboard.tw',
    getenv('DASHBOARD_TW_CLIENT_ID') ?: '',
    getenv('DASHBOARD_TW_CLIENT_SECRET') ?: '',
);
$body = $api->get('/datasets/aqi_obs/latest', ['county' => '63000']);
echo '資料來源：', $body['meta']['source']['agency'], PHP_EOL;
foreach ($body['data'] as $row) {
    echo "測站 {$row['site_id']}\tAQI {$row['aqi']}\t{$row['status']}", PHP_EOL;
}
