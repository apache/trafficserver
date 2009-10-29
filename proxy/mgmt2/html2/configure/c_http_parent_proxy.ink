<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Parenting Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.parent_proxy_routing_enable>Parent Proxy</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.http.parent_proxy_routing_enable" value="1" <@checked proxy.config.http.parent_proxy_routing_enable\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.http.parent_proxy_routing_enable" value="0" <@checked proxy.config.http.parent_proxy_routing_enable\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables the parent caching option.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.no_dns_just_forward_to_parent>No DNS and Just Forward to Parent</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.http.no_dns_just_forward_to_parent" value="1" <@checked proxy.config.http.no_dns_just_forward_to_parent\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.http.no_dns_just_forward_to_parent" value="0" <@checked proxy.config.http.no_dns_just_forward_to_parent\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, and if parent caching is enabled,
            <@record proxy.config.product_name> does no DNS lookups on
	    request hostnames.
      </ul>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.http.parent_proxy.file>Parent Proxy Cache Rules
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.http.parent_proxy.file>" file lets
      you specify the parent proxy for specific objects or sets of
      objects.
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_parent_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_http_parent_proxy.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_parent_config.ink&fname=<@record proxy.config.http.parent_proxy.file>&frecord=proxy.config.http.parent_proxy.file', 'displayWin');">
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
