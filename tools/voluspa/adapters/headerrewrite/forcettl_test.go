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

package headerrewrite

import (
	"errors"
	"fmt"
	"strings"
	"testing"

	"github.com/apache/trafficserver/tools/voluspa"
)

func TestForceTTL(t *testing.T) {

	plugin := &ForceTTLAdapter{}

	if plugin.Type() != "force_ttl" {
		t.Errorf("Type() returned unexpected %q", plugin.Type())
	}

	if fmt.Sprintf("%v", plugin.ConfigParameters()) != "[force_ttl]" {
		t.Errorf("ConfigParameters() returned unexpected %v", plugin.ConfigParameters())
	}

	for i, tt := range []struct {
		param    interface{}
		expected string
		err      error
	}{
		{
			param: nil,
			err:   errors.New(`option "force_ttl": value for "force_ttl" is not a string: <nil>`),
		},
		{
			param: true,
			err:   errors.New(`option "force_ttl": value for "force_ttl" is not a string: true`),
		},
		{
			param: 42,
			err:   errors.New(`option "force_ttl": value for "force_ttl" is not a string: 42`),
		},
		{
			param: "5d",
			expected: `
cond %{READ_RESPONSE_HDR_HOOK} [AND]
cond %{STATUS} >199 [AND]
cond %{STATUS} <400
    set-header Cache-Control "max-age=432000, public"`,
		},
		{
			param: "no-cache, no-store, must-revalidate, stale-while-revalidate=30",
			expected: `
cond %{READ_RESPONSE_HDR_HOOK} [AND]
cond %{STATUS} >199 [AND]
cond %{STATUS} <400
    set-header Cache-Control "no-cache, no-store, must-revalidate, stale-while-revalidate=30"`,
		},
	} {

		ro := &voluspa.RemapOptions{}
		env := &voluspa.ConfigEnvironment{RemapOptions: *ro}

		t.Run(fmt.Sprintf("%d-%v", i, tt.param), func(t *testing.T) {

			(*ro)[ForceTTLAdapterParameter] = tt.param
			got, err := plugin.Content(env)
			if fmt.Sprintf("%v", err) != fmt.Sprintf("%v", tt.err) {
				t.Errorf("Content() error expected `%v` got: %v", tt.err, err)
				return
			}
			expected := strings.TrimPrefix(tt.expected, "\n")
			if err == nil && got.String() != expected {
				t.Errorf("Content() buffer expected:\n%s\ngot:\n%s", expected, got.String())
			}
		})
	}
}
