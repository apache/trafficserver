//-------------------------------------------------------------------------
// tree_footer.ink
//
//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//-------------------------------------------------------------------------

<!-- start -->
var active_menu_id = -1
var active_item_id = -1

var menu_split;
var url_split;

var menu_id;
var item_id;

var minus_img = new Image();
minus_img.src = "/images/minusIcon.gif";

var plus_img = new Image();
plus_img.src = "/images/plusIcon.gif";

var blank_img = new Image();
blank_img.src = "/images/blankIcon.gif";

var plus_minus_img = new Array();
//if(document.all || document.layers)
if((is_ie4up || is_nav4up) && (is_mac || is_win) && (!is_nav6up && !is_gecko))
{
  for(i=0; i<menu_block.length; i++)
    plus_minus_img[i] = plus_img.src;
}
else
{
  for(i=0; i<menu_block.length; i++)
    plus_minus_img[i] = blank_img.src;
}

var active_submenus = new Array();

for(i=0; i<menu_block.length; i++)
  active_submenus[i]=-1;

function openandcloseInternal(param)
{
  var param_split;
  param_split = param.split(":")

  if (param_split[0]) 
  {
    menu_id = parseInt(param_split[0]);
    if (isNaN(menu_id) == true) 
      menu_id = 0
  } 
  else 
  {
    menu_id = 0
  }
  if (param_split[1]) 
  {
    item_id = parseInt(param_split[1])
    if (isNaN(item_id) == true)
      item_id = 0
  } 
  else 
  {
    item_id = 0
  }
	
  // if this the first call, set active items
  if (active_menu_id == -1 || active_item_id == -1) 
  {	
    active_menu_id = menu_id
    active_item_id = item_id	
  }
  active_submenus[menu_id] = (-1) * active_submenus[menu_id]  	

  if((is_ie4up || is_nav4up) && (is_mac || is_win) && (!is_nav6up && !is_gecko))
  {
    if (active_submenus[menu_id] == 1) 
    {
      plus_minus_img[menu_id] = minus_img.src;
    } 
    else 
    {
      plus_minus_img[menu_id] = plus_img.src;
    } 
    output();    
    if(is_ie4up)
      ieOutput();
    else 
      nsMozillaOutput();
  }

}
  	
