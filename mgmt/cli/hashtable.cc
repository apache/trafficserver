/** @file

  Stuff related to hashtable

  @section license License

  Copyright (c) 1997-1998 Sun Microsystems, Inc.

  TK 8.3 license:

  This software is copyrighted by the Regents of the University of
  California, Sun Microsystems, Inc., and other parties.  The following
  terms apply to all files associated with the software unless explicitly
  disclaimed in individual files.

  The authors hereby grant permission to use, copy, modify, distribute,
  and license this software and its documentation for any purpose, provided
  that existing copyright notices are retained in all copies and that this
  notice is included verbatim in any distributions. No written agreement,
  license, or royalty fee is required for any of the authorized uses.
  Modifications to this software may be copyrighted by their authors
  and need not follow the licensing terms described here, provided that
  the new terms are clearly indicated on the first page of each file where
  they apply.

  IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY
  FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
  ARISING OUT OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY
  DERIVATIVES THEREOF, EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

  THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.  THIS SOFTWARE
  IS PROVIDED ON AN "AS IS" BASIS, AND THE AUTHORS AND DISTRIBUTORS HAVE
  NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
  MODIFICATIONS.

  GOVERNMENT USE: If you are acquiring this software on behalf of the
  U.S. government, the Government shall have only "Restricted Rights"
  in the software and related documentation as defined in the Federal
  Acquisition Regulations (FARs) in Clause 52.227.19 (c) (2).  If you
  are acquiring the software on behalf of the Department of Defense, the
  software shall be classified as "Commercial Computer Software" and the
  Government shall have only "Restricted Rights" as defined in Clause
  252.227-7013 (c) (1) of DFARs.  Notwithstanding the foregoing, the
  authors grant the U.S. Government and others acting in its behalf
  permission to use and distribute the software in accordance with the
  terms specified in this license.

  @section details Details

  Parts of this code is based on code from TK, tkConfig.c. The license
  on this code allows us to use as long as we keep the license with
  the source.  Note that only parts of this code is from tkConfig.c,
  since it's been modified to work with our internals.

 */

#include <tcl.h>
#include <stdlib.h>
#include <string.h>
#include "definitions.h"
#include "createArgument.h"
#include "ink_string.h"
#include "ink_defs.h"


extern Tcl_Interp *interp;
Tcl_HashTable CommandHashtable;

#define OPTION_HASH_KEY "cliOptionTable"
static void DestroyOptionHashTable _ANSI_ARGS_((ClientData clientData, Tcl_Interp * interp));

int
cliCreateCommandHashtable()
{
  Tcl_HashTable *hashTablePtr;

  /*
   * We use an AssocData value in the interpreter to keep a hash
   * table of all the option tables we've created for this application.
   * This is used for two purposes.  First, it allows us to share the
   * tables (e.g. in several chains) and second, we use the deletion
   * callback for the AssocData to delete all the option tables when
   * the interpreter is deleted.  The code below finds the hash table
   * or creates a new one if it doesn't already exist.
   */
  hashTablePtr = (Tcl_HashTable *) Tcl_GetAssocData(interp, OPTION_HASH_KEY, NULL);
  if (hashTablePtr == NULL) {
    hashTablePtr = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(hashTablePtr, TCL_STRING_KEYS);
    Tcl_SetAssocData(interp, OPTION_HASH_KEY, DestroyOptionHashTable, (ClientData) hashTablePtr);
  }

  return TCL_OK;
}


