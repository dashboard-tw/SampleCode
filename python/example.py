"""dashboard.tw 開放資料 API：Python 範例（只用標準函式庫）。

用法：
    export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
    export DASHBOARD_TW_CLIENT_SECRET=...
    python example.py
"""

from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request

BASE_URL = os.environ.get("DASHBOARD_TW_BASE_URL", "https://dashboard.tw").rstrip("/")
# 有些防火牆會擋 Python 預設的 User-Agent，請換成能代表你程式的名稱
USER_AGENT = "dashboard-tw-sample/1.0"


class DashboardTW:
    """取得並快取 Access Token，token 快到期或失效時自動重新取得。"""

    def __init__(self, client_id: str, client_secret: str, base_url: str = BASE_URL):
        self.client_id = client_id
        self.client_secret = client_secret
        self.base_url = base_url
        self._token = None
        self._expires_at = 0.0

    def _fetch_token(self) -> str:
        body = urllib.parse.urlencode(
            {
                "grant_type": "client_credentials",
                "client_id": self.client_id,
                "client_secret": self.client_secret,
            }
        ).encode()
        req = urllib.request.Request(
            f"{self.base_url}/api/v1/auth/token",
            data=body,
            headers={"Content-Type": "application/x-www-form-urlencoded", "User-Agent": USER_AGENT},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=30) as res:
            answer = json.load(res)
        self._token = answer["access_token"]
        # 提早 60 秒更新，避免請求途中過期
        self._expires_at = time.time() + answer["expires_in"] - 60
        return self._token

    def token(self) -> str:
        if self._token is None or time.time() >= self._expires_at:
            return self._fetch_token()
        return self._token

    def get(self, path: str, params: dict | None = None, etag: str | None = None) -> tuple[dict | None, str | None]:
        """呼叫 API，回傳 (JSON 本文, ETag)。帶上 etag 且資料沒變時，本文是 None。"""
        url = f"{self.base_url}/api/v1{path}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        for attempt in range(2):
            headers = {"Authorization": f"Bearer {self.token()}", "User-Agent": USER_AGENT}
            if etag:
                headers["If-None-Match"] = etag
            req = urllib.request.Request(url, headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=30) as res:
                    return json.load(res), res.headers.get("ETag")
            except urllib.error.HTTPError as e:
                # 資料沒變：304 不帶內容，也不算流量
                if e.code == 304:
                    return None, etag
                # token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
                if e.code == 401 and attempt == 0:
                    self._token = None
                    continue
                raise
        raise RuntimeError("unreachable")


def main() -> None:
    api = DashboardTW(os.environ["DASHBOARD_TW_CLIENT_ID"], os.environ["DASHBOARD_TW_CLIENT_SECRET"])
    body, etag = api.get("/datasets/aqi_obs/latest", {"county": "63000"})
    print("資料來源：", body["meta"]["source"]["agency"])
    for row in body["data"]:
        print(f"測站 {row['site_id']}	AQI {row['aqi']}	{row['status']}")

    # 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
    again, etag = api.get("/datasets/aqi_obs/latest", {"county": "63000"}, etag=etag)
    print("資料沒有變動（304）" if again is None else "資料已更新")


if __name__ == "__main__":
    main()
