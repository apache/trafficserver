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

import (
	"bytes"
	"fmt"
	"net/url"
	"sort"
	"strings"
)

// NewCustomerConfig creates a new CustomerConfig from a PropertyConfig
func NewCustomerConfig(p *PropertyConfig, configFilename string, options Options) (*CustomerConfig, error) {
	config, err := newCustomerConfig(p)
	if err != nil {
		return nil, err
	}

	if options.PromoteRolesToCDN && isPromotableRole(p.Role) {
		parts := strings.SplitN(p.Role, "_", 2)
		if len(parts) == 2 {
			// If the only CDN is DefaultCDN, purge and use the role
			if len(p.CDN) == 1 && p.CDN[0] == DefaultCDN {
				config.cdn = make(map[string]interface{})
			}

			config.cdn[parts[1]] = nil
			config.role = ""
		}
	}

	config.filename = configFilename

	configLocations := []ConfigLocation{UnknownLocation}
	if p.ParentChild {
		configLocations = []ConfigLocation{ChildConfig, ParentConfig}
	}

	var mappings []*Mapping

	// mappings are like explicit, but smarter
	// They're parent/child aware, no longer requiring different rulesets for each type.
	for _, m := range p.Mappings {

		schemes := p.Schemes
		if len(m.Schemes) > 0 {
			schemes = m.schemes()
		}

		for _, alias := range m.Alias {
			for _, scheme := range schemes {
				for _, configLocation := range configLocations {

					incoming := formatURL(scheme, alias)
					originURL := formatURL(scheme, m.Origin)

					// for parent/child, use the first incoming alias as the origin URL. on the parent side,
					// we map it as the inbound
					if configLocation == ChildConfig && m.RemapOptions.isParentChildEnabled() {
						originURL = formatURL(scheme, m.Alias[0])
					}

					if configLocation == ParentConfig && m.RemapOptions.isParentChildEnabled() {
						if len(m.Alias) > 0 && alias != m.Alias[0] {
							continue
						}
					}

					// clone separate versions of RemapOptions with the appropriate key set
					ro := m.RemapOptions.Clone()

					// clear out manually set attributes
					delete(ro, Parent)
					delete(ro, Child)

					switch configLocation {
					case ParentConfig:
						ro[Parent] = true
					case ChildConfig:
						ro[Child] = true
					}

					mapping := &Mapping{
						Origin:         originURL,
						Alias:          []string{incoming},
						Group:          DefaultGroup,
						Schemes:        []string{scheme},
						RegexMap:       m.RegexMap,
						RuleName:       m.RuleName,
						RemapOptions:   ro,
						id:             m.id,
						ParentOverride: m.ParentOverride,
					}

					if configLocation == ChildConfig && m.RemapOptions.isParentChildEnabled() {
						// only include the first alias of an alias set. as seen above, we only map
						// through the first alias to the parent
						if alias == m.Alias[0] {
							if len(m.ParentDestDomain) > 0 {
								mapping.ParentDestDomain = m.ParentDestDomain
							} else {
								mapping.ParentDestDomain = stripScheme(incoming)
							}
						}
					}

					mappings = append(mappings, mapping)
				}
			}
		}
	}

	for _, m := range p.ExplicitMappings {
		m.explicit = true
		mappings = append(mappings, m)
	}

	for _, m := range mappings {

		schemes := p.Schemes
		if len(m.Schemes) > 0 {
			schemes = m.schemes()
		}

		for _, scheme := range schemes {
			for _, incoming := range m.Alias {

				for _, configLocation := range configLocations {
					if !forCurrentMatch(configLocation, &m.RemapOptions) {
						continue
					}

					env := newConfigEnvironment(p, options, m.id, configLocation)

					mrules, err := convertToMappingRules(m.RemapOptions, env, m.explicit)
					if err != nil {
						return nil, err
					}

					if configLocation == ChildConfig && m.RemapOptions.isParentChildEnabled() {
						config.parentDomains = append(config.parentDomains, m.ParentDomain())
					}

					incomingURL := formatURL(scheme, incoming)
					originURL := formatURL(scheme, m.Origin)

					remap := Remap{
						IncomingURL:     incomingURL,
						OriginURL:       originURL,
						Group:           m.Group,
						Role:            m.Role,
						RegexMap:        m.RegexMap,
						mappingRules:    mrules,
						ConfigLocation:  env.ConfigLocation,
						sourceConfig:    env,
						scheme:          scheme,
						parentOverrides: m.ParentOverride,
						parentDomain:    m.ParentDomain(),
					}

					config.Remaps = append(config.Remaps, remap)
				}
			}
		}
	}

	// For now, sort if the config has mappings. Later, introduce the change for explicit and others
	if len(p.Mappings) > 0 {
		sort.Stable(sort.Reverse(byIncomingURL(config.Remaps)))
	}

	return config, nil
}

func stripScheme(in string) string {
	if !strings.Contains(in, "/") {
		return in
	}
	parts := strings.Split(in, "/")
	return parts[2]
}

