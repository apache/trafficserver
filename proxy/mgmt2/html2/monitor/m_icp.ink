<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /monitor/m_header.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;ICP Peering Statistics</p>
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
          <td height="2" colspan="2" class="configureLabel">Queries Originating from this Node</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Query Requests</td>
          <td class="bodyText"><@record proxy.process.icp.icp_query_requests\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Query Messages Sent</td>
          <td class="bodyText"><@record proxy.process.icp.total_udp_send_queries\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Peer Hit Messages Received</td>
          <td class="bodyText"><@record proxy.process.icp.icp_query_hits\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Peer Miss Messages Received</td>
          <td class="bodyText"><@record proxy.process.icp.icp_query_misses\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Responses Received</td>
          <td class="bodyText"><@record proxy.process.icp.icp_remote_responses\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Average ICP Message Response Time (ms)</td>
          <td class="bodyText"><@record proxy.process.icp.total_icp_response_time></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Average ICP Request Time</td>
          <td class="bodyText"><@record proxy.process.icp.total_icp_request_time></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Queries Originating from ICP Peers</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Query Messages Received</td>
          <td class="bodyText"><@record proxy.process.icp.icp_remote_query_requests\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Remote Query Hits</td>
          <td class="bodyText"><@record proxy.process.icp.cache_lookup_success\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Remote Query Misses</td>
          <td class="bodyText"><@record proxy.process.icp.cache_lookup_fail\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Successful Response Messages Sent to Peers</td>
          <td class="bodyText"><@record proxy.process.icp.query_response_write\c></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>

