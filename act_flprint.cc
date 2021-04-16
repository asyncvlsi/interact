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
#include <act/passes/aflat.h>
#include "all_cmds.h"

/*************************************************************************
 *
 *  PRS flattener: KEEP CONSISTENT WITH aflat
 *
 *************************************************************************
 */
static int _prs_out_fmt = 0;
#define _FLAT_ARRAY_STYLE (_prs_out_fmt == 0 ? 0 : 1)
#define _FLAT_EXTRA_ARGS  NULL, (_prs_out_fmt == 0 ? 0 : 1)
static ActId *_flat_current_prefix = NULL;
static struct Hashtable *labels = NULL;
static ActApplyPass *_gpass;

static void _flat_print_connect (FILE *fp)
{
  if (_prs_out_fmt == 0) { /* sim */
    fprintf (fp, "= ");
  }
  else {
    fprintf (fp, "connect ");
  }
}

static void _flat_prefix_id_print (FILE *fp,
				   Scope *s, ActId *id, const char *str = "")
{
  fprintf (fp, "\"");
  if (s->Lookup (id, 0)) {
    if (_flat_current_prefix) {
      if (id->getName()[0] != ':') {
        _flat_current_prefix->Print (fp);
        fprintf (fp, ".");
      }
    }
  }
  else {
    ValueIdx *vx;
    /* must be a global */
    vx = s->FullLookupVal (id->getName());
    Assert (vx, "Hmm.");
    Assert (vx->global, "Hmm");
    if (vx->global == ActNamespace::Global()) {
      /* nothing to do */
    }
    else {
      char *tmp = vx->global->Name ();
      fprintf (fp, "%s::", tmp);
      FREE (tmp);
    }
  }
  id->Print (fp, _FLAT_EXTRA_ARGS);
  fprintf (fp, "%s\"", str);
}

#define PREC_BEGIN(myprec)                      \
  do {                                          \
    if ((myprec) < prec) {                      \
      fprintf (fp, "(");			\
    }                                           \
  } while (0)

#define PREC_END(myprec)                        \
  do {                                          \
    if ((myprec) < prec) {                      \
      fprintf (fp, ")");			\
    }                                           \
  } while (0)

#define EMIT_BIN(myprec,sym)				\
  do {							\
    PREC_BEGIN(myprec);					\
    _print_prs_expr (fp, s, e->u.e.l, (myprec), flip);	\
    fprintf (fp, "%s", (sym));				\
    _print_prs_expr (fp, s, e->u.e.r, (myprec), flip);	\
    PREC_END (myprec);					\
  } while (0)

#define EMIT_UNOP(myprec,sym)				\
  do {							\
    PREC_BEGIN(myprec);					\
    fprintf (fp, "%s", sym);				\
    _print_prs_expr (fp, s, e->u.e.l, (myprec), flip);	\
    PREC_END (myprec);					\
  } while (0)


/*
  3 = ~
  2 = &
  1 = |
*/
static void _print_prs_expr (FILE *fp,
			     Scope *s, act_prs_expr_t *e, int prec, int flip)
{
  hash_bucket_t *b;
  act_prs_lang_t *pl;
  
  if (!e) return;
  switch (e->type) {
  case ACT_PRS_EXPR_AND:
    EMIT_BIN(2, "&");
    break;
    
  case ACT_PRS_EXPR_OR:
    EMIT_BIN(1, "|");
    break;
    
  case ACT_PRS_EXPR_VAR:
    if (flip) {
      fprintf (fp, "~");
    }
    _flat_prefix_id_print (fp, s, e->u.v.id);
    break;
    
  case ACT_PRS_EXPR_NOT:
    EMIT_UNOP(3, "~");
    break;
    
  case ACT_PRS_EXPR_LABEL:
    if (!labels) {
      fatal_error ("No labels defined!");
    }
    b = hash_lookup (labels, e->u.l.label);
    if (!b) {
      fatal_error ("Missing label `%s'", e->u.l.label);
    }
    pl = (act_prs_lang_t *) b->v;
    if (pl->u.one.dir == 0) {
      fprintf (fp, "~");
    }
    fprintf (fp, "(");
    _print_prs_expr (fp, s, pl->u.one.e, 0, flip);
    fprintf (fp, ")");
    break;

  case ACT_PRS_EXPR_TRUE:
    fprintf (fp, "true");
    break;

  case ACT_PRS_EXPR_FALSE:
    fprintf (fp,"false");
    break;

  default:
    fatal_error ("What?");
    break;
  }
}

