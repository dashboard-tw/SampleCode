// dashboard.tw 開放資料 API：Swift 5.7 以上範例，只用 Foundation。
//
// 用法：
//   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//   export DASHBOARD_TW_CLIENT_SECRET=...
//   swift main.swift

import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

struct TokenAnswer: Decodable {
    let access_token: String
    let expires_in: Int
}

struct AQIResponse: Decodable {
    struct Row: Decodable {
        let site_id: Int
        let aqi: Int?
        let status: String?
    }
    struct Meta: Decodable {
        struct Source: Decodable { let agency: String }
        let source: Source
    }
    let data: [Row]
    let meta: Meta
}

enum APIError: Error {
    case http(Int, String)
}

/// 取得並快取 Access Token，token 快到期或失效時自動重新取得。
actor DashboardTW {
    private let baseURL: String
    private let clientID: String
    private let clientSecret: String
    private var token: String?
    private var expiresAt = Date.distantPast

    init(baseURL: String, clientID: String, clientSecret: String) {
        self.baseURL = baseURL.hasSuffix("/") ? String(baseURL.dropLast()) : baseURL
        self.clientID = clientID
        self.clientSecret = clientSecret
    }

    private func fetchToken() async throws -> String {
        var components = URLComponents()
        components.queryItems = [
            URLQueryItem(name: "grant_type", value: "client_credentials"),
            URLQueryItem(name: "client_id", value: clientID),
            URLQueryItem(name: "client_secret", value: clientSecret),
        ]
        var req = URLRequest(url: URL(string: "\(baseURL)/api/v1/auth/token")!)
        req.httpMethod = "POST"
        req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        req.httpBody = components.percentEncodedQuery?.data(using: .utf8)
        let (data, res) = try await URLSession.shared.data(for: req)
        let status = (res as? HTTPURLResponse)?.statusCode ?? 0
        guard status == 200 else { throw APIError.http(status, String(decoding: data, as: UTF8.self)) }
        let answer = try JSONDecoder().decode(TokenAnswer.self, from: data)
        token = answer.access_token
        // 提早 60 秒更新，避免請求途中過期
        expiresAt = Date().addingTimeInterval(TimeInterval(answer.expires_in - 60))
        return answer.access_token
    }

    func currentToken() async throws -> String {
        if let token, Date() < expiresAt { return token }
        return try await fetchToken()
    }

    /// 呼叫 API，回傳 JSON 本文。
    /// 呼叫 API，回傳本文與 ETag。帶上 etag 且資料沒變時，本文是 nil。
    func get(_ pathAndQuery: String, etag: String? = nil) async throws -> (data: Data?, etag: String?) {
        for attempt in 0..<2 {
            var req = URLRequest(url: URL(string: "\(baseURL)/api/v1\(pathAndQuery)")!)
            // 不用 URLSession 的快取，才看得到伺服器回應的 304
            req.cachePolicy = .reloadIgnoringLocalCacheData
            req.setValue("Bearer \(try await currentToken())", forHTTPHeaderField: "Authorization")
            if let etag { req.setValue(etag, forHTTPHeaderField: "If-None-Match") }
            let (data, res) = try await URLSession.shared.data(for: req)
            let http = res as? HTTPURLResponse
            let status = http?.statusCode ?? 0
            // 資料沒變：304 不帶內容，也不算流量
            if status == 304 { return (nil, etag) }
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if status == 401 && attempt == 0 {
                token = nil
                continue
            }
            guard status == 200 else { throw APIError.http(status, String(decoding: data, as: UTF8.self)) }
            return (data, http?.value(forHTTPHeaderField: "ETag"))
        }
        throw APIError.http(401, "token 無效")
    }
}

let env = ProcessInfo.processInfo.environment
let api = DashboardTW(
    baseURL: env["DASHBOARD_TW_BASE_URL"] ?? "https://dashboard.tw",
    clientID: env["DASHBOARD_TW_CLIENT_ID"] ?? "",
    clientSecret: env["DASHBOARD_TW_CLIENT_SECRET"] ?? ""
)
let path = "/datasets/aqi_obs/latest?county=63000"
let first = try await api.get(path)
let body = try JSONDecoder().decode(AQIResponse.self, from: first.data ?? Data())
print("資料來源：\(body.meta.source.agency)")
for row in body.data {
    print("測站 \(row.site_id)\tAQI \(row.aqi.map(String.init) ?? "—")\t\(row.status ?? "")")
}

// 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
let again = try await api.get(path, etag: first.etag)
print(again.data == nil ? "資料沒有變動（304）" : "資料已更新")
