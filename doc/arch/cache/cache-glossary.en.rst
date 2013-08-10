Glossary
~~~~~~~~

.. include:: common.defs

.. glossary::
    :sorted:

    cache volume
        Persistent storage for the cache, defined and manipulable by the user. Cache volumes are defined in :file:`volume.config`. A cache volume is spread across :term:`storage unit`\ s to increase performance through parallel I/O. Storage units can be split across cache volumes. Each such part of a storage unit in a cache volume is a :term:`volume`.

        Implemented by the class :cpp:class:`CacheVol`.

    volume
        A homogenous persistent store for the cache. A volume always resides entirely on a single physical device and is treated as an undifferentiated span of bytes.

        Implemented by the class :cpp:class:`Vol`.

        See also :term:`storage unit`, :term:`cache volume`

    storage unit
        The physical storage described by a single line in :file:`storage.config`.