func forCurrentMatch(configLocation ConfigLocation, ro *RemapOptions) bool {
	if configLocation == ChildConfig && ro.isParentConfig() {
		return false
	}
	if configLocation == ParentConfig && ro.isChildConfig() {
		return false
	}
	return true
}

func (m *Mapping) schemes() []string {
	if len(m.Schemes) > 0 {
		return m.Schemes
	}

	return []string{""}
}

// formatURL will create URL from scheme and incoming, unless incoming already has scheme applied to it
func formatURL(scheme, incoming string) string {
	if strings.HasPrefix(incoming, "/") {
		return incoming
	}

	if len(scheme) == 0 {
		return incoming
	}

	u, err := url.Parse(incoming)
	if err == nil && strings.HasPrefix(u.Scheme, "http") {
		return incoming
	}

	// in the error case, it could be a regex URL that url.Parse barfs on. try a brute force method
	if strings.HasPrefix(incoming, "http:") || strings.HasPrefix(incoming, "https:") {
		return incoming
	}

	return fmt.Sprintf("%s://%s", scheme, incoming)
}

// isParentChildEnabled returns the value specified by "parent_child". defaults to true
func (options *RemapOptions) isParentChildEnabled() bool {
	value, err := options.ValueByNameAsBool("parent_child")
	if err == nil {
		return value
	}
	return true
}

// isParentConfig returns the value specified by "parent". defaults to false
func (options *RemapOptions) isParentConfig() bool {
	value, err := options.ValueByNameAsBool(Parent)
	if err == nil {
		return value
	}
	return false
}

// isChildConfig returns the value specified by "child". defaults to false
func (options *RemapOptions) isChildConfig() bool {
	value, err := options.ValueByNameAsBool(Child)
	if err == nil {
		return value
	}
	return false
}

// Iterate over all "rules". adding to array of *ATSPLugin
//		If a general adapter, use general logic
//		If an action, use action logic
//		otherwise, assume a compound adapter
//			- group by type
// Iterate over groups of compounds (calculated above)
//		Append to adapter array
// Sort array by weights and return

func convertToMappingRules(ro RemapOptions, env *ConfigEnvironment, explicit bool) ([]mappingRule, error) {
	var adapters []mappingRule

	compounds := make(map[AdapterType][]AdapterType)

	// stable ordering
	var options []string
	for option := range ro {
		options = append(options, option)
	}
	sort.Strings(options)

	var errs Errors

	seen := make(map[Adapter]interface{})
	for _, option := range options {
		if option == "storage_volume" { // prevent warning on rule property that isn't associated with an adapter
			continue
		}
		adapterType := AdaptersRegistry.adapterTypeByConfigName(option)
		if adapterType == UnknownAdapter {
			// Warn when there's garbage in
			if !IsValidConfigurationOption(option) {
				errs = append(errs, fmt.Errorf("unknown configuration option %q", option))
			}
			continue
		}

		vp := AdaptersRegistry.adapterForType(adapterType)
		if vp == nil {
			errs = append(errs, fmt.Errorf("unknown adapter %q", adapterType))
			continue
		}

		// NOTE: even though an adapter can have multiple options associated with it,
		// it's the first invocation that only matters. Once inside PParams, the adapter
		// is responsible for determining any other usage of itself
		if _, ok := seen[vp]; ok {
			continue
		}
		seen[vp] = nil

		if env.ConfigLocation != UnknownLocation {
			vpcp, ok := vp.(ParentChildAdapter)
			matches := false
			if ok {
				for _, loc := range vpcp.ConfigLocations() {
					if loc == env.ConfigLocation {
						matches = true
					}
				}

				if !matches && explicit {
					// ignore and let through
				} else if !matches {
					continue
				}
			}
		}

		env.RemapOptions = ro

		switch vp.PluginType() {
		case GeneralAdapter:
			adapter, err := createAdapter(vp, env)
			if err != nil {
				errs = append(errs, err)
				continue
			}

			if adapter.hasContent() {
				adapters = append(adapters, *adapter)
			}
		case ActionAdapter:
			adapter, err := createAction(vp, env)
			if err != nil {
				errs = append(errs, err)
				continue
			}
			if adapter.hasContent() {
				adapters = append(adapters, *adapter)
			}
		case CompoundAdapter:
			cta, ok := vp.(CompoundTypeAdapter)
			if !ok {
				errs = append(errs, fmt.Errorf("compound adapter does not implement CompoundTypeAdapter"))
				continue
			}
			compounds[cta.CompoundType()] = append(compounds[cta.CompoundType()], adapterType)
		}
	}

	for pt, sp := range compounds {

		sort.Sort(receiptsFirst(sp))

		adapter, err := createCompoundMappingRule(pt, ro, sp, env)
		if err != nil {
			errs = append(errs, err)
			continue
		}

		if adapter.hasContent() {
			adapters = append(adapters, *adapter)
		}

	}

	sort.Stable(byRuleWeight(adapters))

	if len(errs) > 0 {
		return nil, errs
	}

	return adapters, nil
}