static void print_attr_prefix (FILE *fp, act_attr_t *attr, int force_after)
{
  /* examine attributes:
     after
     weak
     unstab
  */
  int weak = 0;
  int unstab = 0;
  int after = 0;
  int have_after = 0;
  act_attr_t *x;

  if (_prs_out_fmt != 0) {
    /* not simulation */
    return;
  }

  for (x = attr; x; x = x->next) {
    if (strcmp (x->attr, "weak") == 0) {
      weak = 1;
    }
    else if (strcmp (x->attr, "unstab") == 0) {
      unstab = 1;
    }
    else if (strcmp (x->attr, "after") == 0) {
      after = x->e->u.v;
      have_after = 1;
    }
  }
  if (!have_after && force_after) {
    after = 0;
    have_after = 1;
  }
  if (weak) {
    fprintf (fp, "weak ");
  }
  if (unstab) {
    fprintf (fp, "unstab ");
  }
  if (have_after) {
    fprintf (fp, "after %d ", after);
  }
}

static void aflat_print_spec (FILE *fp, Scope *s, act_spec *spec)
{
  const char *tmp;
  ActId *id;
  while (spec) {
    if (ACT_SPEC_ISTIMING (spec)) {
      /* timing constraint: special representation */
      if (_prs_out_fmt == 0) {
	Expr *e = (Expr *)spec->ids[3];
	int delay;
	if (e) {
	  Assert (e->type == E_INT, "What?");
	  delay = e->u.v;
	}
	Array *aref[3];
	InstType *it[3];

	Assert (spec->count == 4, "Hmm...");

	for (int i=0; i < 3; i++) {
	  it[i] = s->FullLookup (spec->ids[i], &aref[i]);
	  Assert (it[i], "Hmm...");
	}

	if (it[0]->arrayInfo () && (!aref[0] || !aref[0]->isDeref())) {
	  spec->ids[0]->Print (stderr);
	  fprintf (stderr, "\n");
	  fatal_error ("Timing directive: LHS cannot be an array!");
	}
	ActId *tl[2];
	Arraystep *as[2];

	for (int i=1; i < 3; i++) {
	  if (it[i]->arrayInfo() && (!aref[i] || !aref[i]->isDeref ())) {
	    if (aref[i]) {
	      as[i-1] = it[i]->arrayInfo()->stepper (aref[i]);
	    }
	    else {
	      as[i-1] = it[i]->arrayInfo()->stepper();
	    }
	    tl[i-1] = spec->ids[i]->Tail();
	  }
	  else {
	    tl[i-1] = NULL;
	    as[i-1] = NULL;
	  }
	}

	if (!as[0] && !as[1]) {
	  fprintf (fp, "timing(");
	  _flat_prefix_id_print (fp, s, spec->ids[0]);

#define PRINT_EXTRA(x)					\
	  do {						\
	    if (spec->extra[x] & 0x03) {		\
	      if ((spec->extra[x] & 0x03) == 1) {	\
		fprintf (fp, "+");			\
	      }						\
	      else {					\
		fprintf (fp, "-");			\
	      }						\
	    }						\
	  } while (0)

	  PRINT_EXTRA (0);
	  fprintf (fp, ",");
	  _flat_prefix_id_print (fp, s, spec->ids[1]);
	  PRINT_EXTRA (1);
	  fprintf (fp, ",");
	  _flat_prefix_id_print (fp, s, spec->ids[2]);
	  PRINT_EXTRA (2);
	  if (e) {
	    fprintf (fp, ",%d", delay);
	  }
	  fprintf (fp, ")\n");
	}
	else {
	  if (aref[1]) {
	    tl[0]->setArray (NULL);
	  }
	  if (aref[2]) {
	    tl[1]->setArray (NULL);
	  }
	  
	  while ((as[0] && !as[0]->isend()) ||
		 (as[1] && !as[1]->isend())) {
	    char *tmp[2];
	    for (int i=0; i < 2; i++) {
	      if (as[i]) {
		tmp[i] = as[i]->string();
	      }
	      else {
		tmp[i] = NULL;
	      }
	    }
	    
	    fprintf (fp, "timing(");
	    _flat_prefix_id_print (fp, s, spec->ids[0]);
	    PRINT_EXTRA (0);
	    fprintf (fp, ",");
	    if (tmp[0]) {
	      _flat_prefix_id_print (fp, s, spec->ids[1], tmp[0]);
	      FREE (tmp[0]);
	    }
	    else {
	      _flat_prefix_id_print (fp, s, spec->ids[1]);
	    }
	    PRINT_EXTRA (1);
	    fprintf (fp, ",");
	    if (tmp[1]) {
	      _flat_prefix_id_print (fp, s, spec->ids[2], tmp[1]);
	      FREE (tmp[1]);
	    }
	    else {
	      _flat_prefix_id_print (fp, s, spec->ids[2]);
	    }
	    PRINT_EXTRA (2);
	    if (e) {
	      fprintf (fp, ",%d", delay);
	    }
	    fprintf (fp, ")\n");

	    if (as[1]) {
	      as[1]->step();
	      if (as[1]->isend()) {
		if (as[0]) {
		  as[0]->step();
		  if (as[0]->isend()) {
		    /* we're done! */
		  }
		  else {
		    /* refresh as[1] */
		    delete as[1];
		    if (aref[2]) {
		      as[1] = it[2]->arrayInfo()->stepper (aref[2]);
		    }
		    else {
		      as[1] = it[2]->arrayInfo()->stepper();
		    }
		  }
		}
		else {
		  /* we're done */
		}
	      }
	      else {
		/* keep going! */
	      }
	    }
	    else if (as[0]) {
	      as[0]->step();
	    }
	  }
	  if (as[0]) {
	    delete as[0];
	  }
	  if (as[1]) {
	    delete as[1];
	  }
	  if (aref[1]) {
	    tl[0]->setArray (aref[1]);
	  }
	  if (aref[2]) {
	    tl[1]->setArray (aref[2]);
	  }
	}
      }
      spec = spec->next;
      continue;
    }
    tmp = act_spec_string (spec->type);
    Assert (tmp, "Hmm");
    if ((_prs_out_fmt == 0 && 
	 (strcmp (tmp, "mk_exclhi") == 0 || strcmp (tmp, "mk_excllo") == 0)) ||
	(_prs_out_fmt != 0 &&
	 (strcmp (tmp, "exclhi") == 0 || strcmp (tmp, "excllo") == 0))) {
      if (spec->count > 0) {
	int comma = 0;
	fprintf (fp, "%s(", tmp);
	for (int i=0; i < spec->count; i++) {
	  Array *aref;
	  id = spec->ids[i];
	  InstType *it = s->FullLookup (id, &aref);
	  /* spec->ids[i] might be a bool, or a bool array! fix bool
	     array case */
	  if (it->arrayInfo() && (!aref || !aref->isDeref())) {
	    Arraystep *astep;
	    Array *a = it->arrayInfo();

	    id = id->Tail();
	    
	    if (aref) {
	      astep = a->stepper (aref);
	      a = aref;
	    }
	    else {
	      astep = a->stepper();
	      a = NULL;
	    }
	    int restore = 0;
	    if (id->arrayInfo()) {
	      id->setArray (NULL);
	      restore = 1;
	    }
	    while (!astep->isend()) {
	      char *tmp = astep->string();
	      if (comma != 0) {
		fprintf (fp, ",");
	      }
	      _flat_prefix_id_print (fp, s, spec->ids[i], tmp);
	      comma = 1;
	      FREE (tmp);
	      astep->step();
	    }
	    delete astep;
	    if (restore) {
	      id->setArray (a);
	    }
	  }
	  else {
	    if (comma != 0) {
	      fprintf (fp, ",");
	    }
	    _flat_prefix_id_print (fp, s, spec->ids[i]);
	    comma = 1;
	  }
	}
	fprintf (fp, ")\n");
      }
    }
    spec = spec->next;
  }
}

