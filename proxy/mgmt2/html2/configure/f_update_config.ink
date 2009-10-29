/////////////////////////////////////////////////////////////////////
// update.config form and rule contains following:
//    URL                   url
//    Request Headers       headers
//    Offset Hour           offset_hr
//    Interval              interval
//    Recursion depth       rec_depth
// ** NOTE: the input type names must match those created in 
//          writeUpdateConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(url, headers, offset_hr, interval, rec_depth) { 
 this.url = url;
 this.headers = headers;
 this.offset_hr = offset_hr;
 this.interval = interval;
 this.rec_depth = rec_depth;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var url = document.form1.url.value;
  var headers = document.form1.headers.value;
  var offset_hr = document.form1.offset_hr.value;
  var interval = document.form1.interval.value;
  var rec_depth = document.form1.rec_depth.value;
 
  var rule = new Rule(url, headers, offset_hr, interval, rec_depth);

  return rule;
}

//
// This function displays the Rule object's information in the format
// that's used in the select list.
//
function textFormat(rule, write)
{
  var text = "";
  var delim = "";
  var eq = "=";  
  var space = "";
  if (write == 1) {
     space = "&nbsp;&nbsp;";
  } else {  
     space = "  ";
  }
  delim = space + "," + space;

  text = "URL" + eq + rule.url;
   
  if (rule.headers != "") text += delim + "Headers" + eq + rule.headers; 
  text += delim + "Offset Hour" + eq + rule.offset_hr + delim + "Interval" + eq + rule.interval;
  if (rule.rec_depth != "") text += delim + "Recursion Depth" + eq + rule.rec_depth;
  
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^"; 

  var text = rule.url + delim + rule.headers + delim + rule.offset_hr + delim + rule.interval + delim + rule.rec_depth + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  ruleList[index].url = document.form1.url.value;
  ruleList[index].headers = document.form1.headers.value;
  ruleList[index].offset_hr = document.form1.offset_hr.value;
  ruleList[index].interval = document.form1.interval.value;
  ruleList[index].rec_depth = document.form1.rec_depth.value;
}

// 
// This function updates the elements on the form displayed to the 
// user with the values sotred in the ruleList; has an optional index arg
// 
function updateForm(index)
{ 
  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;
 
  document.form1.url.value = ruleList[index].url;
  document.form1.headers.value = ruleList[index].headers;
  document.form1.offset_hr.value = ruleList[index].offset_hr;
  document.form1.interval.value = ruleList[index].interval;
  document.form1.rec_depth.value = ruleList[index].rec_depth;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.url.value = "";
  document.form1.headers.value = "";
  document.form1.offset_hr.value = "";
  document.form1.interval.value = "";
  document.form1.rec_depth.value = "";

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  if (document.form1.url.value == "" || 
      document.form1.offset_hr.value == "" ||
      document.form1.interval.value == ""){
    alert("Need to specify 'URL', 'Offset Hour', 'Interval'.");
    return false;
  }
	
  if (document.form1.offset_hr.value < 0 || document.form1.offset_hr.value > 23) {
	alert("'Offset Hour' must be between 0 - 23"); 
	return false;
  } 
  return true;   	
}
