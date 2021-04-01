/*************************************************************************
 *
 *  This file is part of the ACT library
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
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <act/act.h>
#include <act/tech.h>
#include <act/passes.h>
#include <common/config.h>
#include <lisp.h>
#include <lispCli.h>

#include "all_cmds.h"

static void signal_handler (int sig)
{
  LispInterruptExecution = 1;
}

static void clr_interrupt (void)
{
  LispInterruptExecution = 0;
}

static void usage (char *name)
{
  fprintf (stderr, "Usage: %s <act-arguments> [...]\n", name);
  exit (1);
}

static int cmd_argc;
static char **cmd_argv;

int process_getargnum (int argc, char **argv)
{
  if (argc != 1) {
    fprintf (stderr, "Usage: %s\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, NULL);
  
  if (Act::cmdline_args) {
    LispSetReturnInt (cmd_argc + list_length (Act::cmdline_args)/2);
  }
  else {
    LispSetReturnInt (cmd_argc);
  }
  return 2;
}


int process_getarg (int argc, char **argv)
{
  int val;
  int l;

  if (argc != 2) {
    fprintf (stderr, "Usage: %s <num>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "i");
  
  if (Act::cmdline_args) {
    l = list_length (Act::cmdline_args)/2;
  }
  else {
    l = 0;
  }
  val = atoi (argv[1]);
  if (val < 0 || val >= l + cmd_argc) {
    fprintf (stderr, "%s: argument (%d) out of range\n", argv[0], val);
    return 0;
  }
  if (val == 0) {
    LispSetReturnString (cmd_argv[0]);
  }
  else {
    if (val <= l) {
      char buf[1024];
      listitem_t *li;
      for (li = list_first (Act::cmdline_args); li; li = list_next (li)) {
	val--;
	if (val == 0) {
	  break;
	}
	li = list_next (li);
      }
      snprintf (buf, 1024, "-%c", (char)list_ivalue (li));
      li = list_next (li);
      Assert (li, "What?");
      if (list_value (li)) {
	snprintf (buf+2, 1022, "%s", (char *)list_value (li));
      }
      LispSetReturnString (buf);
    }
    else {
      val = val - l;
      LispSetReturnString (cmd_argv[val]);
    }
  }
  return 3;
}

int process_echo (int argc, char **argv)
{
  int nl = 1;
  if (argc > 1 && strcmp (argv[1], "-n") == 0) {
      nl = 0;
  }
  for (int i=2-nl; i < argc; i++) {
    printf ("%s", argv[i]);
    if (i != argc-1) {
      printf (" ");
    }
  }
  if (nl) {
    printf ("\n");
  }
  save_to_log (argc, argv, "s*");
  return 1;
}

int process_prompt (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <str>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "s");
  
  LispCliSetPrompt (argv[1]);
  return 1;
}

int process_getopt (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <string>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "s");

  Act::setOptionString (argv[1]);

  if (!Act::getOptions (&cmd_argc, &cmd_argv)) {
    fprintf (stderr, "Unknown command-line option specified.");
    return 0;
  }
  return 1;
}

int process_getenv (int argc, char **argv)
{
  char *s;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <string>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "s");
  
  s = getenv (argv[1]);
  if (!s) {
    char t[1];
    t[0] = '\0';
    LispSetReturnString (t);
  }
  else {
    LispSetReturnString (s);
  }
  return 3;
}

int process_putenv (int argc, char **argv)
{
  char *s;

  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <value>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "ss");
  
  if (setenv (argv[1], argv[2], 1) == -1) {
    fprintf (stderr, "%s failed", argv[0]);
    return 0;
  }
  return 1;
}

struct LispCliCommand Cmds[] = {
  { NULL, "Basic I/O", NULL },
  { "getopt", "getopt <string> - run getopt", process_getopt },
  { "getargc", "getargc - return number of arguments", process_getargnum },
  { "getargv", "getargv # - return command-line argument", process_getarg },
  { "getenv", "getenv <string> - return environment variable", process_getenv },
  { "putenv", "putenv <name> <value> - set environment variable", process_putenv },
  { "echo", "echo [-n] args - display to screen", process_echo },
  { "prompt", "prompt <str> - change prompt to the specified string", process_prompt }
};

int main (int argc, char **argv)
{
  FILE *fp;

  /* initialize ACT library */
  Act::Init (&argc, &argv);

  signal (SIGINT, signal_handler);

  LispInit ();
  
  if (argc == 1) {
    fp = stdin;
  }
  else if (argc > 1) {
    fp = fopen (argv[1], "r");
    if (!fp) {
      fatal_error ("Could not open file `%s' for reading", argv[1]);
    }
    for (int i=1; i < argc; i++) {
      argv[i-1] = argv[i];
    }
    argc--;

    /*-- parse any options for the script --*/
    if (!Act::getOptions (&argc, &argv)) {
      fatal_error ("Unknown command-line option specified.");
    }
  }
  else {
    fprintf (stderr, "Usage: %s [<act-options>] [<script>] [script options]\n", argv[0]);
    fatal_error ("Illegal arguments");
  }

  if (fp == stdin) {
    LispCliInit (NULL, ".act_history", "interact> ", Cmds,
		 sizeof (Cmds)/sizeof (Cmds[0]));
  }
  else {
    LispCliInitPlain ("interact> ", Cmds, sizeof (Cmds)/sizeof (Cmds[0]));
  }

  conf_cmds_init ();
  act_cmds_init ();
  ckt_cmds_init ();
  pandr_cmds_init ();
  misc_cmds_init ();

  cmd_argc = argc;
  cmd_argv = argv;
  
  while (!LispCliRun (fp)) {
    if (LispInterruptExecution) {
      fprintf (stderr, "*** interrupted\n");
      if (fp != stdin) {
	fclose (fp);
	return 0;
      }
    }
    clr_interrupt ();
  }

  LispCliEnd ();
  
  return 0;
}
