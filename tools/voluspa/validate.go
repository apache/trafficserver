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
	"errors"
	"fmt"
	"net"
	"net/url"
	"sort"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa/internal/util/regex"
)

type Errors []error

func (err Errors) Error() string {
	if len(err) == 0 {
		return "Unknown"
	}

	var errOut []string
	for _, e := range err {
		errOut = append(errOut, e.Error())
	}
	sort.Strings(errOut)

	return strings.Join(errOut, "\n")
}

func (err *Errors) Add(newErr error) {
	*err = append(*err, newErr)
}

func (err *Errors) HasErrors() bool {
	return len(*err) > 0
}

func isValidScheme(scheme string) bool {
	return scheme == "http" || scheme == "https"
}

func (v *Voluspa) ensureValidSchemes() Errors {
	var errs Errors
	for _, config := range v.parsedConfigs {
		for _, scheme := range config.schemes {
			if !isValidScheme(scheme) {
				errs.Add(fmt.Errorf("property %q contains invalid scheme: %s (%s)", config.property, scheme, config.filename))
			}
		}

		for _, remap := range config.Remaps {
			if len(remap.scheme) > 0 && !isValidScheme(remap.scheme) {
				errs.Add(fmt.Errorf("property %q contains invalid scheme: %s (%s)", config.property, remap.scheme, config.filename))
			}

		}
	}
	if len(errs) > 0 {
		return errs
	}
	return nil
}

func (v *Voluspa) ensurePropertyNameUniqueness() Errors {
	var errs Errors

	properties := make(map[string]interface{})

	for _, config := range v.parsedConfigs {
		if _, exists := properties[config.property]; exists {
			errs.Add(fmt.Errorf("property %s not unique (%s)", config.property, config.filename))
		} else {
			properties[config.property] = true
		}
	}

	if len(errs) > 0 {
		return errs
	}
	return nil
}

func (v *Voluspa) ensureAliasUniqueness() Errors {
	var errs Errors
	// map of cdn to role to configlocation to alias
	aliases := make(map[string]map[string]map[ConfigLocation]map[string]interface{})

	// Initialize map/struct
	for _, config := range v.parsedConfigs {
		for cdn := range config.cdn {
			if _, exists := aliases[cdn]; !exists {
				aliases[cdn] = make(map[string]map[ConfigLocation]map[string]interface{})
			}
			if _, exists := aliases[cdn][config.role]; !exists {
				aliases[cdn][config.role] = make(map[ConfigLocation]map[string]interface{})
				for _, location := range []ConfigLocation{UnknownLocation, ChildConfig, ParentConfig} {
					aliases[cdn][config.role][location] = make(map[string]interface{})
				}
			}
		}
	}

	for _, config := range v.parsedConfigs {
		for cdn := range config.cdn {
			for _, remap := range config.Remaps {
				alias := remap.IncomingURL

				// aliases are unique based on the aliases itself + the group it's part of and the role
				aliasKey := alias + remap.Group + remap.Role

				if _, exists := aliases[cdn][config.role][remap.ConfigLocation][aliasKey]; exists {
					var err error
					switch {
					case len(config.role) > 0:
						err = fmt.Errorf("alias %s not unique for role %q (%s)", alias, config.role, config.filename)
					case len(remap.Role) > 0:
						err = fmt.Errorf("alias %s not unique for rule role %q (%s)", alias, remap.Role, config.filename)
					default:
						err = fmt.Errorf("alias %s not unique (%s)", alias, config.filename)
					}
					errs.Add(err)
				} else {
					aliases[cdn][config.role][remap.ConfigLocation][aliasKey] = nil
				}
			}
		}
	}

	if len(errs) > 0 {
		return errs
	}
	return nil
}

func (v *Voluspa) ensureHostsResolve() Errors {
	var errs Errors

	for _, config := range v.parsedConfigs {
		for _, remap := range config.Remaps {
			if remap.RegexMap {
				continue
			}

			url, err := url.Parse(remap.OriginURL)
			if err != nil {
				errs.Add(fmt.Errorf("%s invalid url; %s (%s)", remap.OriginURL, err, config.filename))
			}
			if err = isResolvable(url.Host); err != nil {
				errs = append(errs, fmt.Errorf("%s unresolvable host (%s)", url.Host, config.filename))
			}
		}
	}

	if len(errs) > 0 {
		return errs
	}
	return nil
}

