#pragma once

#include "ts/ts.h"

int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  );
