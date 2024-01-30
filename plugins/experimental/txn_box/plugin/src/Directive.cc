/** @file
 * Base directive implementation.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <swoc/Errata.h>

#include "txn_box/Directive.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::Errata;
using swoc::Rv;
using swoc::TextView;

/* ------------------------------------------------------------------------------------ */
DirectiveList &
DirectiveList::push_back(Directive::Handle &&d)
{
  _directives.emplace_back(std::move(d));
  return *this;
}

Errata
DirectiveList::invoke(Context &ctx)
{
  Errata zret;
  for (auto const &drtv : _directives) {
    zret.note(drtv->invoke(ctx));
    if (ctx.is_terminal()) {
      break;
    }
  }
  return zret;
}

// Do nothing.
swoc::Errata
NilDirective::invoke(Context &)
{
  return {};
}
/* ------------------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------------------ */
