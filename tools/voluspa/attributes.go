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

package voluspa

import "sort"

// GetCDNs returns a list of CDNs in current config set
func (v *Voluspa) GetCDNs() []string {
	cdns := make(map[string]interface{})
	for _, parsedConfig := range v.parsedConfigs {
		for cdn := range parsedConfig.cdn {
			cdns[cdn] = nil
		}
	}

	var all []string
	for k := range cdns {
		all = append(all, k)
	}
	sort.Strings(all)

	return all
}

// GetRoles returns a list of Roles in current config set
func (v *Voluspa) GetRoles() []string {
	roles := make(map[string]interface{})
	for _, parsedConfig := range v.parsedConfigs {
		roles[parsedConfig.role] = nil
	}

	var all []string
	for k := range roles {
		all = append(all, k)
	}
	sort.Strings(all)

	return all
}
