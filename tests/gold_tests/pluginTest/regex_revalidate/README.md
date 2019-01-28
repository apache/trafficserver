# Documentation for regex_revalidate AuTest

## regex_revalidate.test.py

The basic regex_revalidation plugin verifies basic functionality (aka
happy path).

In this case items are placed into cache, regex_revalidate rules are
loaded and some basic functionality is tested.

It is important for the regex_revalidate rules to not reset every
time there is a `traffic_ctl reload config` so that is tested as well.

The regex_revalidate plugin allows rule expirations to be
shortened or even removed by changing the expiration date.

## regex_revalidate_remap.test.py

The regex_revalidation remap test verifies per remap rule functionality.

Two sets of remap rules are evaluated to ensure non overlap.

Also tested is to remove regex_revalidate plugin from a remap rule
to ensure that it's gone.

It is important for the regex_revalidate rule's epoch to not reset every
time there is a `traffic_ctl reload config` so that is tested as well.
