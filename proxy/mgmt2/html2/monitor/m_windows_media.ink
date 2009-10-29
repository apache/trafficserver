<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /monitor/m_header.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Windows Media Statistics</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td colspan="6" class="monitorLabel">Client</td>
        </tr>
	<tr>
          <td height="2" colspan="6" class="configureLabel">On Demand</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp</td>
	  <td align=center class="bodyText">MMS (TCP)</td>
	  <td align=center class="bodyText">MMS (UDP)</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;HTTP&nbsp;&nbsp;&nbsp;</td>
	  <td align=center class="bodyText">Multicast</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;Total&nbsp;&nbsp;</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Connections</td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_mmst_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_mmsu_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_http_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_mcast_ondemand\c></td>
          <td class="bodyText"><@record proxy.node.wmt.total_current_unique_streams_ondemand\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_mmst_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_mmsu_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_http_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_mcast_ondemand\c></td>
          <td class="bodyText"><@record proxy.node.wmt.total_unique_streams_ondemand\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Request Bytes</td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.request_bytes_mmst_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.request_bytes_mmsu_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.request_bytes_http_ondemand\b></td>
          <td class="bodyText">N/A</td>
          <td class="bodyText"><@record proxy.node.wmt.downstream.total_request_bytes_ondemand\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_mmst_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_mmsu_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_http_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_mcast_ondemand\b></td>
          <td class="bodyText"><@record proxy.node.wmt.downstream.total_response_bytes_ondemand\b></td>
        </tr>
	<tr>
          <td height="2" colspan="6" class="configureLabel">Live</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp</td>
	  <td align=center class="bodyText">MMS (TCP)</td>
	  <td align=center class="bodyText">MMS (UDP)</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;HTTP&nbsp;&nbsp;&nbsp;&nbsp;</td>
	  <td align=center class="bodyText">&nbsp;Multicast&nbsp;</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;Total&nbsp;&nbsp;&nbsp;</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Connections</td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_mmst_live\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_mmsu_live\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_http_live\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_unique_streams_mcast_live\c></td>
          <td class="bodyText"><@record proxy.node.wmt.total_current_unique_streams_live\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_mmst_live\c></td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_mmsu_live\c></td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_http_live\c></td>
          <td class="bodyText"><@record proxy.process.wmt.unique_streams_mcast_live\c></td>
          <td class="bodyText"><@record proxy.node.wmt.total_unique_streams_live\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Request Bytes</td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.request_bytes_mmst_live\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.request_bytes_mmsu_live\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.request_bytes_http_live\b></td>
          <td class="bodyText">N/A</td>
          <td class="bodyText"><@record proxy.node.wmt.downstream.total_request_bytes_live\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_mmst_live\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_mmsu_live\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_http_live\b></td>
          <td class="bodyText"><@record proxy.process.wmt.downstream.response_bytes_mcast_live\b></td>
          <td class="bodyText"><@record proxy.node.wmt.downstream.total_response_bytes_live\b></td>
        </tr>
      </table>
    </td>
  </tr>
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td colspan="4" class="monitorLabel">Server</td>
        </tr>

        <tr align="center"> 
          <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp</td>
	  <td align=center class="bodyText">MMS (TCP) On Demand</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;MMS (TCP) Live&nbsp;&nbsp;&nbsp;&nbsp;</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;Total&nbsp;&nbsp;&nbsp;&nbsp;</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Connections</td>
          <td class="bodyText"><@record proxy.process.wmt.current_server_connections_mmst_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.current_server_connections_mmst_live\c></td>
          <td class="bodyText"><@record proxy.node.wmt.current_server_connections_mmst\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.wmt.server_connections_mmst_ondemand\c></td>
          <td class="bodyText"><@record proxy.process.wmt.server_connections_mmst_live\c></td>
          <td class="bodyText"><@record proxy.node.wmt.server_connections_mmst\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Request Bytes</td>
          <td class="bodyText"><@record proxy.process.wmt.upstream.request_bytes_mmst_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.upstream.request_bytes_mmst_live\b></td>
          <td class="bodyText"><@record proxy.node.wmt.upstream.request_bytes_mmst\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText"><@record proxy.process.wmt.upstream.response_bytes_mmst_ondemand\b></td>
          <td class="bodyText"><@record proxy.process.wmt.upstream.response_bytes_mmst_live\b></td>
          <td class="bodyText"><@record proxy.node.wmt.upstream.response_bytes_mmst\b></td>
        </tr>
      </table>
    </td>
  </tr>
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td colspan="4" class="monitorLabel">Hit Rate</td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp</td>
	  <td align=center class="bodyText">Cacheable</td>
	  <td align=center class="bodyText">&nbsp;&nbsp;&nbsp;Total&nbsp;&nbsp;&nbsp;</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Cumulative</td>
          <td class="bodyText"><@record proxy.node.wmt.ondemand.byte_hit_ratio\p></td>
          <td class="bodyText"><@record proxy.node.wmt.total.byte_hit_ratio\p></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Instantaneous</td>
          <td class="bodyText"><@record proxy.node.wmt.ondemand.byte_hit_ratio_avg_10s\p></td>
          <td class="bodyText"><@record proxy.node.wmt.total.byte_hit_ratio_avg_10s\p></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>
