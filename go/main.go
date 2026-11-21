// dashboard.tw 開放資料 API：Go 範例，只用標準函式庫。
//
// 用法：
//
//	export DASHBOARD_TW_CLIENT_ID=1a2b3c4d-xxxx-xxxx-xxxx
//	export DASHBOARD_TW_CLIENT_SECRET=...
//	go run .
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"
)

// Client 取得並快取 Access Token，token 快到期或失效時自動重新取得。
type Client struct {
	BaseURL      string
	ClientID     string
	ClientSecret string
	HTTP         *http.Client

	mu        sync.Mutex
	token     string
	expiresAt time.Time
}

func (c *Client) fetchToken() (string, error) {
	form := url.Values{
		"grant_type":    {"client_credentials"},
		"client_id":     {c.ClientID},
		"client_secret": {c.ClientSecret},
	}
	res, err := c.HTTP.PostForm(c.BaseURL+"/api/v1/auth/token", form)
	if err != nil {
		return "", err
	}
	defer res.Body.Close()
	var answer struct {
		AccessToken string `json:"access_token"`
		ExpiresIn   int    `json:"expires_in"`
		Error       string `json:"error"`
	}
	if err := json.NewDecoder(res.Body).Decode(&answer); err != nil {
		return "", err
	}
	if res.StatusCode != http.StatusOK {
		return "", fmt.Errorf("取得 token 失敗：%d %s", res.StatusCode, answer.Error)
	}
	c.token = answer.AccessToken
	// 提早 60 秒更新，避免請求途中過期
	c.expiresAt = time.Now().Add(time.Duration(answer.ExpiresIn-60) * time.Second)
	return c.token, nil
}

// Token 回傳有效的 Access Token。
func (c *Client) Token() (string, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.token == "" || time.Now().After(c.expiresAt) {
		return c.fetchToken()
	}
	return c.token, nil
}

// Get 呼叫 API，回傳 JSON 本文。
func (c *Client) Get(path string, params url.Values, out any) error {
	u := c.BaseURL + "/api/v1" + path
	if len(params) > 0 {
		u += "?" + params.Encode()
	}
	for attempt := 0; attempt < 2; attempt++ {
		token, err := c.Token()
		if err != nil {
			return err
		}
		req, _ := http.NewRequest(http.MethodGet, u, nil)
		req.Header.Set("Authorization", "Bearer "+token)
		res, err := c.HTTP.Do(req)
		if err != nil {
			return err
		}
		body, err := io.ReadAll(res.Body)
		res.Body.Close()
		if err != nil {
			return err
		}
		// token 失效（例如憑證重新產生 Secret）：重新取得後再試一次
		if res.StatusCode == http.StatusUnauthorized && attempt == 0 {
			c.mu.Lock()
			c.token = ""
			c.mu.Unlock()
			continue
		}
		if res.StatusCode != http.StatusOK {
			return fmt.Errorf("API 錯誤：%d %s", res.StatusCode, body)
		}
		return json.Unmarshal(body, out)
	}
	return fmt.Errorf("API 錯誤：token 無效")
}

func main() {
	base := os.Getenv("DASHBOARD_TW_BASE_URL")
	if base == "" {
		base = "https://dashboard.tw"
	}
	api := &Client{
		BaseURL:      strings.TrimRight(base, "/"),
		ClientID:     os.Getenv("DASHBOARD_TW_CLIENT_ID"),
		ClientSecret: os.Getenv("DASHBOARD_TW_CLIENT_SECRET"),
		HTTP:         &http.Client{Timeout: 30 * time.Second},
	}

	var body struct {
		Data []struct {
			SiteID int    `json:"site_id"`
			AQI    *int   `json:"aqi"`
			Status string `json:"status"`
		} `json:"data"`
		Meta struct {
			Source struct {
				Agency string `json:"agency"`
			} `json:"source"`
		} `json:"meta"`
	}
	if err := api.Get("/datasets/aqi_obs/latest", url.Values{"county": {"63000"}}, &body); err != nil {
		log.Fatal(err)
	}
	fmt.Println("資料來源：", body.Meta.Source.Agency)
	for _, row := range body.Data {
		aqi := "—"
		if row.AQI != nil {
			aqi = fmt.Sprint(*row.AQI)
		}
		fmt.Printf("測站 %d\tAQI %s\t%s\n", row.SiteID, aqi, row.Status)
	}
}
