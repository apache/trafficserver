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
	"testing"

	"github.com/apache/trafficserver/tools/voluspa"
)

func TestEscalateAdapter(t *testing.T) {
	var plugin EscalateAdapter
	ro := &voluspa.RemapOptions{}

	subOptions := make(map[interface{}]interface{})
	(*ro)[EscalateAdapterFailoverParameter] = subOptions

	env := &voluspa.ConfigEnvironment{RemapOptions: *ro}

	_, err := plugin.PParams(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration.")
	}

	subOptions["domain"] = ""
	_, err = plugin.PParams(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration.")
	}

	subOptions["domain"] = "domain.example.com"
	_, err = plugin.PParams(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration.")
	}

	subOptions["status_codes"] = []string{}
	_, err = plugin.PParams(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration.")
	}

	subOptions["status_codes"] = []string{"401", "403"}
	out, err := plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	expected := "401,403:domain.example.com"
	if out[0] != expected {
		t.Fatalf("Error: got %q; expected %q", out[0], expected)
	}

	subOptions["host_header"] = "origin"
	out, err = plugin.PParams(env)
	if err != nil {
		t.Fatalf("Unexpected error: %s", err)
	}

	expected = "--pristine"
	if out[0] != expected {
		t.Fatalf("Error: got %q; expected %q", out[0], expected)
	}

	expected = "401,403:domain.example.com"
	if out[1] != expected {
		t.Fatalf("Error: got %q; expected %q", out[1], expected)
	}
}