func isResolvable(host string) error {
	if strings.Contains(host, ":") {
		host = strings.Split(host, ":")[0]
	}

	_, err := net.LookupHost(host)
	return err
}

// See https://docs.trafficserver.apache.org/en/latest/admin-guide/plugins/cachekey.en.html#capture-definition
// This is either a regex or a "/" + regex_with_capture + "/" + regex_replace + "/"
func validateCacheKeyCapture(input string) error {

	validator := &regex.GoLangRegex{}

	regexes := make([]string, 0)
	currentRe := ""
	prev := ' '

	// loop over all the characters
	for _, c := range input {
		if c == '/' && prev != '\\' { // find an unescaped / - it is the boundary
			if currentRe != "" {
				regexes = append(regexes, currentRe)
			}
			currentRe = ""
			prev = c
			continue
		}
		currentRe += string(c)
		prev = c
	}

	// if nothing was pushed, there was no / at all, so push the whole string and a dummy
	if currentRe != "" {
		regexes = append(regexes, currentRe)
		regexes = append(regexes, "dummy")
	}

	// too many unescaped /'s
	if len(regexes) != 2 {
		return errors.New("unexpected /")
	}

	// check the regexes
	for _, re := range regexes {
		_, err := validator.IsValid(re)
		if err != nil {
			return err
		}
	}
	return nil
}

func (v *Voluspa) validateRegexOptions() Errors {
	validator := &regex.GoLangRegex{}

	var errs Errors
	for _, config := range v.parsedConfigs {
		for _, remap := range config.Remaps {
			if remap.RegexMap {
				_, err := validator.IsValid(remap.IncomingURL)
				if err != nil {
					errs = append(errs, fmt.Errorf("invalid regex URL: %s (%s)", err, config.filename))
				}
			}

			for k := range remap.sourceConfig.RemapOptions {
				switch k {
				case "header_rewrite", // HANDLE header_rewrite separately (split and look for Cond)
					"regex_remap",
					"asset_token_include",
					"asset_token_exclude":

					v, err := remap.sourceConfig.RemapOptions.ValueByNameAsString(k)
					if err != nil {
						errs = append(errs, fmt.Errorf("%s invalid field. err=%s (%s)", k, err, config.filename))
						continue
					}

					_, err = validator.IsValid(v)
					if err != nil {
						errs = append(errs, fmt.Errorf("invalid regex: %s (%s)", err, config.filename))
					}

				case "content_type_forge":
					forgeMap, err := remap.sourceConfig.RemapOptions.ValueByNameAsStringMapString(k)
					if err != nil {
						errs = append(errs, fmt.Errorf("%s invalid field. err=%s (%s)", k, err, config.filename))
						continue
					}

					for mapKey := range forgeMap {
						_, err := validator.IsValid(mapKey)
						if err != nil {
							errs = append(errs, fmt.Errorf("invalid regex: %s (%s)", err, config.filename))
						}
					}
				case "cachekey":
					cachekeyMap, err := remap.sourceConfig.RemapOptions.ValueByNameAsStringMapInterface(k)
					if err != nil {
						errs = append(errs, fmt.Errorf("%s invalid field. err=%s (%s)", k, err, config.filename))
						continue
					}

					for mapKey, mapValue := range cachekeyMap {
						if mapKey == "regex_replace_path" || mapKey == "regex_replace_path_uri" || mapKey == "capture_path" {
							err := validateCacheKeyCapture(mapValue.(string))
							if err != nil {
								errs = append(errs, fmt.Errorf("invalid regex: %s (%s)", err, config.filename))
							}
						}
						if mapKey == "capture_header" {
							for _, re := range mapValue.([]interface{}) {
								regexParts := strings.Split(re.(string), ":")[1:]
								regexString := strings.Join(regexParts, ":")
								err := validateCacheKeyCapture(regexString)
								if err != nil {
									errs = append(errs, fmt.Errorf("invalid regex: %s (%s)", err, config.filename))
								}

							}
						}
					}
				}
			}
		}
	}

	if len(errs) > 0 {
		return errs
	}
	return nil
}
