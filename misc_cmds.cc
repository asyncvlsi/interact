/*************************************************************************
 *
 *  Copyright (c) 2021 Rajit Manohar
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *
 **************************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <config.h>
#include <string.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "array.h"

#define MAX_FILES 32
L_A_DECL (FILE *, _flist);

static int process_file_open (int argc, char **argv)
{
  int i;
  FILE *fp;
  
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <r|w|a>\n", argv[0]);
    return 0;
  }
  if (strcmp (argv[2], "r") != 0 &&
      strcmp (argv[2], "w") != 0 &&
      strcmp (argv[2], "a") != 0) {
    fprintf (stderr, "Usage: %s <name> <r|w|a>\n", argv[0]);
    return 0;
  }

  fp = fopen (argv[1], argv[2]);
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' (mode: %s)\n", argv[0],
	     argv[1], argv[2]);
    return 0;
  }

  for (i=0; i < A_LEN (_flist); i++) {
    if (!_flist[i]) {
      break;
    }
  }
  
  if (i == A_LEN (_flist)) {
    if (A_LEN (_flist) == MAX_FILES) {
      fclose (fp);
      fprintf (stderr, "%s: too many open files (max=%d).\n", argv[0], MAX_FILES);
      return 0;
    }
    A_NEW (_flist, FILE *);
    A_INC (_flist);
  }
  _flist[i] = fp;
  LispSetReturnInt (i);
  return 2;
}

static int process_file_close (int argc, char **argv)
{
  int v, x;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <id>\n", argv[0]);
    return 0;
  }
  v = atoi (argv[1]);
  if (v < 0 || v >= A_LEN (_flist)) {
    fprintf (stderr, "%s: file <#%d> does not exist.\n", argv[0], v);
    return 0;
  }
  if (!_flist[v]) {
    fprintf (stderr, "%s: file <#%d> not open.\n", argv[0], v);
    return 0;
  }
  fclose (_flist[v]);
  _flist[v] = NULL;
  return 1;
}

FILE *sys_get_fileptr (int v)
{
  if (v < 0 || v >= A_LEN (_flist)) {
    return NULL;
  }
  return _flist[v];
}

static struct LispCliCommand conf_cmds[] = {
  { NULL, "Misc support functions (use `sys:' prefix)", NULL },
  { "open", "open <name> <r|w|a> - open file, return handle", process_file_open },
  { "close", "close <handle> - close file", process_file_close }
};

void misc_cmds_init (void)
{
  LispCliAddCommands ("sys", conf_cmds, sizeof (conf_cmds)/sizeof (conf_cmds[0]));
}
