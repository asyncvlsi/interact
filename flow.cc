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
#include <string.h>
#include "flow.h"

/*************************************************************************
 *
 *
 *  Utility functions
 *
 *
 *************************************************************************
 */
static const char *get_state_str (void)
{
  static char buf[1024];
  const char *s = NULL;
  switch (F.s) {
  case STATE_EMPTY:
    s = "[no design]";
    break;

  case STATE_DESIGN:
    s = "[unexpanded design]";
    break;

  case STATE_EXPANDED:
    s = "[expanded design]";
    break;

  case STATE_ERROR:
    s = "[error]";
    break;
    
  }
  snprintf (buf, 1024, "%s", s);
  if (F.ckt_gen) {
    snprintf (buf + strlen (buf), 1000, " ckt:yes");
  }
  else {
    snprintf (buf + strlen (buf), 1000, " ckt:no");
  }

  if (F.cell_map) {
    snprintf (buf + strlen (buf), 1000, " cells:yes");
  }
  else {
    snprintf (buf + strlen (buf), 1000, " cells:no");
  }

  if (F.act_toplevel) {
    snprintf (buf + strlen (buf), 1000, " top:set");
  }
  else {
    snprintf (buf + strlen (buf), 1000, " top:unset");
  }
  return buf;
}


/*--------------------------------------------------------------------------

  Check that # args match, and that we are in a valid flow state
  for the command.

--------------------------------------------------------------------------*/

int std_argcheck (int argc, char **argv, int argnum, const char *usage,
		  design_state required)
{
  if (argc != argnum) {
    fprintf (stderr, "Usage: %s %s\n", argv[0], usage);
    return 0;
  }
  if (F.s == required) {
    return 1;
  }

  warning ("%s: command failed; incorrect flow state.\n Current flow state: %s", argv[0], get_state_str ());
  fprintf (stderr, "\n");
  return 0;
}


/*--------------------------------------------------------------------------

  Open/close output file, interpreting "-" as stdout

--------------------------------------------------------------------------*/
FILE *std_open_output (const char *cmd, const char *s)
{
  FILE *fp;
  if (strcmp (s, "-") == 0) {
    fp = stdout;
  }
  else {
    fp = fopen (s, "w");
    if (!fp) {
      return NULL;
    }
  }
  return fp;
}

void std_close_output (FILE *fp)
{
  if (fp != stdout) {
    fclose (fp);
  }
}



void flow_init (void)
{
  F.s = STATE_EMPTY;
  F.cell_map = 0;
  F.ckt_gen = 0;
  F.timer = 0;

  F.act_design = NULL;
  F.act_toplevel = NULL;

#ifdef FOUND_dali
  F.dali = NULL;
#endif

#ifdef FOUND_phydb
  F.phydb = NULL;
  F.phydb_cell = 0;
  F.phydb_lef = 0;
  F.phydb_def = 0;
#endif  
  
}
