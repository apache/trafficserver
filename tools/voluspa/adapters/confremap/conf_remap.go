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

package confremap

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&FreeformConfRemapAdapter{})
}

type FreeformConfRemapAdapter struct {
}

func (*FreeformConfRemapAdapter) Weight() int {
	return 17
}

func (p *FreeformConfRemapAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *FreeformConfRemapAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("raw_conf_remap")
}

func (p *FreeformConfRemapAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *FreeformConfRemapAdapter) ConfigParameters() []string {
	return []string{"conf_remap"}
}

// CONFIG proxy.config.http.connect_attempts_timeout INT 600
var confRegexp = regexp.MustCompile(`CONFIG (.*) ([A-Z]+) (.*)`)

func (p *FreeformConfRemapAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	val, err := env.RemapOptions.ValueByNameAsString("conf_remap")
	if err != nil {
		return nil, err
	}

	seen := make(map[string]bool)

	var params []string

	lines := strings.Split(val, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		// eg CONFIG proxy.config.http.connect_attempts_timeout INT 600
		vals := confRegexp.FindStringSubmatch(line)
		if vals == nil {
			return nil, fmt.Errorf("%s is not properly formed", line)
		}

		if _, ok := seen[line]; ok {
			return nil, fmt.Errorf("Already seen key '%s'", vals[1])
		}
		seen[line] = true

		switch vals[2] {
		case "INT":
			_, err := strconv.Atoi(vals[3])
			if err != nil {
				return nil, fmt.Errorf("Value is not of appropriate type; in=%s type=%s line='%s'", vals[3], vals[2], line)
			}
		case "FLOAT":
			_, err := strconv.ParseFloat(vals[3], 64)
			if err != nil {
				return nil, fmt.Errorf("Value is not of appropriate type; in=%s type=%s line='%s'", vals[3], vals[2], line)
			}
		case "STRING":
		default:
			return nil, fmt.Errorf("Type is unknown/unhandled; in=%s type=%s line='%s'", vals[3], vals[2], line)
		}
		params = append(params, fmt.Sprintf("%s=%s", vals[1], vals[3]))
	}
	return params, nil
}
