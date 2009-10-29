<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /monitor/m_header.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;FTP Statistics</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" width="66%">Attribute</td>
          <td class="monitorLabel" width="33%">Current Value</td>
        </tr>


	<tr>
          <td height="2" colspan="2" class="configureLabel">Client</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Open Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.current_client_connections\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Bytes Read</td>
          <td class="bodyText"><@record proxy.process.ftp.downstream.request_bytes\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Bytes Written</td>
          <td class="bodyText"><@record proxy.process.ftp.downstream.response_bytes\b></td>
        </tr>

	<tr>
          <td height="2" colspan="2" class="configureLabel">Server</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Open Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.current_server_connections\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Bytes Read</td>
          <td class="bodyText"><@record proxy.process.ftp.upstream.response_bytes\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Bytes Written</td>
          <td class="bodyText"><@record proxy.process.ftp.upstream.request_bytes\b></td>
        </tr>

	<tr>
          <td height="2" colspan="2" class="configureLabel">Operations</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;File Hits</td>
          <td class="bodyText"><@record proxy.process.ftp.cache_hit_fresh\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;File Misses</td>
          <td class="bodyText"><@record proxy.process.ftp.cache_miss_cold\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Change Directory Hits</td>
          <td class="bodyText"><@record proxy.process.ftp.cwd_hits\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Change Directory Misses</td>
          <td class="bodyText"><@record proxy.process.ftp.cwd_misses\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;List Directory Hits</td>
          <td class="bodyText"><@record proxy.process.ftp.directory_hits\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;List Directory Misses</td>
          <td class="bodyText"><@record proxy.process.ftp.directory_misses\c></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>

