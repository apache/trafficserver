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
      <p>&nbsp;&nbsp;DNS Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 


  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.dns.splitDNS.enabled>Split DNS</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.dns.splitDNS.enabled" value="1" <@checked proxy.config.dns.splitDNS.enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.dns.splitDNS.enabled" value="0" <@checked proxy.config.dns.splitDNS.enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables server selection.
        <li>When enabled, <@record proxy.config.product_name> refers
            to the splitdns.config file for the selection specification.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.dns.splitdns.def_domain>Default Domain</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.dns.splitdns.def_domain" value="<@record proxy.config.dns.splitdns.def_domain>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the default domain for split DNS requests.
        <li>This value is appended automatically to the hostname if it does
            not include a domain before split DNS determines which DNS server
            to use.
      </ul>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.dns.splitdns.filename>DNS Servers Specification
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.dns.splitdns.filename>" file enables
      you to specify the DNS server that
      <@record proxy.config.product_name> should use for resolving
      hosts under specific conditions.
    </td>
  </tr>
  <tr>
    <td>
     <@config_table_object /configure/f_split_dns_config.ink>
    </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_dns_split.ink?<@link_query>'">
      <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_split_dns_config.ink&fname=<@record proxy.config.dns.splitdns.filename>&frecord=proxy.config.dns.splitdns.filename', 'displayWin');">
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
