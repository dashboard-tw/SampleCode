# dashboard.tw 開放資料 API：R 範例，使用 httr2 套件。
#
# 用法：
#   install.packages("httr2")
#   Sys.setenv(DASHBOARD_TW_CLIENT_ID = "1a2b3c4d-xxxx-xxxx-xxxx")
#   Sys.setenv(DASHBOARD_TW_CLIENT_SECRET = "...")
#   source("example.R")
#
# httr2 內建 OAuth 2.0 Client Credentials：會自動取得 token、快取，過期時重新取得。

library(httr2)

base_url <- sub("/$", "", Sys.getenv("DASHBOARD_TW_BASE_URL", "https://dashboard.tw"))

client <- oauth_client(
  id = Sys.getenv("DASHBOARD_TW_CLIENT_ID"),
  secret = Sys.getenv("DASHBOARD_TW_CLIENT_SECRET"),
  token_url = paste0(base_url, "/api/v1/auth/token"),
  auth = "body"
)

latest <- function(etag = NULL) {
  req <- request(base_url) |>
    req_url_path_append("api", "v1", "datasets", "aqi_obs", "latest") |>
    req_url_query(county = "63000") |>
    req_oauth_client_credentials(client)
  if (!is.null(etag)) req <- req_headers(req, `If-None-Match` = etag)
  req_perform(req)
}

resp <- latest()
body <- resp_body_json(resp)
cat("資料來源：", body$meta$source$agency, "\n")
for (row in body$data) {
  cat("測站", row$site_id, "\tAQI", row$aqi, "\t", row$status, "\n")
}

# 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
again <- latest(resp_header(resp, "ETag"))
cat(if (resp_status(again) == 304) "資料沒有變動（304）" else "資料已更新", "\n")
