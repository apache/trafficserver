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
	"fmt"
	"strconv"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
)

const (
	EscalateAdapterFailoverParameter = "failover"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&EscalateAdapter{})
}

type EscalateAdapter struct {
}

func (p *EscalateAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *EscalateAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("escalate")
}

func (p *EscalateAdapter) Name() string {
	return "escalate"
}

func (p *EscalateAdapter) SharedLibraryName() string {
	return "escalate.so"
}

func (p *EscalateAdapter) ConfigParameters() []string {
	return []string{EscalateAdapterFailoverParameter}
}

func (p *EscalateAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ParentConfig}
}

func (p *EscalateAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	alt, err := env.RemapOptions.ValueByNameAsStringMapInterface(EscalateAdapterFailoverParameter)
	if err != nil {
		return nil, err
	}

	if err = p.validateArgs(alt); err != nil {
		return nil, err
	}

	args, err := p.processArgs(alt)
	if err != nil {
		return nil, err
	}

	return args, nil
}

func (p *EscalateAdapter) validateArgs(alt map[string]interface{}) error {
	domain := p.paramToValue(alt["domain"])
	statusCodes := p.paramToValue(alt["status_codes"])
	if len(domain) == 0 || len(statusCodes) == 0 {
		return fmt.Errorf("%q and %q are required parameters", "domain", "status_codes")
	}

	value := p.paramToValue(alt["host_header"])
	if len(value) > 0 && (value != "alias" && value != "origin") {
		return fmt.Errorf("invalid 'host_header' option: %q", value)
	}

	return nil
}

func convertStatusCodes(in interface{}) []string {
	switch val := in.(type) {
	case []string:
		return val
	case []interface{}:
		var vals []string
		for _, v1 := range in.([]interface{}) {
			vS := strconv.Itoa(v1.(int))
			vals = append(vals, vS)
		}
		return vals
	}
	return nil
}

func (p *EscalateAdapter) processArgs(alt map[string]interface{}) ([]string, error) {
	var args []string

	{
		value := p.paramToValue(alt["host_header"])
		if len(value) > 0 && value == "origin" {
			args = append(args, "--pristine")
		}
	}

	statusCodes := convertStatusCodes(alt["status_codes"])
	domain := p.paramToValue(alt["domain"])

	args = append(args, fmt.Sprintf("%s:%s", strings.Join(statusCodes, ","), domain))

	return args, nil
}

func (p *EscalateAdapter) paramToValue(i interface{}) string {
	switch val := i.(type) {
	case string:
		return val
	case bool:
		return strconv.FormatBool(val)

	case []string:
		return strings.Join(val, ",")
	case []interface{}:
		var vals []string
		for _, v1 := range i.([]interface{}) {
			vS := strconv.Itoa(v1.(int))
			vals = append(vals, vS)
		}
		return strings.Join(vals, ",")
	default:
		return ""
	}
}
