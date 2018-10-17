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

var (
	childIPRange  = []string{"17.0.0.0-17.255.255.255", "10.0.0.0-10.255.255.255"}
	parentIPRange = []string{"17.253.0.0-17.255.255.255", "10.253.0.0-10.255.255.255"}
)

func TestIPAllowAdapter(t *testing.T) {
	var plugin AllowIPAdapter
	ro := &voluspa.RemapOptions{}
	env := &voluspa.ConfigEnvironment{RemapOptions: *ro}

	(*ro)[allowIPAdapterParameter] = ""
	out, err := plugin.Content(env)
	if err == nil && out != nil {
		t.Fatalf("Expected error for invalid configuration.")
	}

	(*ro)[allowIPAdapterParameter] = []string{}
	_, err = plugin.Content(env)
	if err == nil {
		t.Fatalf("Expected error for invalid configuration.")
	}

	(*ro)[allowIPAdapterParameter] = childIPRange
	out, err = plugin.Content(env)
	if err != nil {
		t.Fatalf("Unexpected error. err=%s", err)
	}

	expected := "@src_ip=17.0.0.0-17.255.255.255 @src_ip=10.0.0.0-10.255.255.255 @action=allow"
	if out.String() != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out, expected)
	}

	// "child" config. should still return child version
	(*ro)[allowIPAdapterParameterParent] = parentIPRange
	out, err = plugin.Content(env)
	if err != nil {
		t.Fatalf("Unexpected error. err=%s", err)
	}

	expected = "@src_ip=17.0.0.0-17.255.255.255 @src_ip=10.0.0.0-10.255.255.255 @action=allow"
	if out.String() != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out, expected)
	}

	ro = &voluspa.RemapOptions{}
	env = &voluspa.ConfigEnvironment{RemapOptions: *ro, ConfigLocation: voluspa.ParentConfig}
	(*ro)[allowIPAdapterParameterParent] = []string{"17.0.0.0-17.255.255.255", "10.0.0.0-10.255.255.255"}
	out, err = plugin.Content(env)
	if err != nil {
		t.Fatalf("Unexpected error. err=%s", err)
	}

	expected = "@src_ip=17.0.0.0-17.255.255.255 @src_ip=10.0.0.0-10.255.255.255 @action=allow"
	if out.String() != expected {
		t.Fatalf("Error: got '%s'; expected '%s'", out, expected)
	}
}
