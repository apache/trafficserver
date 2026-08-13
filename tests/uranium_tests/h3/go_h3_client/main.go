//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information regarding
//  copyright ownership.  The ASF licenses this file to you under
//  the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may
//  obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
)

const (
	largeBodySize     = 300000
	largeBodySuffix   = "000927b "
	reusedHeaderValue = "stable-qpack-value"
)

type requestCase struct {
	name         string
	method       string
	path         string
	requestSize  int
	responseSize int
	status       int
}

func generatedBody(size int) []byte {
	var body bytes.Buffer
	for i := 0; body.Len() < size; i++ {
		fmt.Fprintf(&body, "%07x ", i)
	}
	return body.Bytes()[:size]
}

func newTLSConfig(serverName string) *tls.Config {
	return &tls.Config{
		InsecureSkipVerify: true,
		NextProtos:         []string{http3.NextProtoH3},
		ServerName:         serverName,
	}
}

func newQUICConfig() *quic.Config {
	return &quic.Config{
		MaxIdleTimeout: 10 * time.Second,
	}
}

func newClient(serverName string) (*http.Client, *http3.Transport) {
	transport := &http3.Transport{
		TLSClientConfig:    newTLSConfig(serverName),
		QUICConfig:         newQUICConfig(),
		DisableCompression: true,
	}
	client := &http.Client{
		Transport: transport,
		Timeout:   30 * time.Second,
	}
	return client, transport
}

func newRequest(ctx context.Context, baseURL string, authority string, tc requestCase) (*http.Request, error) {
	var body io.Reader
	if tc.requestSize > 0 {
		body = bytes.NewReader(generatedBody(tc.requestSize))
	}

	req, err := http.NewRequestWithContext(ctx, tc.method, baseURL+tc.path, body)
	if err != nil {
		return nil, err
	}

	req.Host = authority
	req.Header.Set("User-Agent", "ats-h3-quic-go-autest")
	req.Header.Set("X-H3-Go-Client", "quic-go")
	req.Header.Set("X-H3-Reused-Header", reusedHeaderValue)
	req.Header.Set("X-H3-Test-Case", tc.name)
	req.Header.Set("uuid", tc.name)
	if tc.requestSize > 0 {
		req.Header.Set("Content-Type", "application/octet-stream")
	}

	return req, nil
}

