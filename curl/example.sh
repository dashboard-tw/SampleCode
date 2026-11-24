#!/usr/bin/env bash
# dashboard.tw 開放資料 API：以 curl 取得 Access Token 並呼叫 API。
# 需要 curl 與 jq。用法：
#   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
#   export DASHBOARD_TW_CLIENT_SECRET=...
#   ./example.sh
set -euo pipefail

BASE_URL="${DASHBOARD_TW_BASE_URL:-https://dashboard.tw}"
: "${DASHBOARD_TW_CLIENT_ID:?請設定 DASHBOARD_TW_CLIENT_ID}"
: "${DASHBOARD_TW_CLIENT_SECRET:?請設定 DASHBOARD_TW_CLIENT_SECRET}"

# 1. 換取 Access Token（有效 24 小時，請重複使用）
TOKEN=$(curl -sS --fail-with-body -X POST "$BASE_URL/api/v1/auth/token" \
  --data-urlencode "grant_type=client_credentials" \
  --data-urlencode "client_id=$DASHBOARD_TW_CLIENT_ID" \
  --data-urlencode "client_secret=$DASHBOARD_TW_CLIENT_SECRET" | jq -r '.access_token')

# 2. 呼叫 API：臺北市各空品測站的最新 AQI，回應標頭另存起來取 ETag
URL="$BASE_URL/api/v1/datasets/aqi_obs/latest?county=63000"
HEADERS=$(mktemp)
trap 'rm -f "$HEADERS"' EXIT
curl -sS --fail-with-body -D "$HEADERS" "$URL" -H "Authorization: Bearer $TOKEN" |
  jq -r '"資料來源：\(.meta.source.agency)", (.data[] | "測站 \(.site_id)\tAQI \(.aqi)\t\(.status)")'

# 3. 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
ETAG=$(grep -i '^etag:' "$HEADERS" | cut -d' ' -f2- | tr -d '\r')
STATUS=$(curl -sS -o /dev/null -w '%{http_code}' "$URL" \
  -H "Authorization: Bearer $TOKEN" -H "If-None-Match: $ETAG")
if [ "$STATUS" = "304" ]; then echo "資料沒有變動（304）"; else echo "資料已更新"; fi