func createCompoundMappingRule(compoundType AdapterType, ro RemapOptions, adapters []AdapterType, env *ConfigEnvironment) (*mappingRule, error) {
	vp := AdaptersRegistry.adapterForType(compoundType)
	if vp == nil {
		return nil, fmt.Errorf("unknown AdapterType %q", compoundType)
	}

	var errs Errors
	var content bytes.Buffer

	adapter := baseCreateAdapter(vp, env)

	ccontent, err := ConfigContent(vp, env)
	if err != nil {
		errs = append(errs, err)
	}
	adapter.ConfigContent = ccontent

	ccontent, err = SubConfigContent(vp, env)
	if err != nil {
		errs = append(errs, err)
	}

	if ccontent != nil {
		content.Write(ccontent.Bytes())
	}

	var buffer bytes.Buffer
	for _, adapterType := range adapters {
		vps := AdaptersRegistry.adaptersForType(adapterType)
		for _, vp = range vps {
			if vp == nil {
				return nil, fmt.Errorf("unknown AdapterType %s", adapterType)
			}

			// filter out unrelated types
			cta, ok := vp.(CompoundTypeAdapter)
			if ok && cta.CompoundType() != compoundType {
				continue
			}

			vpp, ok := vp.(ParameterAdapter)
			if ok {
				pparams, err := vpp.PParams(env)
				if err != nil {
					errs = append(errs, err)
				}

				for _, val := range pparams {
					fmt.Fprintf(&buffer, " @pparam=%s", val)
				}
			}

			env.RemapOptions = ro
			val, err := SubConfigContent(vp, env)
			if err != nil {
				errs = append(errs, err)
			}

			if val != nil && val.Len() > 0 {
				content.Write(val.Bytes())
				content.WriteString("\n")
			}
		}
	}

	if len(errs) > 0 {
		return nil, errs
	}

	if buffer.Len() > 0 {
		adapter.ConfigContent.WriteString(buffer.String())
	} else if content.Len() == 0 {
		// if no subconfig content, then there's nothing to be output (at least at this time)
		adapter.ConfigContent = &bytes.Buffer{}
	}

	adapter.SubConfigContent = &content

	return adapter, nil
}

func baseCreateAdapter(vp Adapter, env *ConfigEnvironment) *mappingRule {
	adapter := newMappingRule(vp)

	scp, ok := vp.(SubConfigAdapter)
	if ok {
		adapter.SubConfigFileName = SubConfigFilename(scp, env)
	}

	return adapter
}

func createAdapter(vp Adapter, env *ConfigEnvironment) (*mappingRule, error) {
	adapter := baseCreateAdapter(vp, env)

	content, err := ConfigContent(vp, env)
	if err != nil {
		return nil, err
	}
	adapter.ConfigContent = content

	content, err = SubConfigContent(vp, env)
	if err != nil {
		return nil, err
	}
	adapter.SubConfigContent = content

	return adapter, nil
}

func createAction(vp Adapter, env *ConfigEnvironment) (*mappingRule, error) {
	adapter := baseCreateAdapter(vp, env)
	content, err := SubConfigContent(vp, env)
	if err != nil {
		return nil, err
	}
	adapter.SubConfigContent = content
	adapter.ConfigContent = content

	return adapter, nil
}

// receiptsFirst implements sort.Interface, sorting by AdapterType, bubbling Receipt to the top.
type receiptsFirst []AdapterType

func (s receiptsFirst) Len() int           { return len(s) }
func (s receiptsFirst) Swap(i, j int)      { s[i], s[j] = s[j], s[i] }
func (s receiptsFirst) Less(i, j int) bool { return s[i] == Receipt }

// byRuleWeight implements sort.Interface, sorting by a Rule's Weight.
type byRuleWeight []mappingRule

func (s byRuleWeight) Len() int           { return len(s) }
func (s byRuleWeight) Swap(i, j int)      { s[i], s[j] = s[j], s[i] }
func (s byRuleWeight) Less(i, j int) bool { return s[i].Weight < s[j].Weight }

// byIncomingURL will sort by incoming URL domain
// for the same scheme and inbound host:
//   http will be put before https
//   empty paths come last
//   sorting alphabetically
//   ignoring host-less and regex remaps
type byIncomingURL []Remap

func (s byIncomingURL) Len() int      { return len(s) }
func (s byIncomingURL) Swap(i, j int) { s[i], s[j] = s[j], s[i] }
func (s byIncomingURL) Less(i, j int) bool {
	if s[i].RegexMap || s[j].RegexMap {
		return false
	}

	u1, _ := url.Parse(s[i].IncomingURL)
	u2, _ := url.Parse(s[j].IncomingURL)
	if u1 == nil || u2 == nil {
		return len(s[i].IncomingURL) < len(s[j].IncomingURL)
	}

	if len(u1.Host) == 0 || len(u2.Host) == 0 {
		return false
	}

	if u1.Host == u2.Host {
		if u1.Scheme == u2.Scheme {
			if len(u1.Path) == 0 || len(u2.Path) == 0 {
				return u1.Path < u2.Path
			}
			return u1.Path > u2.Path
		}
		return !(u1.Scheme == "http" || u2.Scheme == "http")
	}

	return u1.Path < u2.Path
}
