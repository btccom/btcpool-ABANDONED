package util

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/http/httputil"
	"github.com/golang/glog"
)

type Client struct {
	client http.Client
}

func NewHttpClient() *Client {
	client := http.Client{}
	return &Client{client: client}
}


func (c *Client) GetJson(endpoint string, headers map[string]string) ([]byte, error) {
	glog.Info("httpclient get start " + endpoint)
	req, err := http.NewRequest("GET", endpoint, nil)
	if err != nil {
		return nil, fmt.Errorf("httpclient get err:%w", err)
	}

	for key, value := range headers {
		req.Header[key] = []string{value}
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")

	byts, _ := httputil.DumpRequest(req, true)
	glog.Info("httpclient get : " + string(byts))

	var res *http.Response
	if res, err = c.client.Do(req); err != nil {
		return nil, fmt.Errorf("httpclient get err:%w", err)
	}
	var body []byte

	body, err = ioutil.ReadAll(res.Body)
	defer func() {
		_ = res.Body.Close()
	}()
	glog.Info("httpclient get res: " + string(body))
	if err != nil {
		return nil, fmt.Errorf("httpclient get err:%w", err)
	}
	return body, nil
}

func (c *Client) Post(endpoint string, user string, pass string, data *bytes.Buffer, headers map[string]string) ([]byte, error) {
	req, err := http.NewRequest("POST", endpoint, data)
	if err != nil {
		return nil, err
	}
	for key, value := range headers {
		req.Header[key] = []string{value}
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	//req.Header.Set("Content-Length", strconv.Itoa(data.Len()))
	// 输出到 log
	byts, _ := httputil.DumpRequest(req, true)
	fmt.Println(string(byts))

    if len(user) > 0 && len(pass) > 0 {
		req.SetBasicAuth(user, pass)
	}

	res, err := c.client.Do(req)
	if err != nil {
		return nil, err
	}
	fmt.Println(res.Status)
	body, err := ioutil.ReadAll(res.Body)
	fmt.Println(string(body))
	defer res.Body.Close()
	if err != nil {
		return nil, err
	}
	return body, err
}

