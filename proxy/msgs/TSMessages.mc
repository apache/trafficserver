;//
;#ifndef _TSMESSAGES_H_
;#define _TSMESSAGES_H_
;//
MessageIdTypedef=DWORD
;//
;//     Traffic Server messages
;//
;#define CAT_TS 1
MessageId=0x1
Language=English
TrafficServer
.
;//
;//     Traffic Server messages
;//
MessageId=10001
Severity=Informational
Facility=Application
SymbolicName=TRAFFIC_SERVER
Language=English
TRAFFIC SERVER %1
.
;//
;//     Perfutil messages
;//
MessageId=1900
Severity=Informational
Facility=Application
SymbolicName=UTIL_LOG_OPEN
Language=English
An extensible counter has opened the Event Log for MEMCTRS.DLL
.
;//
MessageId=1999
Severity=Informational
Facility=Application
SymbolicName=UTIL_CLOSING_LOG
Language=English
An extensible counter has closed the Event Log for MEMCTRS.DLL
.
;//
MessageId=2000
Severity=Error
Facility=Application
SymbolicName=APPMEM_OPEN_FILE_MAPPING_ERROR
Language=English
Unable to open mapped file containing Application Memory performance data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=APPMEM_UNABLE_MAP_VIEW_OF_FILE
Language=English
Unable to map to shared memory file containing Application Memory performance data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=APPMEM_UNABLE_OPEN_DRIVER_KEY
Language=English
Unable open "Performance" key of Applicationa Memory Performace Driver in registy. Status code is returned in data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=APPMEM_UNABLE_READ_FIRST_COUNTER
Language=English
Unable to read the "First Counter" value under the AppMem\Performance Key. Status codes retuened in data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=APPMEM_UNABLE_READ_FIRST_HELP
Language=English
Unable to read the "First Help" value under the AppMem\Performance Key. Status codes retuened in data.
.
;//
;#endif // _TSMESSAGES_H_
