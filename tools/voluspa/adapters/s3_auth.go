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
	"strconv"

	"fmt"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
)

const (
	S3AuthAdapterParameter = "s3"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&S3AuthAdapter{})
}

type S3AuthAdapter struct {
}

func (*S3AuthAdapter) Weight() int {
	return 16
}

func (p *S3AuthAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *S3AuthAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("s3_auth")
}

func (p *S3AuthAdapter) SharedLibraryName() string {
	return "s3_auth.so"
}

func (p *S3AuthAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ParentConfig}
}

func (p *S3AuthAdapter) ConfigParameters() []string {
	return []string{S3AuthAdapterParameter}
}

func (p *S3AuthAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	var alt map[string]interface{}
	alt, err := env.RemapOptions.ValueByNameAsStringMapInterface(S3AuthAdapterParameter)
	if err != nil {
		return nil, err
	}

	args, err := p.processArgs(alt)
	if err != nil {
		return nil, err
	}

	return args, nil
}

func (p *S3AuthAdapter) processArgs(alt map[string]interface{}) ([]string, error) {
	var args []string
	args = p.maybeAddArgs(alt["path"], "config", args)
	args = p.maybeAddArgs(alt["auth"], "config", args)
	args = p.maybeAddArgs(alt["virtual_host"], "virtual_host", args)
	args = p.maybeAddArgs(alt["version"], "version", args)
	args = p.maybeAddArgs(alt["v4_include_headers"], "v4-include-headers", args)
	args = p.maybeAddArgs(alt["v4_exclude_headers"], "v4-exclude-headers", args)
	args = p.maybeAddArgs(alt["region_map"], "v4-region-map", args)

	// if this is v4 and there is no v4_exclude_headers defined, add a @pparam=--v4-exclude-headers=x-forwarded-for,forwarded,via,authorization
	_, excludeHeadersDefined := alt["v4_exclude_headers"]
	if alt["version"] == 4 && !excludeHeadersDefined {
		args = p.maybeAddArgs("x-forwarded-for,forwarded,via,authorization", "v4-exclude-headers", args)
	}

	return args, nil
}

func (p *S3AuthAdapter) paramToValue(i interface{}) string {
	switch val := i.(type) {
	case string:
		return val
	case int:
		return strconv.Itoa(val)
	case bool:
		return strconv.FormatBool(val)
	case []string:
		return strings.Join(val, ",")
	case []interface{}:
		var vals []string
		for _, v1 := range i.([]interface{}) {
			vS := v1.(string)
			vals = append(vals, vS)
		}
		return strings.Join(vals, ",")
	default:
		return ""
	}
}

func (p *S3AuthAdapter) maybeAddArgs(option interface{}, param string, args []string) []string {
	value := p.paramToValue(option)
	switch param {
	case "virtual_host":
		if value == "true" {
			return append(args, "--virtual_host")
		}
	case "config":
		if len(value) > 0 {
			return append(args, []string{"--config", value}...)
		}
	default:
		if len(value) > 0 {
			return append(args, fmt.Sprintf("--%s=%s", param, value))
		}
	}
	return args
}
