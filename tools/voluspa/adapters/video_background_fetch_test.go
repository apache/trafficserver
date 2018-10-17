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
	"sort"
	"testing"

	"github.com/apache/trafficserver/tools/voluspa"
)

func TestVideoBackgroundFetch(t *testing.T) {
	var plugin VideoBackgroundFetchAdapter

	alt := make(map[interface{}]interface{})
	ro := voluspa.RemapOptions{}
	env := &voluspa.ConfigEnvironment{
		Property:     "testVideoBackgroundFetch",
		RemapOptions: ro,
	}

	alt[VideoBackgroundFetchAdapterFetchCount] = "100"
	alt[VideoBackgroundFetchAdapterFetchMax] = "1000"
	alt[VideoBackgroundFetchAdapterFetchPolicy] = "simple"
	alt[VideoBackgroundFetchAdapterAPIHeader] = "header1"
	alt[VideoBackgroundFetchAdapterFetchPathPattern] = []string{"(.*)"}
	alt[VideoBackgroundFetchAdapterReplaceHost] = "host1"
	alt[VideoBackgroundFetchAdapterNameSpace] = "ns1"
	alt[VideoBackgroundFetchAdapterMetricsPrefix] = "prefix1"
	alt[VideoBackgroundFetchAdapterExactMatch] = "true"
	alt[VideoBackgroundFetchAdapterLogName] = "log1"

	ro["video_background_fetch"] = alt

	pparams, err := plugin.PParams(env)
	if err != nil {
		t.Fatalf("Failed to generate output for plugin. error=%v", err)
	}

	out := pparams[voluspa.DefaultRole]
	sort.Sort(sort.StringSlice(out))

	if len(out) == 0 {
		t.Fatalf("Failed to generate output for plugin. no output.")
	}

	if expected := "--api-header=header1"; out[0] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[0], expected)
	}

	if expected := "--exact-match=true"; out[1] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[1], expected)
	}

	if expected := "--fetch-count=100"; out[2] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[2], expected)
	}

	if expected := "--fetch-max=1000"; out[3] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[3], expected)
	}

	if expected := "--fetch-path-pattern=(.*)"; out[4] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[4], expected)
	}

	if expected := "--fetch-policy=simple"; out[5] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[5], expected)
	}

	if expected := "--log-name=log1"; out[6] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[6], expected)
	}

	if expected := "--metrics-prefix=prefix1"; out[7] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[7], expected)
	}

	if expected := "--name-space=ns1"; out[8] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[8], expected)
	}

	if expected := "--replace-host=host1"; out[9] != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out[9], expected)
	}
}
