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

package regex

import (
	"testing"
)

func TestGoLangRegex(t *testing.T) {
	testCases := []struct {
		u string
		e bool
	}{
		{u: "asdf", e: true},
		{u: `^[-_A-Za-z0-9\.]+$`, e: true},
		{u: "", e: true},
		{u: " ^/([^/]*-eu-dub-[^/]*)/(.*) https://s3-eu-west-1.amazonaws.com$0", e: true},
		{u: "[", e: false},
	}

	glr := &GoLangRegex{}
	for _, tc := range testCases {
		e, err := glr.IsValid(tc.u)
		if e != tc.e {
			t.Errorf("%q: expected %t got %t: %s", tc.u, tc.e, e, err)
		}
	}
}
