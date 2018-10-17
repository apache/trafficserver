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

// parentDomainMap maps domain names to parentDomain struct
type parentDomainMap map[string]*parentDomain

type ParentConfigurator struct {
	options *Options
}

type Override struct {
	schemes           map[string]bool
	primaryList       []string
	secondaryList     []string
	HTTPPort          int
	HTTPSPort         int
	strategy          string
	goDirect          bool
	ignoreQueryString bool
	overrideDomain    string
}

type parentDomain struct {
	originalDomain    string
	schemes           map[string]bool
	ignoreQueryString bool
	OverridesByRole   map[string]Override
}

func newParentDomain(domain string) *parentDomain {
	return &parentDomain{originalDomain: domain, schemes: make(map[string]bool), OverridesByRole: make(map[string]Override)}
}

func newParentConfigurator(options *Options) *ParentConfigurator {
	return &ParentConfigurator{
		options: options,
	}
}

func (p *ParentConfigurator) Do(parsedConfigs []*CustomerConfig, merge bool) ([]ManagedFile, error) {
	if merge {
		return p.get(parsedConfigs, NoCDN)
	}

	// otherwise, group properties by CDN and generate parent.config for each CDN separately
	grouped := groupCustomerConfigsByCDN(parsedConfigs)

	var managedFiles []ManagedFile
	for cdn, configs := range grouped {
		files, err := p.get(configs, cdn)
		if err != nil {
			return nil, err
		}
		managedFiles = append(managedFiles, files...)
	}

	return managedFiles, nil

}

func (p *ParentConfigurator) get(parsedConfigs []*CustomerConfig, cdn string) ([]ManagedFile, error) {
	// a map of roles to parentDomainMaps
	parentDomains := make(map[string]parentDomainMap)
	for _, parsedConfig := range parsedConfigs {

		role := parsedConfig.role
		if _, exists := parentDomains[role]; !exists {
			parentDomains[role] = make(parentDomainMap)
		}

		for _, domain := range parsedConfig.parentDomains {
			if parsedConfig.lifecycle == Retired {
				continue
			}

			for _, remap := range parsedConfig.Remaps {
				if remap.ConfigLocation == ParentConfig {
					continue
				}

				pd, ok := parentDomains[role][domain]
				if !ok {
					pd = newParentDomain(domain)
					parentDomains[role][domain] = pd
				}

				// extract schemes from toplevel or from remap rules
				if len(remap.scheme) > 0 {
					pd.schemes[remap.scheme] = true
				} else {
					for _, scheme := range parsedConfig.schemes {
						pd.schemes[scheme] = true
					}
				}

				scheme, err := extractScheme(remap.IncomingURL)
				if err != nil {
					return nil, fmt.Errorf("could not extract scheme from %q: %s", remap.IncomingURL, err)
				}

				if len(scheme) > 0 {
					pd.schemes[scheme] = true
				}

				if len(remap.parentOverrides) > 0 && remap.parentDomain == pd.originalDomain {
					for _, o := range remap.parentOverrides {
						ignore := o.IgnoreQuerystring
						if hasRemoveAllParamsOption(remap.sourceConfig.RemapOptions) {
							ignore = true
						}
						pd.OverridesByRole[o.Role] = Override{
							primaryList:       o.PrimaryList,
							secondaryList:     o.SecondaryList,
							HTTPPort:          o.HTTPPort,
							HTTPSPort:         o.HTTPSPort,
							ignoreQueryString: ignore,
							strategy:          o.Strategy,
							goDirect:          o.GoDirect,
							overrideDomain:    o.DestDomain,
						}
					}
				}

				if remap.sourceConfig.RemapOptions == nil {
					continue
				}

				// We obey if it was set for any remap
				if hasRemoveAllParamsOption(remap.sourceConfig.RemapOptions) || hasIgnoreQuery(remap.sourceConfig.RemapOptions) {
					pd.ignoreQueryString = true
				}

			}
		}
	}

	return p.ExpandConfigTemplate(parentDomains, cdn)
}

func extractScheme(in string) (string, error) {
	parsedURL, err := url.Parse(in)
	if err == nil {
		return parsedURL.Scheme, nil
	}

	if strings.HasPrefix(in, "https:") {
		return "https", nil
	}

	if strings.HasPrefix(in, "http:") {
		return "http", nil
	}

	return "", nil
}

func hasRemoveAllParamsOption(remapOptions RemapOptions) bool {
	vals, err := remapOptions.ValueByNameAsStringMapInterface("cachekey")
	if err != nil {
		return false
	}

	val, ok := vals["remove_all_params"]
	if !ok {
		return false
	}

	optionValue, ok := val.(bool)
	if !ok {
		return false
	}

	return optionValue
}

func hasIgnoreQuery(remapOptions RemapOptions) bool {
	vals, err := remapOptions.ValueByNameAsStringMapInterface("cachekey")
	if err != nil {
		return false
	}

	val, ok := vals["parent_selection"]
	if !ok {
		return false
	}

	optionValue, ok := val.(string)
	if !ok {
		return false
	}
	return optionValue == "ignore_query"
}