static void aflat_print_prs (FILE *fp, Scope *s, act_prs_lang_t *p)
{
  if (!p) return;
  
  while (p) {
    switch (p->type) {
    case ACT_PRS_RULE:
      /* attributes */
      if (p->u.one.label) {
	hash_bucket_t *b;
	if (!labels) {
	  labels = hash_new (4);
	}
	if (hash_lookup (labels, (char *)p->u.one.id)) {
	  fatal_error ("Duplicate label `%s'", (char *)p->u.one.id);
	}
	b = hash_add (labels, (char *)p->u.one.id);
	b->v = p;
      }
      else {
	print_attr_prefix (fp, p->u.one.attr, 0);
	_print_prs_expr (fp, s, p->u.one.e, 0, 0);
	fprintf (fp, "->");
	_flat_prefix_id_print (fp, s, p->u.one.id);
	if (p->u.one.dir) {
	  fprintf (fp, "+\n");
	}
	else {
	  fprintf (fp, "-\n");
	}
	if (p->u.one.arrow_type == 1) {
	  print_attr_prefix (fp, p->u.one.attr, 0);
	  fprintf (fp, "~(");
	  _print_prs_expr (fp, s, p->u.one.e, 0, 0);
	  fprintf (fp, ")");
	  fprintf (fp, "->");
	  _flat_prefix_id_print (fp, s, p->u.one.id);
	  if (p->u.one.dir) {
	    fprintf (fp, "-\n");
	  }
	  else {
	    fprintf (fp, "+\n");
	  }
	}
	else if (p->u.one.arrow_type == 2) {
	  print_attr_prefix (fp, p->u.one.attr, 0);
	  _print_prs_expr (fp, s, p->u.one.e, 0, 1);
	  fprintf (fp, "->");
	  _flat_prefix_id_print (fp, s, p->u.one.id);
	  if (p->u.one.dir) {
	    fprintf (fp, "-\n");
	  }
	  else {
	    fprintf (fp, "+\n");
	  }
	}
	else if (p->u.one.arrow_type != 0) {
	  fatal_error ("Eh?");
	}
      }
      break;
    case ACT_PRS_GATE:
      print_attr_prefix (fp, p->u.p.attr, 1);
      if (p->u.p.g) {
	/* passn */
	_flat_prefix_id_print (fp, s, p->u.p.g);
	fprintf (fp, " & ~");
	_flat_prefix_id_print (fp, s, p->u.p.s);
	fprintf (fp, " -> ");
	_flat_prefix_id_print (fp, s, p->u.p.d);
	fprintf (fp, "-\n");
      }
      if (p->u.p._g) {
	fprintf (fp, "~");
	_flat_prefix_id_print (fp, s, p->u.p._g);
	fprintf (fp, " & ");
	_flat_prefix_id_print (fp, s, p->u.p.s);
	fprintf (fp, " -> ");
	_flat_prefix_id_print (fp, s, p->u.p.d);
	fprintf (fp, "+\n");
      }
      break;
    case ACT_PRS_TREE:
      /* this is fine */
      aflat_print_prs (fp, s, p->u.l.p);
      break;
    case ACT_PRS_SUBCKT:
      /* this is fine */
      aflat_print_prs (fp, s, p->u.l.p);
      break;
    default:
      fatal_error ("loops should have been expanded by now!");
      break;
    }
    p = p->next;
  }
}

