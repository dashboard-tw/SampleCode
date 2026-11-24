// dashboard.tw MCP 伺服器：JavaScript（Node.js 18 以上）範例，只用內建的 fetch。
//
// 用 API 憑證連上 MCP 伺服器，列出工具，再查石門水庫的最新水情。
//
// 用法：
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   node example.mjs
//
// 這裡直接送 JSON-RPC，方便看懂 MCP 的運作方式。正式專案建議用官方的 MCP SDK（npm install @modelcontextprotocol/sdk）。
// 請在伺服器端使用。Client Secret 不能放在網頁前端的程式碼裡。

const BASE_URL = (process.env.DASHBOARD_TW_BASE_URL ?? 'https://dashboard.tw').replace(/\/$/, '')
const PROTOCOL_VERSION = '2025-06-18'

class MCPClient {
  #sessionId = null
  #nextId = 0

  constructor(url, credential) {
    this.url = url
    this.credential = credential
  }

  async #post(message) {
    const headers = {
      // 不支援 OAuth 登入時，可以直接用「Client Id:Client Secret」當作 Bearer token
      Authorization: `Bearer ${this.credential}`,
      'Content-Type': 'application/json',
      Accept: 'application/json, text/event-stream',
      'MCP-Protocol-Version': PROTOCOL_VERSION,
    }
    if (this.#sessionId) headers['Mcp-Session-Id'] = this.#sessionId
    const res = await fetch(this.url, { method: 'POST', headers, body: JSON.stringify(message) })
    if (!res.ok) throw new Error(`MCP 錯誤：${res.status} ${await res.text()}`)
    this.#sessionId = res.headers.get('mcp-session-id') ?? this.#sessionId
    let text = await res.text()
    if (!text.trim()) return null
    // 伺服器可能用 SSE 回應：取出 data: 開頭的那幾行
    if ((res.headers.get('content-type') ?? '').includes('text/event-stream')) {
      text = text
        .split('\n')
        .filter((line) => line.startsWith('data:'))
        .map((line) => line.slice(5).trim())
        .join('\n')
    }
    return JSON.parse(text)
  }

  async request(method, params = {}) {
    const answer = await this.#post({ jsonrpc: '2.0', id: ++this.#nextId, method, params })
    if (answer.error) throw new Error(`${method} 失敗：${answer.error.message}`)
    return answer.result
  }

  async notify(method) {
    await this.#post({ jsonrpc: '2.0', method })
  }

  async callTool(name, args) {
    const result = await this.request('tools/call', { name, arguments: args })
    if (result.isError) throw new Error(`${name} 失敗：${result.content[0].text}`)
    return result.structuredContent
  }
}

const credential = `${process.env.DASHBOARD_TW_CLIENT_ID}:${process.env.DASHBOARD_TW_CLIENT_SECRET}`
const mcp = new MCPClient(`${BASE_URL}/mcp`, credential)

// 1. 建立連線
const info = await mcp.request('initialize', {
  protocolVersion: PROTOCOL_VERSION,
  capabilities: {},
  clientInfo: { name: 'sample', version: '1.0' },
})
await mcp.notify('notifications/initialized')
console.log('已連上：', info.serverInfo.name)

// 2. 列出可用的工具
const { tools } = await mcp.request('tools/list')
console.log('工具：', tools.map((t) => t.name).join('、'))

// 3. 查測站代碼不算額度；取資料（get_latest）會算一次 API 請求
const sites = await mcp.callTool('list_sites', { dataset: 'reservoir_obs', name: '石門' })
const site = sites.sites[0]
const latest = await mcp.callTool('get_latest', {
  dataset: 'reservoir_obs',
  site: String(site[sites.id_field]),
  limit: 1,
})
const row = latest.rows[0]
console.log(`${site.name}：蓄水率 ${row.storage_pct}%，資料時間 ${row.observed_at}`)