func verifyResponse(tc requestCase, resp *http.Response) error {
	defer resp.Body.Close()

	if resp.ProtoMajor != 3 {
		return fmt.Errorf("%s: expected HTTP/3, got %s", tc.name, resp.Proto)
	}
	if resp.StatusCode != tc.status {
		return fmt.Errorf("%s: expected status %d, got %d", tc.name, tc.status, resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("%s: read response body: %w", tc.name, err)
	}

	if tc.method == http.MethodHead || tc.status == http.StatusNoContent {
		if len(body) != 0 {
			return fmt.Errorf("%s: expected no response body, got %d bytes", tc.name, len(body))
		}
		return nil
	}

	expected := generatedBody(tc.responseSize)
	if !bytes.Equal(body, expected) {
		return fmt.Errorf("%s: response body mismatch: got %d bytes, expected %d", tc.name, len(body), len(expected))
	}
	if tc.responseSize == largeBodySize && !bytes.HasSuffix(body, []byte(largeBodySuffix)) {
		return fmt.Errorf("%s: large response body does not end with %q", tc.name, largeBodySuffix)
	}

	return nil
}

func doRequest(
	ctx context.Context,
	roundTrip func(*http.Request) (*http.Response, error),
	baseURL string,
	authority string,
	tc requestCase,
) error {
	req, err := newRequest(ctx, baseURL, authority, tc)
	if err != nil {
		return err
	}

	resp, err := roundTrip(req)
	if err != nil {
		return fmt.Errorf("%s: request failed: %w", tc.name, err)
	}

	if err := verifyResponse(tc, resp); err != nil {
		return err
	}

	fmt.Printf("ok %s\n", tc.name)
	return nil
}

func runSequential(ctx context.Context, baseURL string, authority string, serverName string, cases []requestCase) error {
	client, transport := newClient(serverName)
	defer transport.Close()

	for _, tc := range cases {
		if err := doRequest(ctx, client.Do, baseURL, authority, tc); err != nil {
			return err
		}
	}

	return nil
}

func runConcurrent(ctx context.Context, addr string, baseURL string, authority string, serverName string, cases []requestCase) error {
	transport := &http3.Transport{}
	defer transport.Close()

	conn, err := quic.DialAddr(ctx, addr, newTLSConfig(serverName), newQUICConfig())
	if err != nil {
		return fmt.Errorf("dial concurrent HTTP/3 connection: %w", err)
	}
	clientConn := transport.NewClientConn(conn)
	defer clientConn.CloseWithError(0, "done")

	var wg sync.WaitGroup
	errs := make(chan error, len(cases))
	for _, tc := range cases {
		tc := tc
		wg.Add(1)
		go func() {
			defer wg.Done()
			errs <- doRequest(ctx, clientConn.RoundTrip, baseURL, authority, tc)
		}()
	}

	wg.Wait()
	close(errs)
	for err := range errs {
		if err != nil {
			return err
		}
	}

	return nil
}

func main() {
	addr := flag.String("addr", "", "ATS HTTP/3 address in host:port form")
	authority := flag.String("authority", "", "HTTP/3 request authority")
	serverName := flag.String("server-name", "", "TLS SNI server name")
	flag.Parse()

	if *addr == "" || *authority == "" || *serverName == "" {
		flag.Usage()
		os.Exit(2)
	}

	baseURL := "https://" + *addr
	ctx := context.Background()
	sequentialCases := []requestCase{
		{name: "go-get-empty", method: http.MethodGet, path: "/go-get-empty", status: http.StatusOK},
		{name: "go-get-small", method: http.MethodGet, path: "/go-get-small", responseSize: 100, status: http.StatusOK},
		{name: "go-head-no-body", method: http.MethodHead, path: "/go-head-no-body", responseSize: 100, status: http.StatusOK},
		{name: "go-204-no-body", method: http.MethodGet, path: "/go-204-no-body", status: http.StatusNoContent},
		{
			name:         "go-post-small",
			method:       http.MethodPost,
			path:         "/go-post-small",
			requestSize:  100,
			responseSize: 100,
			status:       http.StatusOK,
		},
		{
			name:         "go-put-small",
			method:       http.MethodPut,
			path:         "/go-put-small",
			requestSize:  100,
			responseSize: 100,
			status:       http.StatusOK,
		},
		{name: "go-delete-empty", method: http.MethodDelete, path: "/go-delete-empty", status: http.StatusNoContent},
		{name: "go-options-small", method: http.MethodOptions, path: "/go-options-small", responseSize: 100, status: http.StatusOK},
	}
	concurrentCases := []requestCase{
		{
			name:         "go-get-concurrent-large",
			method:       http.MethodGet,
			path:         "/go-get-concurrent-large",
			responseSize: largeBodySize,
			status:       http.StatusOK,
		},
		{
			name:         "go-get-concurrent-small",
			method:       http.MethodGet,
			path:         "/go-get-concurrent-small",
			responseSize: 100,
			status:       http.StatusOK,
		},
	}
	largeCases := []requestCase{
		{name: "go-get-large", method: http.MethodGet, path: "/go-get-large", responseSize: largeBodySize, status: http.StatusOK},
		{
			name:         "go-post-large",
			method:       http.MethodPost,
			path:         "/go-post-large",
			requestSize:  largeBodySize,
			responseSize: largeBodySize,
			status:       http.StatusOK,
		},
		{
			name:         "go-put-large",
			method:       http.MethodPut,
			path:         "/go-put-large",
			requestSize:  largeBodySize,
			responseSize: largeBodySize,
			status:       http.StatusOK,
		},
	}

	if err := runSequential(ctx, baseURL, *authority, *serverName, sequentialCases); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if err := runConcurrent(ctx, *addr, baseURL, *authority, *serverName, concurrentCases); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if err := runSequential(ctx, baseURL, *authority, *serverName, largeCases); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	fmt.Println("completed 13 HTTP/3 requests")
}