static void aflat_dump (FILE *fp, Scope *s, act_prs *prs, act_spec *spec)
{
  while (prs) {
    aflat_print_prs (fp, s, prs->p);
    prs = prs->next;
  }
  aflat_print_spec (fp, s, spec);
}

static void aflat_ns (FILE *fp, ActNamespace *ns)
{
  aflat_dump (fp, ns->CurScope(), ns->getprs(), ns->getspec());
}

static void aflat_body (void *cookie, ActId *prefix, Process *p)
{
  FILE *fp = (FILE *)cookie;
  Assert (p->isExpanded(), "What?");
  _flat_current_prefix = prefix;
  if (labels) {
    hash_clear (labels);
  }
  aflat_dump (fp, p->CurScope(), p->getprs(), p->getspec());
  _flat_current_prefix = NULL;
}

static void aflat_conns (void *cookie, ActId *id1, ActId *id2)
{
  FILE *fp = (FILE *)cookie;
  
  _flat_print_connect (fp);

  fprintf (fp, "\"");
  _gpass->printns (fp);
  id1->Print (fp);
  fprintf (fp, "\" \"");
  _gpass->printns (fp);
  id2->Print (fp);
  fprintf (fp, "\"\n");
}

void act_flatten_prs (Act *a, FILE *fp, Process *p, int mode)
{
  ActPass *ap = a->pass_find ("apply");
  if (!ap) {
    _gpass = new ActApplyPass (a);
  }
  else {
    _gpass = dynamic_cast<ActApplyPass *>(ap);
  }

  _prs_out_fmt = mode;

  _gpass->setCookie (fp);
  _gpass->setInstFn (aflat_body);
  _gpass->setConnPairFn (aflat_conns);
  _gpass->run (p);
  aflat_ns (fp, a->Global());
}
