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
      <p>&nbsp;&nbsp;Cluster Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr> 
    <td height="2" colspan="2" class="configureLabel">Cluster</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.local.cluster.type>Type</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.local.cluster.type" value="3" <@checked proxy.local.cluster.type\3>>
                    Single Node<br>
                  <input type="radio" name="proxy.local.cluster.type" value="2" <@checked proxy.local.cluster.type\2>>
                    Management Clustering<br>
                  <input type="radio" name="proxy.local.cluster.type" value="1" <@checked proxy.local.cluster.type\1>>
                    Full Cache Clustering<br>
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies whether <@record proxy.config.product_name>
                        will act as a single-node or as part of a cluster.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <@clear_cluster_stats>

</table>

<@include /configure/c_buttons.ink>

<@include /monitor/m_footer.ink>

</form>

<@include /include/footer.ink>
