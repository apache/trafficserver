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
	"bytes"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	allowIPAdapterParameter       = "allow_ip"
	allowIPAdapterParameterParent = "allow_ip_parent"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&AllowIPAdapter{})
}

type AllowIPAdapter struct {
}

func (*AllowIPAdapter) Weight() int {
	return 120
}
func (p *AllowIPAdapter) PluginType() voluspa.PluginType {
	return voluspa.ActionAdapter
}

func (p *AllowIPAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("allow_ip")
}

func (p *AllowIPAdapter) ConfigParameters() []string {
	return []string{allowIPAdapterParameter, allowIPAdapterParameterParent}
}

func (p *AllowIPAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	switch env.ConfigLocation {
	case voluspa.UnknownLocation, voluspa.ChildConfig:
		return p.configContent(env, allowIPAdapterParameter)
	case voluspa.ParentConfig:
		return p.configContent(env, allowIPAdapterParameterParent)
	}
	return nil, fmt.Errorf("Unhandled configLocation %q", env.ConfigLocation)
}

func (p *AllowIPAdapter) configContent(env *voluspa.ConfigEnvironment, parameterName string) (*bytes.Buffer, error) {
	ips, err := env.RemapOptions.ValueByNameAsSlice(parameterName)
	if err != nil {
		return nil, nil
	}

	if len(ips) == 0 {
		return nil, util.FormatError(parameterName, fmt.Errorf("empty IP range speclist"))
	}

	// TODO other validation on actual content

	content := &bytes.Buffer{}
	for _, ip := range ips {
		fmt.Fprintf(content, "@src_ip=%s ", ip)
	}
	content.WriteString("@action=allow")

	return content, nil
}
