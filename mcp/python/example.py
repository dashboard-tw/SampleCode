"""dashboard.tw MCP 伺服器：Python 範例（只用標準函式庫）。

用 API 憑證連上 MCP 伺服器，列出工具，再查石門水庫的最新水情。

用法：
    export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
    export DASHBOARD_TW_CLIENT_SECRET=...
    python example.py

這裡直接送 JSON-RPC，方便看懂 MCP 的運作方式。正式專案建議用官方的 MCP SDK（pip install mcp）。
"""

from __future__ import annotations

import json
import os
import urllib.request

BASE_URL = os.environ.get("DASHBOARD_TW_BASE_URL", "https://dashboard.tw").rstrip("/")
PROTOCOL_VERSION = "2025-06-18"
# 有些防火牆會擋 Python 預設的 User-Agent，請換成能代表你程式的名稱
USER_AGENT = "dashboard-tw-sample/1.0"


class MCPClient:
    def __init__(self, url: str, credential: str):
        self.url = url
        self.credential = credential
        self.session_id = None
        self.next_id = 0

    def _post(self, message: dict) -> dict | None:
        headers = {
            # 不支援 OAuth 登入時，可以直接用「Client Id:Client Secret」當作 Bearer token
            "Authorization": f"Bearer {self.credential}",
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "MCP-Protocol-Version": PROTOCOL_VERSION,
            "User-Agent": USER_AGENT,
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        req = urllib.request.Request(self.url, data=json.dumps(message).encode(), headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=30) as res:
            self.session_id = res.headers.get("Mcp-Session-Id") or self.session_id
            text = res.read().decode()
            if not text.strip():
                return None
            # 伺服器可能用 SSE 回應：取出 data: 開頭的那幾行
            if "text/event-stream" in res.headers.get("Content-Type", ""):
                text = "\n".join(line[5:].strip() for line in text.splitlines() if line.startswith("data:"))
            return json.loads(text)

    def request(self, method: str, params: dict | None = None) -> dict:
        self.next_id += 1
        answer = self._post({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}})
        if "error" in answer:
            raise RuntimeError(f"{method} 失敗：{answer['error']['message']}")
        return answer["result"]

    def notify(self, method: str) -> None:
        self._post({"jsonrpc": "2.0", "method": method})

    def call_tool(self, name: str, arguments: dict) -> dict:
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if result.get("isError"):
            raise RuntimeError(f"{name} 失敗：{result['content'][0]['text']}")
        return result["structuredContent"]


def main() -> None:
    credential = f"{os.environ['DASHBOARD_TW_CLIENT_ID']}:{os.environ['DASHBOARD_TW_CLIENT_SECRET']}"
    mcp = MCPClient(f"{BASE_URL}/mcp", credential)

    # 1. 建立連線
    info = mcp.request(
        "initialize",
        {"protocolVersion": PROTOCOL_VERSION, "capabilities": {}, "clientInfo": {"name": "sample", "version": "1.0"}},
    )
    mcp.notify("notifications/initialized")
    print("已連上：", info["serverInfo"]["name"])

    # 2. 列出可用的工具
    tools = mcp.request("tools/list")["tools"]
    print("工具：", "、".join(t["name"] for t in tools))

    # 3. 查測站代碼不算額度；取資料（get_latest）會算一次 API 請求
    sites = mcp.call_tool("list_sites", {"dataset": "reservoir_obs", "name": "石門"})
    site = sites["sites"][0]
    latest = mcp.call_tool("get_latest", {"dataset": "reservoir_obs", "site": str(site[sites["id_field"]]), "limit": 1})
    row = latest["rows"][0]
    print(f"{site['name']}：蓄水率 {row['storage_pct']}%，資料時間 {row['observed_at']}")


if __name__ == "__main__":
    main()
