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

int output_window_width;

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
  int sz = 1024;
  int pos = 0;
  int len;
  const char *s = NULL;
  switch (F.s) {
  case STATE_ANY:
    s = "[any]";
    break;
    
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

  case STATE_DIRTY:
    s = "[dirty]";
    break;
    
  }
  snprintf (buf + pos, sz, "%s\n  ", s);
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
  if (F.ckt_gen) {
    snprintf (buf + pos, sz, " ckt:yes");
  }
  else {
    snprintf (buf + pos, sz, " ckt:no");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;

  if (F.cell_map) {
    snprintf (buf + pos, sz, " cells:yes");
  }
  else {
    snprintf (buf + pos, sz, " cells:no");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;

  if (F.act_toplevel) {
    snprintf (buf + pos, sz, " top:set");
  }
  else {
    snprintf (buf + pos, sz, " top:unset");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
  
  if (F.timer == 0) {
    snprintf (buf + pos, sz, " timer:none");
  }
  else if (F.timer == TIMER_INIT) {
    snprintf (buf + pos, sz, " timer:init");
  }
  else if (F.timer == TIMER_RUN) {
    snprintf (buf + pos, sz, " timer:run");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;

#ifdef FOUND_dali
  if (F.dali) {
    snprintf (buf + pos, sz, " placer:init");
  }
  else {
    snprintf (buf + pos, sz, " placer:none");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
#endif
#ifdef FOUND_phydb
  if (F.phydb) {
    snprintf (buf + pos, sz, " phydb:init");
  } 
  else {
    snprintf (buf + pos, sz, " phydb:none");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
#endif
     
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
  if (required == STATE_ANY || F.s == required) {
    return 1;
  }

  warning ("%s: command failed; incorrect flow state.\n Current flow state: %s", argv[0], get_state_str ());
  fprintf (stderr, "\n");
  return 0;
}

/*------------------------------------------------------------------------

  Standard getopt-style parsing

  Returns a hash table with each option that exists.
  For options that are flags, it returns a count.
  For options that are strings, pointer is the string.
  Missing entry in hash table is unspecified option.

  Returns NULL on error.

  *argc is updated to optind, the remaining args.

--------------------------------------------------------------------------*/
struct Hashtable *std_getopt (int *argc, char **argv, const char *opts)
{
  int ch;
  struct Hashtable *H;
  hash_bucket_t *b;
  unsigned char tab[256];
  
  H = hash_new (4);

  for (int i=0; i < 256; i++) {
    tab[i] = 2;
  }
  for (int i=0; opts[i]; i++) {
    if (opts[i+1] == ':') {
      tab[(int)opts[i]] = 1; // string
    }
    else {
      tab[(int)opts[i]] = 0; // not string
    }
  }

  int i;
  char str[2];
  for (i=1; i < *argc; i++) {
    if (argv[i][0] == '-') {
      int pos;
      if (argv[i][1] == '\0' || argv[i][1] == '-') {
	// end of options
	break;
      }
      pos = 1;
      while (argv[i][pos]) {
	bool err_flag = false;
	str[0] = argv[i][pos];
	str[1] = '\0';
	if (tab[(int)argv[i][pos]] == 1) {
	  // string
	  b = hash_lookup (H, str);
	  if (b) {
	    FREE (b->v);
	  }
	  else {
	    b = hash_add (H, str);
	  }
	  if (argv[i][pos+1] == '\0') {
	    if (i != (*argc)-1) {
	      b->v = Strdup (argv[i+1]);
	      i++;
	      pos = strlen (argv[i])-1;
	    }
	    else {
	      b->v = NULL;
	      err_flag = true;
	      printf ("Error in option parsing: missing argument to `-%c'\n",
		      argv[i][pos]);
	    }
	  }
	  else {
	    b->v = Strdup (argv[i]+pos+1);
	    pos += strlen (argv[i]+pos);
	  }
	}
	else if (tab[(int)argv[i][pos]] == 0) {
	  b = hash_lookup (H, str);
	  if (b) {
	    b->i++;
	  }
	  else {
	    b = hash_add (H, str);
	    b->i = 1;
	  }
	}
	else {
	  printf ("Error in option parsing: unknown option `-%c'\n",
		  argv[i][pos]);
	  err_flag = true;
	}
	if (err_flag) {
	  // error in option parsing
	  for (int j=0; j < 256; j++) {
	    if (tab[j] == 1) {
	      str[0] = (char) j;
	      str[1] = '\0';
	      b = hash_lookup (H, str);
	      if (b && b->v) {
		FREE (b->v);
	      }
	    }
	  }
	  hash_free (H);
	  return NULL;
	}
	pos++;
      }
    }
  }
  *argc = i;
  return H;
}


void std_opt_free (struct Hashtable *H, const char *opts)
{
  int tab[256];
  
  for (int i=0; i < 256; i++) {
    tab[i] = 2;
  }
  for (int i=0; opts[i]; i++) {
    if (opts[i+1] == ':') {
      tab[(int)opts[i]] = 1; // string
    }
    else {
      tab[(int)opts[i]] = 0; // not string
    }
  }

  hash_iter_t it;
  hash_bucket_t *b;
  hash_iter_init (H, &it);
  while ((b = hash_iter_next (H, &it))) {
    if (tab[(int)b->key[0]] == 1) {
      if (b->v) {
	FREE (b->v);
      }
    }
  }
  hash_free (H);
}


/*--------------------------------------------------------------------------

  Argument and state checking, combined with design state.

--------------------------------------------------------------------------*/

struct Hashtable *std_argcheck_opt (int &argc, char **argv, const char *opts,
				    design_state required,
				    const char *usage)
{
  if (required == STATE_ANY || F.s == required) {
    int v = argc;
    // handle arguments
    struct Hashtable *H = std_getopt (&v, argv, opts);
    argc = v;
    if (H == NULL) {
      fprintf (stderr, "Usage: %s %s\n", argv[0], usage);
      return NULL;
    }
    return H;
  }
  else {
    warning ("%s: command failed; incorrect flow state.\n Current flow state: %s", argv[0], get_state_str ());
    fprintf (stderr, "\n");
    return NULL;
  }
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
  F.sp = NULL;
  F.tp = NULL;

  F.act_design = new Act ();
  F.act_toplevel = NULL;

#ifdef FOUND_dali
  F.dali = NULL;
#endif

#ifdef FOUND_pwroute
  F.pwroute = NULL;
#endif

#ifdef FOUND_sproute
  F.sproute = NULL;
#endif

#ifdef FOUND_phydb
  F.phydb = NULL;
  F.phydb_cell = 0;
  F.phydb_lef = 0;
  F.phydb_def = 0;
#endif  
  
}
