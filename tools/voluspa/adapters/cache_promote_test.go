/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package adapters

import (
	"strings"
	"testing"

	"github.com/apache/trafficserver/tools/voluspa"
)

func TestCachePromoteAdapter(t *testing.T) {
	var plugin CachePromoteAdapter
	ro := &voluspa.RemapOptions{}
	subOptions := make(map[interface{}]interface{})
	(*ro)["cache_promote"] = subOptions

	env := &voluspa.ConfigEnvironment{RemapOptions: *ro}

	_, err := plugin.PParams(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration")
	}

	subOptions["policy"] = "notapolicy"
	_, err = plugin.PParams(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration")
	}

	subOptions["policy"] = "lru"
	out, err := plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	expected := "--policy=lru"
	if out[0] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[0], expected)
	}

	subOptions["sample"] = 66
	out, err = plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	joined := strings.Join(out, " ")
	expected = "--policy=lru --sample=66%"
	if joined != expected {
		t.Fatalf("Error: got %q; expected %q", joined, expected)
	}

	subOptions["lru_hits"] = 1000
	out, err = plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	joined = strings.Join(out, " ")
	expected = "--policy=lru --sample=66% --hits=1000"
	if joined != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", joined, expected)
	}

	subOptions["lru_buckets"] = 10000
	out, err = plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	joined = strings.Join(out, " ")
	expected = "--policy=lru --sample=66% --hits=1000 --buckets=10000"
	if joined != expected {
		t.Fatalf("Error: got %q; expected %q", joined, expected)
	}

	// Non-LRU policy's should drop hits and buckets from command-line
	subOptions["policy"] = "chance"
	out, err = plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	joined = strings.Join(out, " ")
	expected = "--policy=chance --sample=66%"
	if joined != expected {
		t.Fatalf("Error: got %q; expected %q", joined, expected)
	}
}