int
cliAddCommandtoHashtable(const char *name, cli_ArgvInfo * argtable,
                         char **reqd_args, cli_parsedArgInfo * parsedArgTable, const char *helpString)
{
  Tcl_HashEntry *entryPtr;
  int newCmd;
  cli_CommandInfo *commandinfo;
  Tcl_HashTable *hashTablePtr;

  hashTablePtr = (Tcl_HashTable *) Tcl_GetAssocData(interp, OPTION_HASH_KEY, NULL);
  if (hashTablePtr == NULL) {
    Tcl_AppendResult(interp, "can not add command to hash table", (char *) NULL);
    return TCL_ERROR;
  }


  commandinfo = (cli_CommandInfo *) ckalloc(sizeof(cli_CommandInfo));
  commandinfo->command_name = name;
  commandinfo->argtable = argtable;
  commandinfo->reqd_args = reqd_args;
  commandinfo->parsedArgTable = parsedArgTable;
  if (helpString != NULL) {
    int helpString_size = sizeof(char) * (strlen(helpString) + 1);
    commandinfo->helpString = (char *) ckalloc(helpString_size);
    ink_strlcpy(commandinfo->helpString, helpString, helpString_size);
  }

  /*
   * See if a table has already been created for this template.  If
   * so, just reuse the existing table.
   */

  entryPtr = Tcl_CreateHashEntry(hashTablePtr, name, &newCmd);
  if (entryPtr == NULL) {
    Tcl_AppendResult(interp, "can not add command to hash table", (char *) NULL);
    return TCL_ERROR;
  }
  Tcl_SetHashValue(entryPtr, commandinfo);
  return TCL_OK;
}


cli_CommandInfo *
cliGetCommandArgsfromHashtable(char *name)
{
  Tcl_HashEntry *entryPtr;
  cli_CommandInfo *commandinfo;
  Tcl_HashTable *hashTablePtr;

  hashTablePtr = (Tcl_HashTable *) Tcl_GetAssocData(interp, OPTION_HASH_KEY, NULL);
  if (hashTablePtr == NULL) {
    Tcl_AppendResult(interp, "can not add command to hash table", (char *) NULL);
    return NULL;
  }

  entryPtr = Tcl_FindHashEntry(hashTablePtr, name);
  if (entryPtr == NULL) {
    Tcl_AppendResult(interp, "no command named \"", name, "\"", (char *) NULL);
    return NULL;
  }

  commandinfo = (cli_CommandInfo *) Tcl_GetHashValue(entryPtr);
  if (commandinfo == (cli_CommandInfo *) NULL) {
    Tcl_AppendResult(interp, "no command named \"", name, "\"", (char *) NULL);
    return NULL;
  }

  return (commandinfo);

}


static void
DestroyOptionHashTable(ClientData clientData, Tcl_Interp * /* interp ATS_UNUSED */)
    /* The hash table we are destroying */
    /* The interpreter we are destroying */
{
  Tcl_HashTable *hashTablePtr = (Tcl_HashTable *) clientData;
  Tcl_HashSearch search;
  Tcl_HashEntry *hashEntryPtr;
  cli_CommandInfo *commandinfo;
  cli_ArgvInfo *infoPtr;

  for (hashEntryPtr = Tcl_FirstHashEntry(hashTablePtr, &search);
       hashEntryPtr != NULL; hashEntryPtr = Tcl_NextHashEntry(&search)) {

    commandinfo = (cli_CommandInfo *) Tcl_GetHashValue(hashEntryPtr);
    if (commandinfo) {
      if (commandinfo->reqd_args) {
        ckfree((char *) commandinfo->reqd_args);
      }

      if (commandinfo->argtable) {
        for (infoPtr = commandinfo->argtable; infoPtr->key != NULL; infoPtr++) {
          if (infoPtr->key) {
            ckfree((char *) infoPtr->key);
          }
          if (infoPtr->help) {
            ckfree((char *) infoPtr->help);
          }
          if (infoPtr->def) {
            ckfree((char *) infoPtr->def);
          }
        }
        ckfree((char *) commandinfo->argtable);
      }

      if (commandinfo->command_name) {
        ckfree((char *) commandinfo->command_name);
      }
      if (commandinfo->helpString) {
        ckfree((char *) commandinfo->helpString);
      }
      ckfree((char *) commandinfo);
    }
    hashEntryPtr = Tcl_NextHashEntry(&search);
  }
  Tcl_DeleteHashTable(hashTablePtr);
  ckfree((char *) hashTablePtr);
}
