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
	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&NullAdapter{})
}

type NullAdapter struct {
}

func (p *NullAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *NullAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("null")
}

func (p *NullAdapter) Name() string {
	return "null"
}

func (p *NullAdapter) ConfigParameters() []string {
	return []string{"null"}
}

func (p *NullAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	return nil, nil
}
