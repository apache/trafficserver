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

package voluspa

import "testing"

func TestRemapOptions_ValueByNameAsBool(t *testing.T) {
	ro := &RemapOptions{}
	for _, v := range []bool{true, false} {
		(*ro)["key"] = v

		out, err := ro.ValueByNameAsBool("key")
		if err != nil || out != v {
			t.Fatalf("Expected value '%t', got '%t'", v, out)
		}
	}

	_, err := ro.ValueByNameAsBool("non-existent-key")
	if err == nil {
		t.Fatalf("Expected err getting value by non-existent key")
	}

	(*ro)["key2"] = "stringval"
	_, err = ro.ValueByNameAsBool("key2")
	if err == nil {
		t.Fatalf("Expected err getting boolean value for key")
	}
}

func TestRemapOptions_ValueByNameAsString(t *testing.T) {
	ro := &RemapOptions{}
	(*ro)["key"] = "value"

	out, err := ro.ValueByNameAsString("key")
	if err != nil || out != "value" {
		t.Fatalf("Expected value '%s', got '%s'", (*ro)["key"], out)
	}

	_, err = ro.ValueByNameAsString("non-existent-key")
	if err == nil {
		t.Fatalf("Expected err getting value by non-existent key")
	}

	(*ro)["key2"] = false
	_, err = ro.ValueByNameAsString("key2")
	if err == nil {
		t.Fatalf("Expected err getting string value for key")
	}
}