func (p *ParentConfigurator) ExpandConfigTemplate(parentDomains map[string]parentDomainMap, cdn string) ([]ManagedFile, error) {
	var files []ManagedFile
	for role := range parentDomains {
		parentDomains, ok := parentDomains[role]
		if !ok {
			return nil, fmt.Errorf("role %q not found", role)
		}

		mf, err := p.expandConfigTemplate(parentDomains, cdn, role)
		if err != nil {
			return nil, err
		}

		if mf == nil {
			continue
		}

		files = append(files, *mf)

		// ATS wants a file on parent hosts. match name that's used in salt
		filename := "parent_empty.config"
		if len(role) > 0 {
			filename = fmt.Sprintf("parent_empty.config_%s", role)
		}

		emptyParentConfig := NewManagedFile(filename, "parent.config", "", role, "", intentionallyEmptyMessage, ParentConfig)
		files = append(files, emptyParentConfig)
	}

	return files, nil
}

// DomainNames allows for implementing the sort interface; regular string sort is not good enough
// we want the longest match to win in the case of bar.com and foo.bar.com.
type DomainNames []string

// var _ sort.Interface = DomainNames{}

func (d DomainNames) Len() int {
	return len(d)
}
func (d DomainNames) Swap(i, j int) {
	d[i], d[j] = d[j], d[i]
}

func (d DomainNames) Less(i, j int) bool {

	if len(strings.Split(d[i], ".")) != len(strings.Split(d[j], ".")) {
		return len(strings.Split(d[i], ".")) > len(strings.Split(d[j], "."))
	}
	if strings.Compare(d[i], d[j]) < 0 {
		return true
	}
	return false
}

var intentionallyEmptyMessage = bytes.NewBufferString("# This file is intentionally left blank\n")

func (p *ParentConfigurator) expandConfigTemplate(parentDomains parentDomainMap, cdn, role string) (*ManagedFile, error) {
	seen := make(map[string]bool)

	var buf bytes.Buffer
	buf.WriteString("\n")

	buf.WriteString(generatedFileBanner)
	var domains DomainNames
	for _, pd := range parentDomains {
		domains = append(domains, pd.originalDomain)
	}
	sort.Sort(domains)

	for _, domain := range domains {

		if _, exists := seen[domain]; exists {
			continue
		}

		pd := parentDomains[domain]

		var schemes []string
		for scheme := range pd.schemes {
			schemes = append(schemes, scheme)
		}
		sort.Strings(schemes)

		for _, scheme := range schemes {
			if len(pd.OverridesByRole) > 0 {

				// sort roles to make diffs more predictable
				roles := make([]string, 0)
				for role := range pd.OverridesByRole {
					roles = append(roles, role)
				}
				sort.Strings(roles)

				for _, role := range roles {
					override := pd.OverridesByRole[role]
					port := override.HTTPPort
					if scheme == "https" {
						port = override.HTTPSPort
					}
					pstr := fmt.Sprintf(`parent="{{ %s_parents }}" {{ secondary_%s_parents }}`, scheme, scheme) // the default
					if len(override.primaryList) > 0 {
						primaryString := "parent=\""
						secondaryString := "secondary_parent=\""
						for _, h := range override.primaryList {
							primaryString += fmt.Sprintf("%s:%d,", h, port)
						}
						primaryString = strings.TrimSuffix(primaryString, ",")
						primaryString += "\""
						if len(override.secondaryList) > 0 {
							for _, h := range override.secondaryList {
								secondaryString += fmt.Sprintf("%s:%d,", h, port)
							}
							secondaryString = strings.TrimSuffix(secondaryString, ",")
							secondaryString += "\""
						} else {
							secondaryString = ""
						}
						pstr = fmt.Sprintf("%s %s", primaryString, secondaryString)
					}
					if role != "" {
						buf.WriteString(fmt.Sprintf("\n{%% if salt.pillar.get(\"%s\") %%}\n", role))
					}
					strategy := override.strategy
					if strategy == "" {
						strategy = "consistent_hash"
					}
					direct := override.goDirect // false is default?
					finalDomain := domain
					if override.overrideDomain != "" {
						finalDomain = override.overrideDomain
					}
					buf.WriteString(fmt.Sprintf(`dest_domain=%s scheme=%s %s round_robin=%s go_direct=%t`, finalDomain, scheme, pstr, strategy, direct))
					if override.ignoreQueryString {
						buf.WriteString(" qstring=ignore")
					}
					buf.WriteString("\n")
					if role != "" {
						buf.WriteString("{% endif %}\n\n")
					}
				}
			} else {
				// This is the "legacy way" with parent_dest_domwain at the mapping level NOTE DEPRECATED
				buf.WriteString(fmt.Sprintf(
					`dest_domain=%s scheme=%s parent="{{ %s_parents }}" {{ secondary_%s_parents }} round_robin=consistent_hash go_direct=false`, domain, scheme, scheme, scheme))
				if pd.ignoreQueryString {
					buf.WriteString(" qstring=ignore")
				}
				buf.WriteString("\n")
			}
		}
		seen[domain] = true
	}

	buf.WriteString("\n")

	if len(seen) == 0 {
		return nil, nil
	}

	filename := "parent.config_default"
	if len(role) > 0 {
		filename = fmt.Sprintf("parent.config_%s", role)
	}

	if cdn != DefaultCDN && cdn != NoCDN {
		filename = fmt.Sprintf("parent.config_%s", cdn)
	}

	mf := NewManagedFile(filename, "parent.config", cdn, role, "", &buf, ChildConfig)

	return &mf, nil
}
