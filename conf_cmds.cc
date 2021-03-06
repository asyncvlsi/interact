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
#include <lispCli.h>
#include "all_cmds.h"

static const char *conf_name (int t)
{
  switch (t) {
  case -1:
    return "missing";
    break;
  case 0:
    return "int";
    break;
  case 1:
    return "string";
    break;
  case 2:
    return "real";
    break;
  case 3:
    return "int_table";
    break;
  case 4:
    return "string_table";
    break;
  case 5:
    return "real_table";
    break;
  default:
    return "-error-";
    break;
  }
}

static int process_conf_read (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <file>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  char *tmp = config_file_name (argv[1]);
  if (!tmp) {
    fprintf (stderr, "%s: Could not find configuration file `%s'",
	     argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (tmp, "r");
  FREE (tmp);
  if (!fp) {
    fprintf (stderr, "%s: Could not find configuration file `%s'", argv[0],
	     argv[1]);
    return LISP_RET_ERROR;
  }
  config_read (argv[1]);
  return LISP_RET_TRUE;
}

static int process_conf_save (int argc, char **argv)
{
  FILE *fp;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <file>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  fp = fopen (argv[1], "w");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s'", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  config_dump (fp);
  fclose (fp);
  return LISP_RET_TRUE;
}

static int process_conf_gettype (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <name>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  LispSetReturnInt (config_gettype (argv[1]));
  return LISP_RET_INT;
}

static int process_conf_set_int (int argc, char **argv)
{
  int v, x;
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <val>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "si");
  
  v = atoi (argv[2]);
  x = config_gettype (argv[1]);
  if (x == -1 || x == 0) {
    config_set_int (argv[1], v);
    return LISP_RET_TRUE;
  }
  else {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
}

static int process_conf_get_int (int argc, char **argv)
{
  int v, x;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <name>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");
  
  x = config_gettype (argv[1]);
  if (x == -1) {
    fprintf (stderr, "%s: `%s' does not exist\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  if (x != 0) {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
  v = config_get_int (argv[1]);
  LispSetReturnInt (v);
  return LISP_RET_INT;
}

static int process_conf_set_string (int argc, char **argv)
{
  int x;
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <val>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");
  
  x = config_gettype (argv[1]);
  if (x == -1 || x == 1) {
    config_set_string (argv[1], argv[2]);
    return LISP_RET_TRUE;
  }
  else {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
}

static int process_conf_get_string (int argc, char **argv)
{
  int x;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <name>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  x = config_gettype (argv[1]);
  if (x == -1) {
    fprintf (stderr, "%s: `%s' does not exist\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  if (x != 1) {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
  LispSetReturnString (config_get_string (argv[1]));
  return LISP_RET_STRING;
}

static int process_conf_set_real (int argc, char **argv)
{
  double v;
  int x;
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <val>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "si");
  
  v = atof (argv[2]);
  x = config_gettype (argv[1]);
  if (x == -1 || x == 2) {
    config_set_real (argv[1], v);
    return LISP_RET_TRUE;
  }
  else {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
  return 1;
}

static int process_conf_get_real (int argc, char **argv)
{
  double v;
  int x;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <name>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  x = config_gettype (argv[1]);
  if (x == -1) {
    fprintf (stderr, "%s: `%s' does not exist\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  if (x != 2) {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
  LispSetReturnFloat (config_get_real (argv[1]));
  return LISP_RET_FLOAT;
}

static int process_conf_set_default_int (int argc, char **argv)
{
  int v, x;
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <val>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "si");
  
  v = atoi (argv[2]);
  x = config_gettype (argv[1]);
  if (x == -1 || x == 0) {
    config_set_int (argv[1], v);
    return LISP_RET_TRUE;
  }
  else {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
}

static int process_conf_set_default_real (int argc, char **argv)
{
  double v;
  int x;
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <val>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "sf");
  
  v = atof (argv[2]);
  x = config_gettype (argv[1]);
  if (x == -1 || x == 2) {
    config_set_real (argv[1], v);
    return LISP_RET_TRUE;
  }
  else {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
}

static int process_conf_set_default_string (int argc, char **argv)
{
  int x;
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <name> <val>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "ss");
  
  x = config_gettype (argv[1]);
  if (x == -1 || x == 1) {
    config_set_string (argv[1], argv[2]);
    return LISP_RET_TRUE;
  }
  else {
    fprintf (stderr, "%s: `%s' exists with different type (%s)\n", argv[0],
	     argv[1], conf_name (x));
    return LISP_RET_ERROR;
  }
}


static struct LispCliCommand conf_cmds[] = {
  { NULL, "Configuration parameters", NULL },
  { "read", "<file> - read configuration file", process_conf_read },
  { "save", "<file> - write parameters to file", process_conf_save },
  { "gettype", "<name> - get config paramter type (-1 = missing, 0 = int, 1 = string, 2 = real)", process_conf_gettype },
  { "set_int", "<name> <val> - set integer config parameter", process_conf_set_int },
  { "set_string", "<name> <val> - set string config parameter", process_conf_set_string },
  { "set_real", "<name> <val> - set real config parameter", process_conf_set_real },
  { "set_default_int", "<name> <val> - set default integer config parameter", process_conf_set_int },
  { "set_default_string", "<name> <val> - set default string config parameter", process_conf_set_string },
  { "set_default_real", "<name> <val> - set default real config parameter", process_conf_set_real },
  { "get_int", "<name> - return integer config parameter", process_conf_get_int },
  { "get_string", "<name> - get string config parameter", process_conf_get_string },
  { "get_real", "<name> - get real config parameter", process_conf_get_real }
};


void conf_cmds_init (void)
{
  LispCliAddCommands ("conf", conf_cmds, sizeof (conf_cmds)/sizeof (conf_cmds[0]));
}
