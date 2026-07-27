/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

//go:build ignore

package main

import (
  "crypto/hmac"
  "crypto/md5"
  "crypto/sha1"
  "encoding/base64"
  "encoding/hex"
  "flag"
  "fmt"
  "hash"
  "os"
  "strings"
  "time"
)

func main() {
  var (
    url        string
    useparts   string
    duration   int
    key        string
    client     string
    algorithm  int
    keyindex   int
    verbose    bool
    pathparams bool
    proxy      string
    siganchor  string
  )

  flag.StringVar(&url, "url", "", "URL to sign (required)")
  flag.StringVar(&useparts, "useparts", "", "parts mask (required)")
  flag.IntVar(&duration, "duration", 0, "duration in seconds (required)")
  flag.StringVar(&key, "key", "", "signing key (required)")
  flag.StringVar(&client, "client", "", "client IP (optional)")
  flag.IntVar(&algorithm, "algorithm", 1, "1=HMAC-SHA1, 2=HMAC-MD5")
  flag.IntVar(&keyindex, "keyindex", 0, "key index (required)")
  flag.BoolVar(&verbose, "verbose", false, "verbose output")
  flag.BoolVar(&pathparams, "pathparams", false, "use path parameters instead of query string")
  flag.StringVar(&proxy, "proxy", "", "proxy URL:port (optional)")
  flag.StringVar(&siganchor, "siganchor", "", "signature anchor string for path params (optional)")
  flag.Parse()

  if url == "" || useparts == "" || 0 == duration || key == "" {
    flag.Usage()
    os.Exit(1)
  }

  if proxy != "" && !strings.Contains(proxy, ":") {
    fmt.Fprintf(os.Stderr, "proxy must be in format http://host:port\n")
    os.Exit(1)
  }

  // Strip and remember scheme.
  scheme := "http://"
  if strings.HasPrefix(url, "https://") {
    scheme = "https://"
    url = strings.TrimPrefix(url, "https://")
  } else {
    url = strings.TrimPrefix(url, "http://")
  }

  // Split off query params if present.
  var queryParams string
  if idx := strings.Index(url, "?"); 0 <= idx {
    queryParams = url[idx+1:]
    url = url[:idx]
  }

  // Split path into parts.
  parts := strings.Split(url, "/")

  // In pathparams mode, pop file segment.
  var file string
  if pathparams {
    if len(parts) < 2 {
      fmt.Fprintf(os.Stderr, "ERROR: No file segment in path when using --pathparams.\n")
      os.Exit(1)
    }
    file = parts[len(parts)-1]
    parts = parts[:len(parts)-1]
  }

  // Build signed string from parts mask.
  var signed strings.Builder
  j := 0
  for i, part := range parts {
    active := byte('0')
    if j < len(useparts) {
      active = useparts[j]
    } else if 0 < len(useparts) {
      active = useparts[len(useparts)-1]
    }
    if active == '1' {
      signed.WriteString(part)
      if i < len(parts)-1 || !pathparams {
        signed.WriteByte('/')
      }
    }
    if j+1 < len(useparts) {
      j++
    }
  }

  // Remove trailing '/'.
  signedStr := signed.String()
  if strings.HasSuffix(signedStr, "/") {
    signedStr = signedStr[:len(signedStr)-1]
  }

  // Build signing signature (query or path params).
  expiry := time.Now().Unix() + int64(duration)
  expiryStr := fmt.Sprintf("%d", expiry)

  var sigParams string
  if pathparams {
    if client != "" {
      sigParams = fmt.Sprintf(";C=%s;E=%s;A=%d;K=%d;P=%s;S=", client, expiryStr, algorithm, keyindex, useparts)
    } else {
      sigParams = fmt.Sprintf(";E=%s;A=%d;K=%d;P=%s;S=", expiryStr, algorithm, keyindex, useparts)
    }
    signedStr += sigParams
  } else {
    params := ""
    if queryParams != "" {
      params = queryParams + "&"
    }
    if client != "" {
      sigParams = fmt.Sprintf("?%sC=%s&E=%s&A=%d&K=%d&P=%s&S=", params, client, expiryStr, algorithm, keyindex, useparts)
    } else {
      sigParams = fmt.Sprintf("?%sE=%s&A=%d&K=%d&P=%s&S=", params, expiryStr, algorithm, keyindex, useparts)
    }
    signedStr += sigParams
  }

  // Compute HMAC.
  var h func() hash.Hash
  switch algorithm {
  case 1:
    h = sha1.New
  case 2:
    h = md5.New
  default:
    fmt.Fprintf(os.Stderr, "unsupported algorithm: %d\n", algorithm)
    os.Exit(1)
  }

  mac := hmac.New(h, []byte(key))
  mac.Write([]byte(signedStr))
  digest := hex.EncodeToString(mac.Sum(nil))

  if verbose {
    fmt.Fprintf(os.Stderr, "\nSigned String: %s\n", signedStr)
    fmt.Fprintf(os.Stderr, "URL: %s\n", url)
    fmt.Fprintf(os.Stderr, "signing_signature: %s\n", sigParams)
    fmt.Fprintf(os.Stderr, "digest: %s\n\n", digest)
  }

  // Build output curl command.
  var curlURL string
  if pathparams {
    lastSlash := strings.LastIndex(url, "/")
    urlBase := url
    if 0 <= lastSlash {
      urlBase = url[:lastSlash]
    }
    encoded := base64.URLEncoding.EncodeToString([]byte(sigParams + digest))
    if siganchor != "" {
      curlURL = fmt.Sprintf("%s%s;%s=%s/%s", scheme, urlBase, siganchor, encoded, file)
    } else {
      curlURL = fmt.Sprintf("%s%s/%s/%s", scheme, urlBase, encoded, file)
    }
    if queryParams != "" {
      curlURL += "?" + queryParams
    }
  } else {
    curlURL = fmt.Sprintf("%s%s%s%s", scheme, url, sigParams, digest)
  }

  if proxy != "" {
    fmt.Printf("curl -s -o /dev/null -v --max-redirs 0 --proxy %s '%s'\n\n", proxy, curlURL)
  } else {
    fmt.Printf("curl -s -o /dev/null -v --max-redirs 0 '%s'\n\n", curlURL)
  }
}
