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
#include <common/config.h>
#include <string.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"
#include <common/array.h>

static int process_file_open (int argc, char **argv)
{
  int i;
  FILE *fp;
  
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <r|w|a>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "s*");
  
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
  i = ptr_register ("file", fp);
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
  save_to_log (argc, argv, "i");
  
  v = atoi (argv[1]);

  void *fv = ptr_get ("file", v);
  if (!fv) {
    fprintf (stderr, "%s: file <#%d> does not exist.\n", argv[0], v);
    return 0;
  }

  fclose ((FILE *)fv);

  if (ptr_unregister ("file", v) != 0) {
    fprintf (stderr, "%s: file <#%d> internal error.\n", argv[0], v);
    return 0;
  }

  return 1;
}

FILE *sys_get_fileptr (int v)
{
  return (FILE *) ptr_get ("file", v);
}

static FILE *_logfile = NULL;

int process_log_file (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <file>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "s");

  if (_logfile) {
    fclose (_logfile);
  }
  _logfile = fopen (argv[1], "w");
  if (!_logfile) {
    fprintf (stderr, "%s: could not open file `%s' for writing\n", argv[0],
	     argv[1]);
    return 0;
  }
  return 1;
}

int process_end_log (int argc, char **argv)
{
  if (argc != 1) {
    fprintf (stderr, "Usage: %s\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, NULL);
  if (_logfile) {
    fclose (_logfile);
    _logfile = NULL;
  }
  else {
    fprintf (stderr, "%s: no log file detected\n", argv[0]);
    return 0;
  }
  return 1;
}

static struct LispCliCommand conf_cmds[] = {
  { NULL, "Misc support functions (use `sys:' prefix)", NULL },
  { "open", "open <name> <r|w|a> - open file, return handle", process_file_open },
  { "close", "close <handle> - close file", process_file_close },
  { "log", "logfile <name> - open log file", process_log_file },
  { "endlog", "endlog - close open file", process_end_log }
};

void misc_cmds_init (void)
{
  LispCliAddCommands ("sys", conf_cmds, sizeof (conf_cmds)/sizeof (conf_cmds[0]));
}

void save_to_log (int argc, char **argv, const char *fmt)
{
  int j = 0;

  if (!_logfile) {
    return;
  }

  fprintf (_logfile, "%s", argv[0]);

  for (int i=1; i < argc; i++) {
    int ch;
    if (fmt[j] == '*') {
      ch = fmt[j-1];
    }
    else {
      ch = fmt[j];
      j++;
    }
    if (ch == 's') {
      fputc (' ', _logfile);
      fputc ('"', _logfile);
      for (int j=0; argv[i][j]; j++) {
	if (argv[i][j] == '"' || argv[i][j] == '\\') {
	  fputc ('\\', _logfile);
	}
	fputc (argv[i][j], _logfile);
      }
      fputc ('"', _logfile);
    }
    else {
      fprintf (_logfile, " %s", argv[i]);
    }
  }
  fprintf (_logfile, "\n");
  fflush (_logfile);
}
