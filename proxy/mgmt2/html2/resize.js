/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

function resizeIt() {
  if (saveInnerWidth < window.innerWidth || 
      saveInnerWidth > window.innerWidth || 
      saveInnerHeight > window.innerHeight || 
      saveInnerHeight < window.innerHeight ) 
    {
      window.history.go(0);
    }
}

if ((navigator.appName == "Netscape") &&
    (parseInt(navigator.appVersion) >= 4) &&
    (parseInt(navigator.appVersion) < 5)) {
  if(!window.saveInnerWidth) {
    window.onresize = resizeIt;
    window.saveInnerWidth = window.innerWidth;
    window.saveInnerHeight = window.innerHeight;
  }
}
