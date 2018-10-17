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
	"bytes"
	"fmt"
	"sort"

	"github.com/apache/trafficserver/tools/voluspa"
)

const (
	logCookieDirective = "log_cookie"
	logHeaderDirective = "log_header"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&LogAdapter{})
}

type LogAdapter struct {
}

func (p *LogAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *LogAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("log_field")
}

func (p *LogAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *LogAdapter) ConfigParameters() []string {
	return []string{logCookieDirective, logHeaderDirective}
}

func (p *LogAdapter) logCookieHeader(logFields map[string]string, content *bytes.Buffer, parameterName string, index int) (*bytes.Buffer, int) {
	var logFieldNames []string

	for k := range logFields {
		logFieldNames = append(logFieldNames, k)
	}
	sort.Strings(logFieldNames)

	content.WriteString(fmt.Sprintf(
		`cond %%{REMAP_PSEUDO_HOOK}`,
	))
	content.WriteString("\n")

	for _, logFieldName := range logFieldNames {
		logFieldValue := logFields[logFieldName]
		if parameterName == "log_cookie" {
			content.WriteString(fmt.Sprintf(
				`    set-header Bazinga%d %%{COOKIE:%s}`,
				index, logFieldValue,
			))
			index++
		} else if parameterName == "log_header" {
			content.WriteString(fmt.Sprintf(
				`    set-header Bazinga%d %%{HEADER:%s}`,
				index, logFieldValue,
			))
			index++
		}
		content.WriteString("\n")
	}
	return content, index
}

func (p *LogAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}
	index := 1

	// Log Cookies
	logFields, _ := env.RemapOptions.ValueByNameAsStringMapString(logCookieDirective)
	if len(logFields) > 0 {
		content, index = p.logCookieHeader(logFields, content, logCookieDirective, index)
	}

	// Log headers
	logFields, _ = env.RemapOptions.ValueByNameAsStringMapString(logHeaderDirective)
	if len(logFields) > 0 {
		content, _ = p.logCookieHeader(logFields, content, logHeaderDirective, index)
	}
	return content, nil
}