function output()	
{
  menu_content = "";
  menu_content += "<table border=0 cellpadding=0 cellspacing=0 bordercolor=333366 width=190>";

  for(i=0; i<menu_block.length; i++)
  {
    menu_split = menu_block[i].split(";");
    url_split = menu_split[0].split("|");

    if(!menu_split[1]) //no submenu for particlar item
    {		
      if (i == active_menu_id) 
      {
	menu_content += "<tr><td class='hilightColor' align=right width=10>";
	menu_content += "<img src='/images/blankIcon.gif' width='10' height='10' HSPACE='5' VSPACE='5'></td>";
	menu_content += "<td class='hilightColor' width=190 height='20'>";
	menu_content += "<span class='blackItem'>" + url_split[0] + "</span>";
	menu_content += "</td></tr><tr><td><img src='/images/dot_clear.gif' width='1' height='1'></td></tr>";
      }
      else
      {
        menu_content += "<tr><td bgcolor=000000 align=left width=10>";
        menu_content += "<a href=" + url_split[1] + " target='' class='greyLinks'>";
	menu_content += "<img border='0' src='/images/blankIcon.gif' width='10' height='10' HSPACE='5'></a></td>";
        menu_content += "<td bgcolor=000000 width=190 height='20'>";
        menu_content += "<a href=" + url_split[1] + " target='' class='greyLinks'>";
	menu_content += url_split[0];
	menu_content += "</a></td></tr><tr><td colspan='2'><img src='/images/dot_clear.gif' width='1' height='1'></td></tr>";
      }
    } //if
    else 
    { //submenus present
      //if(document.all || document.layers)
      if((is_ie4up || is_nav4up) && (is_mac || is_win) && (!is_nav6up && !is_gecko))
      {
	menu_content += "<tr><td bgcolor=000000 align=right width=10>"
	menu_content += "<a href='javascript:openandcloseInternal(\"" + i + ":\")'>"
     	menu_content += "<img src='" + plus_minus_img[i] + "' border=0 HSPACE='5'></a></td>"
	menu_content += "<td bgcolor=000000 width=190 height='20'>"
	menu_content += "<font size='1'>"
      	menu_content += "<a href='javascript:openandcloseInternal(\"" + i + ":\")' class='greyLinks'>" 
      	menu_content += url_split[0]
     	menu_content += "</a></td></tr><tr><td><img src='/images/dot_clear.gif' width='1' height='1'></td></tr>"
   
	if (active_submenus[i] == 1) 
	{
	  for (ii=1; ii<menu_split.length; ii++) 
	  {
	    url_split=menu_split[ii].split("|")
	    menu_content += "<tr><td class='secondaryColor'>&nbsp;</td>"
	    
	    if (active_menu_id == i && active_item_id == ii-1) 
	    {
	      menu_content += "<td class='hilightColor' height='20'>"
	      menu_content += "<img border='0' src='/images/blankIcon.gif' width='10' height='10' HSPACE='5'>"
	      menu_content += "<span class='blackItem'>" + url_split[0] + "</span>"
	    } 
	    else 
	    {
	      menu_content += "<td class='unhilightColor' height='20'>"
	      menu_content += "<a href=" + url_split[1] + " target='' class='greyLinks'>"
	      menu_content += "<img border='0' src='/images/blankIcon.gif' width='10' height='10' HSPACE='5'>"
	      menu_content += url_split[0]
	      menu_content += "</a>"
	    }
	    menu_content += "</td></tr><tr><td><img src='/images/dot_clear.gif' width='1' height='1'></td></tr>"
	  }
      	}    	
      }//if		
      else
      {	
	menu_content += "<tr><td bgcolor=000000 align=right width=10>";
    	//menu_content += "<a href=''>";
    	menu_content += "<img src='" + plus_minus_img[i] + "' border=0 HSPACE='5'><!--</a>--></td>";
	menu_content += "<td bgcolor=000000 width=190 height='20'>";
    	menu_content += "<font size='1'>";
	menu_content += "<span class='greyLinks'>"; //kwt
	//menu_content += "<a href='' class='greyLinks'>";
    	menu_content += url_split[0];
	menu_content += "</span>";
	menu_content += "<!--</a>--></td></tr><tr><td><img src='/images/dot_clear.gif' width='1' height='1'></td></tr>";
			
	for (ii=1; ii<menu_split.length; ii++)
	{
	  url_split=menu_split[ii].split("|")
	  menu_content += "<tr><td class='secondaryColor'>&nbsp;</td>"
          if (active_menu_id == i && active_item_id == ii-1) 
	  {
	    menu_content += "<td class='hilightColor' height='20'>"
	    menu_content += "<img border='0' src='/images/blankIcon.gif' width='10' height='10' HSPACE='5'>"
	    menu_content += "<span class='blackItem'>" + url_split[0] + "</span>"
	  } 
	  else 
	  {
	    menu_content += "<td class='unhilightColor' height='20'>"
	    menu_content += "<a href=" + url_split[1] + " target='' class='greyLinks'>"
	    menu_content += "<img border='0' src='/images/blankIcon.gif' width='10' height='10' HSPACE='5'>"
	    menu_content += url_split[0]
	    menu_content += "</a>"
	  }
	  menu_content += "</td></tr><tr><td><img src='/images/dot_clear.gif' width='1' height='1'></td></tr>"
	
        }
      }
    }  // big else
  }  //for
  
  menu_content += "</table>";

}  //function output()

function DivOutBegin()
{
  document.write("<DIV id=menu style='LEFT: 5px; POSITION: absolute; TOP: 95px'>");
}

function DivOutEnd()
{
  document.write("</DIV>");
}

function ieOutput()
{
  menu.innerHTML=menu_content;
}

function nsMozillaOutput()
{
  document.layers["menu"].document.write(menu_content)
  document.layers["menu"].document.close()
}

if(is_nav6up && is_gecko)
{
  renderExpandedMenu();
}

function renderExpandedMenu()
{
  openandcloseInternal('<@query menu>:<@query item>');
  output();
  if(is_nav6up && is_gecko)
  {
    document.write("<DIV id='menu' style='LEFT: 5px; POSITION: absolute; TOP: 95px'>");
    document.write(menu_content);
    document.write("</DIV>");
    document.close();
  }
  else if(is_nav4up)
  {
    nsMozillaOutput();
  }
}	

function openandclose(param)
{
  if((is_ie4up || is_nav4up) && (is_mac || is_win) && (!is_nav6up && !is_gecko))
    openandcloseInternal(param);
  else if((!is_win && !is_mac) && (is_nav4up) && (!is_nav6up && !is_gecko))
    renderExpandedMenu();
}

<!-- end -->
</SCRIPT>
<div id="menu" style="position:absolute;top:95px;left:5px"></div>
