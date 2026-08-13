//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
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
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"time"

	"github.com/okdaichi/qmux-go/qmux"
	"github.com/quic-go/qpack"
	"github.com/quic-go/quic-go/quicvarint"
)

const (
	qmuxALPN      = "h3qx-01"
	bodyChunkSize = 8 * 1024
	largeBodySize = 300000

	h3FrameData     = 0x00
	h3FrameHeaders  = 0x01
	h3FrameSettings = 0x04

	h3ControlStream      = 0x00
	h3QPACKEncoderStream = 0x02
	h3QPACKDecoderStream = 0x03
)

type requestCase struct {
	name         string
	method       string
	path         string
	requestSize  int
	responseSize int
}

func generatedBody(size int) []byte {
	var body bytes.Buffer
	for i := 0; body.Len() < size; i++ {
		fmt.Fprintf(&body, "%07x ", i)
	}
	return body.Bytes()[:size]
}

func writeVarInt(w io.Writer, value uint64) error {
	encoded := quicvarint.Append(nil, value)
	_, err := w.Write(encoded)
	return err
}

func writeFrame(w io.Writer, frameType uint64, payload []byte) error {
	header := quicvarint.Append(nil, frameType)
	header = quicvarint.Append(header, uint64(len(payload)))
	if _, err := w.Write(header); err != nil {
		return err
	}
	_, err := w.Write(payload)
	return err
}

func writeRequestBody(w io.Writer, body []byte) error {
	for len(body) > 0 {
		chunkSize := min(len(body), bodyChunkSize)
		if err := writeFrame(w, h3FrameData, body[:chunkSize]); err != nil {
			return err
		}
		body = body[chunkSize:]
	}
	return nil
}

func openUniStream(ctx context.Context, conn *qmux.Conn, streamType uint64) error {
	stream, err := conn.OpenUniStreamSync(ctx)
	if err != nil {
		return err
	}
	return writeVarInt(stream, streamType)
}

func initializeHTTP3(ctx context.Context, conn *qmux.Conn) error {
	control, err := conn.OpenUniStreamSync(ctx)
	if err != nil {
		return fmt.Errorf("open control stream: %w", err)
	}
	if err := writeVarInt(control, h3ControlStream); err != nil {
		return fmt.Errorf("write control stream type: %w", err)
	}
	if err := writeFrame(control, h3FrameSettings, nil); err != nil {
		return fmt.Errorf("write SETTINGS frame: %w", err)
	}

	if err := openUniStream(ctx, conn, h3QPACKEncoderStream); err != nil {
		return fmt.Errorf("open QPACK encoder stream: %w", err)
	}
	if err := openUniStream(ctx, conn, h3QPACKDecoderStream); err != nil {
		return fmt.Errorf("open QPACK decoder stream: %w", err)
	}
	return nil
}

func encodeRequestHeaders(authority string, tc requestCase) ([]byte, error) {
	var block bytes.Buffer

	encoder := qpack.NewEncoder(&block)
	fields := []qpack.HeaderField{
		{Name: ":method", Value: tc.method},
		{Name: ":scheme", Value: "https"},
		{Name: ":authority", Value: authority},
		{Name: ":path", Value: tc.path},
		{Name: "user-agent", Value: "ats-qmux-go-autest"},
		{Name: "x-qmux-client", Value: "qmux-go"},
		{Name: "x-qmux-test-case", Value: tc.name},
		{Name: "uuid", Value: tc.name},
	}
	if tc.requestSize > 0 {
		fields = append(
			fields,
			qpack.HeaderField{Name: "content-type", Value: "application/octet-stream"},
			qpack.HeaderField{Name: "content-length", Value: strconv.Itoa(tc.requestSize)},
		)
	}
	for _, field := range fields {
		if err := encoder.WriteField(field); err != nil {
			return nil, err
		}
	}
	return block.Bytes(), nil
}

func decodeResponseHeaders(block []byte) (string, string, string, error) {
	var status string
	var marker string
	var contentLength string

	decode := qpack.NewDecoder().Decode(block)
	for {
		field, err := decode()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			return "", "", "", err
		}
		switch field.Name {
		case ":status":
			status = field.Value
		case "x-qmux-response":
			marker = field.Value
		case "content-length":
			contentLength = field.Value
		}
	}
	return status, marker, contentLength, nil
}

func readResponse(stream *qmux.Stream) (string, string, string, []byte, error) {
	reader := quicvarint.NewReader(stream)
	var status string
	var marker string
	var contentLength string
	var body bytes.Buffer

	for {
		frameType, err := quicvarint.Read(reader)
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			return "", "", "", nil, err
		}
		length, err := quicvarint.Read(reader)
		if err != nil {
			return "", "", "", nil, err
		}
		payload := make([]byte, length)
		if _, err := io.ReadFull(reader, payload); err != nil {
			return "", "", "", nil, err
		}

		switch frameType {
		case h3FrameHeaders:
			decodedStatus, decodedMarker, decodedContentLength, err := decodeResponseHeaders(payload)
			if err != nil {
				return "", "", "", nil, fmt.Errorf("decode response headers: %w", err)
			}
			if decodedStatus != "" {
				status = decodedStatus
			}
			if decodedMarker != "" {
				marker = decodedMarker
			}
			if decodedContentLength != "" {
				contentLength = decodedContentLength
			}
		case h3FrameData:
			body.Write(payload)
		}
	}
	return status, marker, contentLength, body.Bytes(), nil
}

