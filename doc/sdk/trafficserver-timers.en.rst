Timers applicable to various states in a transaction
-----------------------------------------------------

Traffic Server runs a variety of timers at various states of a transaction. Typically,
a given transaction may include upto two connections (one on the UA/client side and the
other on the Origin side). Traffic Server supports two kinds of timers "Active" and
"Inactive" timers for each side respectively, as applicable at a given state. The below
picture illustrates the specific timers run at various states in the current implementation.
The picture only depicts the HTTP transaction level timers and does not include the TLS handshake
states and other more detailed/internal timers in each individual state.

.. figure:: ../static/images/admin/transaction_states_timers.svg
      :align: center
   :alt: Active and Inactive Timers in various states

