<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /monitor/m_header.ink>

<@overview_details_object>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">

        <tr> 
          <td align="center" class="monitorLabelSmall">Node</td>
          <td align="center" class="monitorLabelSmall">On/Off</td>
          <td align="center" class="monitorLabelSmall">Objects Served</td>
          <td align="center" class="monitorLabelSmall">Ops/Sec</td>
          <td align="center" class="monitorLabelSmall">Hit Rate</td>
          <td align="center" class="monitorLabelSmall">Throughput (Mbit/sec)</td>
          <td align="center" class="monitorLabelSmall">HTTP Hit (ms)</td>
          <td align="center" class="monitorLabelSmall">HTTP Miss (ms)</td>
        </tr>
<@overview_object>
      </table>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="secondaryColor">
    <td class="secondaryColor" width="2" height="10"><img src="/images/dot_clear.gif" width="2" height="10"></td>
  </tr>
</table>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>
