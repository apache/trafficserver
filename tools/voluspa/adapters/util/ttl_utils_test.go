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

package util

import "testing"

func TestDurationToSeconds(t *testing.T) {
	in := "30d"
	expected := "2592000"
	val, err := DurationToSeconds(in)

	if err != nil || val != expected {
		t.Fatalf("Got '%s' expected '%s' err=%s", val, expected, err)
	}

	in = "0"
	expected = "0"
	val, err = DurationToSeconds(in)

	if err != nil || val != expected {
		t.Fatalf("Got '%s' expected '%s' err=%s", val, expected, err)
	}

	in = "7w"
	expected = "4233600"
	val, err = DurationToSeconds(in)

	if err != nil || val != expected {
		t.Fatalf("Got '%s' expected '%s' err=%s", val, expected, err)
	}

	in = "1h"
	expected = "3600"
	val, err = DurationToSeconds(in)

	if err != nil || val != expected {
		t.Fatalf("Got '%s' expected '%s' err=%s", val, expected, err)
	}

	in = "2h3s"
	expected = "7203"
	val, err = DurationToSeconds(in)

	if err == nil || val == expected {
		t.Fatalf("Got '%s' expected '%s' err=%s", val, expected, err)
	}

	in = "7r"
	val, err = DurationToSeconds(in)

	if err == nil {
		t.Fatalf("Expected err converting '%s' val=%s", in, val)
	}

	in = "5"
	val, err = DurationToSeconds(in)

	if err == nil {
		t.Fatalf("Expected err converting '%s' val=%s", in, val)
	}

	in = "7w2d3h"
	val, err = DurationToSeconds(in)

	if err == nil {
		t.Fatalf("Expected err converting '%s' val=%s", in, val)
	}
}
