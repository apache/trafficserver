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

import (
	"fmt"
	"regexp"
	"strconv"
	"time"
)

var durationRE = regexp.MustCompile(`^(\d+)([a-z])$`)

// DurationToSeconds takes a duration string and returns number of seconds
//  required to be a single time unit (eg 7d or 24h or 3s)
//	complex duration not supported(yet) (eg 7d24h3s)
func DurationToSeconds(value string) (string, error) {
	if value == "0" {
		return value, nil
	}

	if !durationRE.MatchString(value) {
		return "", fmt.Errorf("Bad duration \"%s\"", value)
	}

	matches := durationRE.FindStringSubmatch(value)
	count, err := strconv.ParseUint(matches[1], 10, 64)
	if err != nil {
		return "", fmt.Errorf("Bad duration \"%s\"", value)
	}

	switch matches[2] {
	case "d":
		return strconv.FormatUint(count*60*60*24, 10), nil
	case "w":
		return strconv.FormatUint(count*60*60*24*7, 10), nil

	default:
		duration, err := time.ParseDuration(value)
		if err == nil {
			return fmt.Sprintf("%d", int(duration.Seconds())), nil
		}
		return "", fmt.Errorf("Unhandled spec '%s' for '%s'. err=%s", matches[2], value, err)
	}
}
