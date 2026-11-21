//! dashboard.tw 開放資料 API：Rust 範例。
//!
//! 用法：
//!   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//!   export DASHBOARD_TW_CLIENT_SECRET=...
//!   cargo run

use std::env;
use std::time::{Duration, Instant};

use reqwest::blocking::Client as Http;
use reqwest::StatusCode;
use serde::Deserialize;
use serde_json::Value;

#[derive(Deserialize)]
struct TokenAnswer {
    access_token: String,
    expires_in: u64,
}

/// 取得並快取 Access Token，token 快到期或失效時自動重新取得。
struct DashboardTw {
    base_url: String,
    client_id: String,
    client_secret: String,
    http: Http,
    token: Option<(String, Instant)>,
}

impl DashboardTw {
    fn new(base_url: String, client_id: String, client_secret: String) -> Self {
        let http = Http::builder().timeout(Duration::from_secs(30)).build().expect("http client");
        Self { base_url, client_id, client_secret, http, token: None }
    }

    fn fetch_token(&mut self) -> Result<String, Box<dyn std::error::Error>> {
        let res = self
            .http
            .post(format!("{}/api/v1/auth/token", self.base_url))
            .form(&[
                ("grant_type", "client_credentials"),
                ("client_id", self.client_id.as_str()),
                ("client_secret", self.client_secret.as_str()),
            ])
            .send()?;
        if !res.status().is_success() {
            return Err(format!("取得 token 失敗：{} {}", res.status(), res.text()?).into());
        }
        let answer: TokenAnswer = res.json()?;
        // 提早 60 秒更新，避免請求途中過期
        let expires = Instant::now() + Duration::from_secs(answer.expires_in.saturating_sub(60));
        self.token = Some((answer.access_token.clone(), expires));
        Ok(answer.access_token)
    }

    fn token(&mut self) -> Result<String, Box<dyn std::error::Error>> {
        match &self.token {
            Some((token, expires)) if Instant::now() < *expires => Ok(token.clone()),
            _ => self.fetch_token(),
        }
    }

    /// 呼叫 API，回傳 JSON。
    fn get(&mut self, path_and_query: &str) -> Result<Value, Box<dyn std::error::Error>> {
        let url = format!("{}/api/v1{}", self.base_url, path_and_query);
        for attempt in 0..2 {
            let token = self.token()?;
            let res = self.http.get(&url).bearer_auth(token).send()?;
            // token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
            if res.status() == StatusCode::UNAUTHORIZED && attempt == 0 {
                self.token = None;
                continue;
            }
            if !res.status().is_success() {
                return Err(format!("API 錯誤：{} {}", res.status(), res.text()?).into());
            }
            return Ok(res.json()?);
        }
        Err("API 錯誤：token 無效".into())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let base = env::var("DASHBOARD_TW_BASE_URL").unwrap_or_else(|_| "https://dashboard.tw".into());
    let mut api = DashboardTw::new(
        base.trim_end_matches('/').to_string(),
        env::var("DASHBOARD_TW_CLIENT_ID")?,
        env::var("DASHBOARD_TW_CLIENT_SECRET")?,
    );

    let body = api.get("/datasets/aqi_obs/latest?county=63000")?;
    println!("資料來源：{}", body["meta"]["source"]["agency"].as_str().unwrap_or(""));
    for row in body["data"].as_array().into_iter().flatten() {
        println!("測站 {}\tAQI {}\t{}", row["site_id"], row["aqi"], row["status"].as_str().unwrap_or(""));
    }
    Ok(())
}