func request(ctx context.Context, conn *qmux.Conn, authority string, tc requestCase) error {
	stream, err := conn.OpenStreamSync(ctx)
	if err != nil {
		return fmt.Errorf("%s: open request stream: %w", tc.name, err)
	}
	stream.SetDeadline(time.Now().Add(20 * time.Second))

	headerBlock, err := encodeRequestHeaders(authority, tc)
	if err != nil {
		return fmt.Errorf("%s: encode request headers: %w", tc.name, err)
	}
	if err := writeFrame(stream, h3FrameHeaders, headerBlock); err != nil {
		return fmt.Errorf("%s: write request headers: %w", tc.name, err)
	}
	if tc.requestSize > 0 {
		if err := writeRequestBody(stream, generatedBody(tc.requestSize)); err != nil {
			return fmt.Errorf("%s: write request body: %w", tc.name, err)
		}
	}
	if err := stream.Close(); err != nil {
		return fmt.Errorf("%s: finish request stream: %w", tc.name, err)
	}

	status, marker, contentLength, body, err := readResponse(stream)
	if err != nil {
		return fmt.Errorf("%s: read response: %w", tc.name, err)
	}
	if status != "200" {
		return fmt.Errorf("%s: expected status 200, got %q", tc.name, status)
	}
	if marker != "success" {
		return fmt.Errorf("%s: expected X-QMux-Response success, got %q", tc.name, marker)
	}
	if contentLength != strconv.Itoa(tc.responseSize) {
		return fmt.Errorf("%s: expected Content-Length %d, got %q", tc.name, tc.responseSize, contentLength)
	}
	expectedBody := generatedBody(tc.responseSize)
	if !bytes.Equal(body, expectedBody) {
		return fmt.Errorf("%s: response body mismatch: got %d bytes, expected %d", tc.name, len(body), len(expectedBody))
	}

	fmt.Printf("ok %s request=%d response=%d\n", tc.name, tc.requestSize, tc.responseSize)
	return nil
}

func run(addr string, authority string, serverName string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	tcpConn, err := (&net.Dialer{}).DialContext(ctx, "tcp", addr)
	if err != nil {
		return fmt.Errorf("dial TCP: %w", err)
	}
	tlsConn := tls.Client(tcpConn, &tls.Config{
		InsecureSkipVerify: true,
		MinVersion:         tls.VersionTLS13,
		NextProtos:         []string{qmuxALPN},
		ServerName:         serverName,
	})
	if err := tlsConn.HandshakeContext(ctx); err != nil {
		return fmt.Errorf("TLS handshake: %w", err)
	}
	if negotiated := tlsConn.ConnectionState().NegotiatedProtocol; negotiated != qmuxALPN {
		return fmt.Errorf("expected ALPN %q, got %q", qmuxALPN, negotiated)
	}

	config := qmux.DefaultConfig()
	// qmux-go v0.2.0 uses a nonstandard code point for this optional parameter.
	// Omitting it selects the interoperable protocol default of 16,382 bytes.
	config.MaxRecordSize = 0
	config.InitialConnectionReceiveWindow = 10000000
	config.InitialStreamReceiveWindow = 1000000
	conn, err := qmux.Dial(newQMuxCompatConn(tlsConn), config)
	if err != nil {
		return fmt.Errorf("start QMux: %w", err)
	}
	defer conn.Close()

	if err := initializeHTTP3(ctx, conn); err != nil {
		return err
	}
	cases := []requestCase{
		{name: "qmux-get-empty", method: "GET", path: "/qmux-get-empty"},
		{name: "qmux-post-small", method: "POST", path: "/qmux-post-small", requestSize: 100, responseSize: 100},
		{
			name:         "qmux-post-large",
			method:       "POST",
			path:         "/qmux-post-large",
			requestSize:  largeBodySize,
			responseSize: largeBodySize,
		},
	}
	for _, tc := range cases {
		if err := request(ctx, conn, authority, tc); err != nil {
			return err
		}
	}

	fmt.Printf("completed %d QMux HTTP/3 requests: alpn=%s\n", len(cases), qmuxALPN)
	return nil
}

func main() {
	addr := flag.String("addr", "", "ATS QMux address in host:port form")
	authority := flag.String("authority", "", "HTTP/3 request authority")
	serverName := flag.String("server-name", "", "TLS SNI server name")
	flag.Parse()

	if *addr == "" || *authority == "" || *serverName == "" {
		flag.Usage()
		os.Exit(2)
	}
	if err := run(*addr, *authority, *serverName); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
