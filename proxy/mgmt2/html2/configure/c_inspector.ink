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

<form method="post" name="inspector_form" action="/submit_inspector.cgi?<@link_query>">
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Cache Inspector</p>
    </td>
  </tr>
</table>

<@submit_error_msg>

<!-------------------------------------------------------------------------
  blue bar
  ------------------------------------------------------------------------->
<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
  </tr>
</table>

<script language="Javascript1.2">
urllist = new Array(100);
index = 0;
function addToUrlList(input) {
  for (c=0; c < index; c++) {
    if (urllist[c] == input.name) {
      urllist.splice(c,1);
      index--;
      return true;
    }
  }
  urllist[index++] = input.name;
  return true;
}

function setUrls(form) {
  form.elements[0].value="";
  if (index > 10) {
    alert("Can't choose more than 10 urls for deleting");
    return true;
  }
  for (c=0; c < index; c++){
    form.elements[0].value += urllist[c]+ "%0D%0A";
  }
  if (form.elements[0].value == ""){
    alert("Please select atleast one url before clicking delete");
    return true;
  }
  srcfile="/configure/submit_inspector_display.cgi?url=" + form.elements[0].value + "&url_op=Delete";
  window.open(srcfile, 'display', 'width=600, height=400');
  return true;
}
</SCRIPT>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10">
<!-------------------------------------------------------------------------
  light blue bar
  ------------------------------------------------------------------------->
  <tr> 
    <td colspan="2" class="helpBg">
      <font class="configureBody">
        Cache Inspector allows you to retrieve information
        about the cache.  You can lookup, delete, or invalidate
        documents based on an URL or an URL in regular expression.
      </font>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="4" class="configureLabel">URL Operation</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="40" name="url" value="http://">
    </td>
    <td>
      <table>
        <tr>
          <td class="configureHelp" valign="top" align="left"> 
            <input class="configureButton" type=button name="url_lookup" value="Lookup" target="display" onclick="window.open('/configure/submit_inspector_display.cgi?url=' + document.inspector_form.url.value + '&url_op=' + document.inspector_form.url_lookup.value, 'display')">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <input class="configureButton" type=button name="url_delete" value="Delete" target="display" onclick="window.open('/configure/submit_inspector_display.cgi?url=' + document.inspector_form.url.value + '&url_op=' + document.inspector_form.url_delete.value, 'display')">
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">URL Regular Expression Operation</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <textarea cols="35" rows="10" wrap="off" name="regex">http://</textarea>
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <table>
        <tr>
          <td class="configureHelp" valign="top" align="left"> 
            <input class="configureButton" type=submit name="regex_op" value="Lookup">
<!--            <input class="configureButton" type=button name="regex_lookup" value="Lookup" target="display" onclick="window.open('/configure/submit_inspector_display.cgi?regex=' + document.inspector_form.regex.value + '&regex_op=' + document.inspector_form.regex_lookup.value, 'display')"> -->
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <input class="configureButton" type=submit name="regex_op" value="Delete">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <input class="configureButton" type=submit name="regex_op" value="Invalidate">
          </td>
        </tr>
      </table>
    </td>
  </tr>
</form>

<@cache_regex_query>

</table>



<!-------------------------------------------------------------------------
  blue bar
  ------------------------------------------------------------------------->
<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
  </tr>
</table>

<@include /configure/c_footer.ink>
<@include /include/footer.ink>
