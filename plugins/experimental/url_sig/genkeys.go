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
  "crypto/rand"
  "fmt"
  "math/big"
  "os"
)

var chars = []byte("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")

func randomKey(length int) (string, error) {
  buf := make([]byte, length)
  max := big.NewInt(int64(len(chars)))
  for i := range buf {
    n, err := rand.Int(rand.Reader, max)
    if err != nil {
      return "", err
    }
    buf[i] = chars[n.Int64()]
  }
  return string(buf), nil
}

func main() {
  keyLen := 32

  for i := 0; i < 16; i++ {
    key, err := randomKey(keyLen)
    if err != nil {
      fmt.Fprintf(os.Stderr, "error generating key: %v\n", err)
      os.Exit(1)
    }
    fmt.Printf("key%d = %s\n", i, key)
  }
  fmt.Println("error_url = 403")
}
