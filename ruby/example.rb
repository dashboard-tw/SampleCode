# frozen_string_literal: true

# dashboard.tw 開放資料 API：Ruby 範例，只用標準函式庫。
#
# 用法：
#   export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
#   export DASHBOARD_TW_CLIENT_SECRET=...
#   ruby example.rb

require 'json'
require 'net/http'
require 'uri'

# 取得並快取 Access Token，token 快到期或失效時自動重新取得。
class DashboardTW
  def initialize(client_id, client_secret, base_url = ENV.fetch('DASHBOARD_TW_BASE_URL', 'https://dashboard.tw'))
    @client_id = client_id
    @client_secret = client_secret
    @base_url = base_url.chomp('/')
    @token = nil
    @expires_at = Time.at(0)
  end

  def token
    fetch_token if @token.nil? || Time.now >= @expires_at
    @token
  end

  # 呼叫 API，回傳 [JSON 本文, ETag]。帶上 etag 且資料沒變時，本文是 nil。
  def get(path, params = {}, etag: nil)
    uri = URI("#{@base_url}/api/v1#{path}")
    uri.query = URI.encode_www_form(params) unless params.empty?
    2.times do |attempt|
      req = Net::HTTP::Get.new(uri)
      req['Authorization'] = "Bearer #{token}"
      req['If-None-Match'] = etag if etag
      res = Net::HTTP.start(uri.host, uri.port, use_ssl: uri.scheme == 'https', read_timeout: 30) { |h| h.request(req) }
      # 資料沒變：304 不帶內容，也不算流量
      return [nil, etag] if res.code == '304'

      # token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
      if res.code == '401' && attempt.zero?
        @token = nil
        next
      end
      raise "API 錯誤：#{res.code} #{res.body}" unless res.code == '200'

      return [JSON.parse(res.body), res['ETag']]
    end
    raise 'API 錯誤：token 無效'
  end

  private

  def fetch_token
    res = Net::HTTP.post_form(URI("#{@base_url}/api/v1/auth/token"),
                              'grant_type' => 'client_credentials',
                              'client_id' => @client_id,
                              'client_secret' => @client_secret)
    raise "取得 token 失敗：#{res.code} #{res.body}" unless res.code == '200'

    answer = JSON.parse(res.body)
    @token = answer['access_token']
    # 提早 60 秒更新，避免請求途中過期
    @expires_at = Time.now + answer['expires_in'] - 60
  end
end

api = DashboardTW.new(ENV.fetch('DASHBOARD_TW_CLIENT_ID'), ENV.fetch('DASHBOARD_TW_CLIENT_SECRET'))
body, etag = api.get('/datasets/aqi_obs/latest', county: '63000')
puts "資料來源：#{body.dig('meta', 'source', 'agency')}"
body['data'].each { |row| puts "測站 #{row['site_id']}\tAQI #{row['aqi']}\t#{row['status']}" }

# 定時查詢時帶上上次的 ETag，資料沒變會回應 304，不算流量
again, = api.get('/datasets/aqi_obs/latest', { county: '63000' }, etag: etag)
puts again.nil? ? '資料沒有變動（304）' : '資料已更新'
