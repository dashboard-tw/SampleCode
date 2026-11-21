"""dashboard.tw 開放資料 API：Python 範例（只用標準函式庫）。

用法：
    export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
    export DASHBOARD_TW_CLIENT_SECRET=...
    python example.py
"""

import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request

BASE_URL = os.environ.get("DASHBOARD_TW_BASE_URL", "https://dashboard.tw").rstrip("/")


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
            headers={"Content-Type": "application/x-www-form-urlencoded"},
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

    def get(self, path: str, params: dict | None = None) -> dict:
        url = f"{self.base_url}/api/v1{path}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        for attempt in range(2):
            req = urllib.request.Request(url, headers={"Authorization": f"Bearer {self.token()}"})
            try:
                with urllib.request.urlopen(req, timeout=30) as res:
                    return json.load(res)
            except urllib.error.HTTPError as e:
                # token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
                if e.code == 401 and attempt == 0:
                    self._token = None
                    continue
                raise
        raise RuntimeError("unreachable")


def main() -> None:
    api = DashboardTW(os.environ["DASHBOARD_TW_CLIENT_ID"], os.environ["DASHBOARD_TW_CLIENT_SECRET"])
    body = api.get("/datasets/aqi_obs/latest", {"county": "63000"})
    print("資料來源：", body["meta"]["source"]["agency"])
    for row in body["data"]:
        print(f"測站 {row['site_id']}\tAQI {row['aqi']}\t{row['status']}")


if __name__ == "__main__":
    main()
