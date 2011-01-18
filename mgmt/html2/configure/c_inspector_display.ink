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

<@include /include/html_header.ink>

<body class="primaryColor">

<form method="get" action="/submit_inspector_display.cgi?">
<input type=hidden name=url value=<@query url>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
  <tr class="hilightColor"> 
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td width="100%" height="10"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
  <tr> 
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="whiteColor" width="100%" align="left" valign="top">

      <table width="100%" border="0" cellspacing="0" cellpadding="0">
        <tr class="tertiaryColor">
          <td class="greyLinks">&nbsp;&nbsp;Cache Inspector Result</td>
        </tr>
      </table>

      <@cache_query>

    </td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
  <tr> 
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
</table>

</form>
</body>

<@include /include/html_footer.ink>
