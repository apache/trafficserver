<!-------------------------------------------------------------------------
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;HTTP Cacheability Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel">Caching</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.http>HTTP Caching</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.http" value="1" <@checked proxy.config.http.cache.http\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.cache.http" value="0" <@checked proxy.config.http.cache.http\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables caching of HTTP documents.
                  </ul>
                </td>
              </tr>
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.ftp>FTP over HTTP Caching</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.ftp" value="1" <@checked proxy.config.http.cache.ftp\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.cache.ftp" value="0" <@checked proxy.config.http.cache.ftp\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables caching of FTP requests sent via HTTP.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Behavior</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.required_headers>Required Headers</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.required_headers" value="2" <@checked proxy.config.http.cache.required_headers\2>>	
                    An Explicit Lifetime Header <br>
                  <input type="radio" name="proxy.config.http.cache.required_headers" value="1" <@checked proxy.config.http.cache.required_headers\1>>	
                    A Last-Modified Header <br>
                  <input type="radio" name="proxy.config.http.cache.required_headers" value="0" <@checked proxy.config.http.cache.required_headers\0>>	
                    No Required Headers
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the minimum header information required for a document to be cachable.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.when_to_revalidate>When to Revalidate</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.when_to_revalidate" value="3" <@checked proxy.config.http.cache.when_to_revalidate\3>>	
                    Never Revalidate <br>
                  <input type="radio" name="proxy.config.http.cache.when_to_revalidate" value="2" <@checked proxy.config.http.cache.when_to_revalidate\2>>	
                    Always Revalidate <br>
                  <input type="radio" name="proxy.config.http.cache.when_to_revalidate" value="1" <@checked proxy.config.http.cache.when_to_revalidate\1>>	
                    Revalidate if Heuristic Expiration <br>
                  <input type="radio" name="proxy.config.http.cache.when_to_revalidate" value="0" <@checked proxy.config.http.cache.when_to_revalidate\0>>	
                    Use Cache Directive or Heuristic
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies when to revalidate documents.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.when_to_add_no_cache_to_msie_requests>Add "no-cache" to MSIE Requests</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.when_to_add_no_cache_to_msie_requests" value="2" <@checked proxy.config.http.cache.when_to_add_no_cache_to_msie_requests\2>>
                    To All MSIE Requests <br>
                  <input type="radio" name="proxy.config.http.cache.when_to_add_no_cache_to_msie_requests" value="1" <@checked proxy.config.http.cache.when_to_add_no_cache_to_msie_requests\1>>
                    To IMS MSIE Requests <br>
                  <input type="radio" name="proxy.config.http.cache.when_to_add_no_cache_to_msie_requests" value="0" <@checked proxy.config.http.cache.when_to_add_no_cache_to_msie_requests\0>>
                  Not to Any MSIE Requests 
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies when to add "no-cache" directives to Microsoft Internet Explorer requests.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.ignore_client_no_cache>Ignore "no-cache" in Client Requests</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.ignore_client_no_cache" value="1" <@checked proxy.config.http.cache.ignore_client_no_cache\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.cache.ignore_client_no_cache" value="0" <@checked proxy.config.http.cache.ignore_client_no_cache\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> ignores 
                        "no-cache" header in client requests to bypass the cache.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Freshness</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.heuristic_min_lifetime>Minimum Heuristic Lifetime</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.http.cache.heuristic_min_lifetime" value="<@record proxy.config.http.cache.heuristic_min_lifetime>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the minimum amount of time that a document in the cache can be considered fresh if an explicit Expires header is not present.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.heuristic_max_lifetime>Maximum Heuristic Lifetime</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.http.cache.heuristic_max_lifetime" value="<@record proxy.config.http.cache.heuristic_max_lifetime>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the maximum amount of time that a document in the cache can be considered fresh if an explicit Expires header is not present.
                  </ul>
                </td>
              </tr>
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.ftp.cache.document_lifetime>FTP Document Lifetime</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.http.ftp.cache.document_lifetime" value="<@record proxy.config.http.ftp.cache.document_lifetime>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the maximum amount of time that an FTP document 
                        can stay in the <@record proxy.config.product_name> cache.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.cache.limits.http.max_alts>Maximum Alternates</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.cache.limits.http.max_alts" value="<@record proxy.config.cache.limits.http.max_alts>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the maximum number of alternates <@record proxy.config.product_name> can cache for an HTTP document.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Vary Based on Content Type</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.vary_default_headers>Enable/Disable</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.enable_default_vary_headers" value="1" <@checked proxy.config.http.cache.enable_default_vary_headers\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.cache.enable_default_vary_headers" value="0" <@checked proxy.config.http.cache.enable_default_vary_headers\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Enables/Disables caching of alternate versions for
                        HTTP documents that do not contain the Vary header.
                        If no Vary header is present,
                        <@record proxy.config.product_name> will vary on the headers
                        specified below, depending on the document's content
                        type.
                  </ul>
                </td>
              </tr>
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.vary_default_text>Vary by Default on Text</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.http.cache.vary_default_text" value="<@record proxy.config.http.cache.vary_default_text>">
                </td>
                <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Specifies the header on which <@record proxy.config.product_name> varies for text documents.
                  </ul>
                </td>
              </tr>
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.vary_default_images>Vary by Default on Images</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.http.cache.vary_default_images" value="<@record proxy.config.http.cache.vary_default_images>">
                </td>
                <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Specifies the header on which <@record proxy.config.product_name> varies for images.
                  </ul>
                </td>
              </tr>
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.vary_default_other>Vary by Default on Other Document Types</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.http.cache.vary_default_other" value="<@record proxy.config.http.cache.vary_default_other>">
                </td>
                <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Specifies the header on which <@record proxy.config.product_name> varies for 
                        anything other than text and images.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Dynamic Caching</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.cache_urls_that_look_dynamic>Caching Documents with Dynamic URLs</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.cache_urls_that_look_dynamic" value="1" <@checked proxy.config.http.cache.cache_urls_that_look_dynamic\1>>
                    Enabled <br> 
                  <input type="radio" name="proxy.config.http.cache.cache_urls_that_look_dynamic" value="0" <@checked proxy.config.http.cache.cache_urls_that_look_dynamic\0>>
                    Disabled 
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> will attempt to cache pages it recognizes as dynamic by checking the URL. A URL is considered dynamic if it contains a question mark(?), a semi-colon(;), cgi, or if it ends in .asp. However, pages with "no-cache" headers will not be cached. 
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.cache_responses_to_cookies>Caching Response to Cookies</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="3" <@checked proxy.config.http.cache.cache_responses_to_cookies\3>>
                    Cache All but Text <br>
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="2" <@checked proxy.config.http.cache.cache_responses_to_cookies\2>>
                    Cache Only Image Types <br>
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="1" <@checked proxy.config.http.cache.cache_responses_to_cookies\1>>
                    Cache Any Content-Type <br>
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="0" <@checked proxy.config.http.cache.cache_responses_to_cookies\0>>
                    No Cache on Cookies
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies how cookies are cached.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
       <@submit_error_flg proxy.config.cache.control.filename><a name="cache_policy">Caching Policy/Forcing Document Caching</a>
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.cache.control.filename>" file lets
      you specify how a particular group of URLs should be cached.
      You can also define ttl-in-cache rules that force a document to get
      cached for the specified duration regardless of Cache-Control response headers. 
      These rules can also be applied to documents with dynamic URLs.
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_cache_config.ink>
   </td>
  </tr>
  <tr>
   <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_http_cachability.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_cache_config.ink&fname=<@record proxy.config.cache.control.filename>&frecord=proxy.config.cache.control.filename', 'displayWin');">
   </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
