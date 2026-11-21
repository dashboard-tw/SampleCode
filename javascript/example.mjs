// dashboard.tw 開放資料 API：JavaScript（Node.js 18 以上）範例，只用內建的 fetch。
//
// 用法：
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   node example.mjs
//
// 請在伺服器端使用。Client Secret 不能放在網頁前端的程式碼裡。

const BASE_URL = (process.env.DASHBOARD_TW_BASE_URL ?? 'https://dashboard.tw').replace(/\/$/, '')

class DashboardTW {
  #token = null
  #expiresAt = 0

  constructor(clientId, clientSecret, baseUrl = BASE_URL) {
    this.clientId = clientId
    this.clientSecret = clientSecret
    this.baseUrl = baseUrl
  }

  async #fetchToken() {
    const res = await fetch(`${this.baseUrl}/api/v1/auth/token`, {
      method: 'POST',
      body: new URLSearchParams({
        grant_type: 'client_credentials',
        client_id: this.clientId,
        client_secret: this.clientSecret,
      }),
    })
    const answer = await res.json()
    if (!res.ok) throw new Error(`取得 token 失敗：${res.status} ${answer.error ?? ''}`)
    this.#token = answer.access_token
    // 提早 60 秒更新，避免請求途中過期
    this.#expiresAt = Date.now() + (answer.expires_in - 60) * 1000
    return this.#token
  }

  async token() {
    if (!this.#token || Date.now() >= this.#expiresAt) return this.#fetchToken()
    return this.#token
  }

  async get(path, params = {}) {
    const url = new URL(`${this.baseUrl}/api/v1${path}`)
    for (const [k, v] of Object.entries(params)) url.searchParams.set(k, v)
    for (let attempt = 0; attempt < 2; attempt++) {
      const res = await fetch(url, { headers: { Authorization: `Bearer ${await this.token()}` } })
      // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
      if (res.status === 401 && attempt === 0) {
        this.#token = null
        continue
      }
      if (!res.ok) throw new Error(`API 錯誤：${res.status} ${await res.text()}`)
      return res.json()
    }
  }
}

const api = new DashboardTW(process.env.DASHBOARD_TW_CLIENT_ID, process.env.DASHBOARD_TW_CLIENT_SECRET)
const body = await api.get('/datasets/aqi_obs/latest', { county: '63000' })
console.log('資料來源：', body.meta.source.agency)
for (const row of body.data) console.log(`測站 ${row.site_id}\tAQI ${row.aqi}\t${row.status}`)
