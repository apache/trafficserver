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
	"errors"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&AuthProxyAdapter{})
}

type AuthProxyAdapter struct {
}

func (p *AuthProxyAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *AuthProxyAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("auth_proxy")
}

func (p *AuthProxyAdapter) Name() string {
	return "authproxy"
}

func (p *AuthProxyAdapter) ConfigParameters() []string {
	return []string{"authproxy"}
}

func (p *AuthProxyAdapter) SharedLibraryName() string {
	return "authproxy.so"
}

func (p *AuthProxyAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ChildConfig}
}

func (p *AuthProxyAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	value, err := env.RemapOptions.ValueByNameAsString(p.Name())
	if err != nil && len(value) == 0 {
		return nil, errors.New("Need transform parameter")
	}

	return []string{fmt.Sprintf("--auth-transform=%s", value)}, nil
}
