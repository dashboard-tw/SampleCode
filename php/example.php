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

    /** @return array{0:int,1:string,2:?string} 狀態碼、本文、ETag */
    private function request(string $url, ?array $form = null, ?string $bearer = null, ?string $etag = null): array
    {
        $headers = [];
        if ($bearer) {
            $headers[] = "Authorization: Bearer {$bearer}";
        }
        if ($etag) {
            $headers[] = "If-None-Match: {$etag}";
        }
        $responseEtag = null;
        $ch = curl_init($url);
        curl_setopt_array($ch, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 30,
            CURLOPT_HTTPHEADER => $headers,
            CURLOPT_HEADERFUNCTION => function ($ch, string $line) use (&$responseEtag): int {
                if (stripos($line, 'etag:') === 0) {
                    $responseEtag = trim(substr($line, 5));
                }
                return strlen($line);
            },
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
        return [$status, $body, $responseEtag];
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

    /**
     * 呼叫 API，回傳 [JSON 本文, ETag]。帶上 $etag 且資料沒變時，本文是 null。
     *
     * @return array{0:?array,1:?string}
     */
    public function get(string $path, array $params = [], ?string $etag = null): array
    {
        $url = "{$this->baseUrl}/api/v1{$path}" . ($params ? '?' . http_build_query($params) : '');
        for ($attempt = 0; $attempt < 2; $attempt++) {
            [$status, $body, $responseEtag] = $this->request($url, bearer: $this->token(), etag: $etag);
            // 資料沒變：304 不帶內容，也不算流量
            if ($status === 304) {
                return [null, $etag];
            }
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if ($status === 401 && $attempt === 0) {
                $this->token = null;
                continue;
            }
            if ($status !== 200) {
                throw new RuntimeException("API 錯誤：{$status} {$body}");
            }
            return [json_decode($body, true, flags: JSON_THROW_ON_ERROR), $responseEtag];
        }
        throw new RuntimeException('API 錯誤：token 無效');
    }
}

$api = new DashboardTW(
    getenv('DASHBOARD_TW_BASE_URL') ?: 'https://dashboard.tw',
    getenv('DASHBOARD_TW_CLIENT_ID') ?: '',
    getenv('DASHBOARD_TW_CLIENT_SECRET') ?: '',
);
[$body, $etag] = $api->get('/datasets/aqi_obs/latest', ['county' => '63000']);
echo '資料來源：', $body['meta']['source']['agency'], PHP_EOL;
foreach ($body['data'] as $row) {
    echo "測站 {$row['site_id']}\tAQI {$row['aqi']}\t{$row['status']}", PHP_EOL;
}

// 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
[$again] = $api->get('/datasets/aqi_obs/latest', ['county' => '63000'], $etag);
echo $again === null ? '資料沒有變動（304）' : '資料已更新', PHP_EOL;
