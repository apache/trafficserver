#pragma once

#include "ts/ts.h"

#include "Channel.h"

struct Stage // upstream or downstream
{
  explicit
  Stage
    ( TSVConn vci
    )
    : vc(vci)
  { }

  TSVConn vc;
  Channel readio; // the vconn's reader
  Channel writeio; // the vconn's writer
};

