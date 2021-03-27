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
#include "ptr_manager.h"
#include <hash.h>
#include <array.h>

struct ptr_entries {
  A_DECL (void *, ptrs);
};

static struct Hashtable *tag_hash = NULL;

/* maps pointers to hash buckets in the tag_hash */
static struct pHashtable *ptr_hash = NULL;

static void init (void)
{
  if (!tag_hash) {
    tag_hash = hash_new (2);
    ptr_hash = phash_new (8);
  }
}

int ptr_register (const char *tag, void *x)
{
  hash_bucket_t *b;
  struct ptr_entries *e;
  phash_bucket_t *pb;
  int i;

  init ();

  b = hash_lookup (tag_hash, tag);
  if (!b) {
    b = hash_add (tag_hash, tag);
    NEW (e, struct ptr_entries);
    A_INIT (e->ptrs);
    b->v = e;
  }
  else {
    e = (struct ptr_entries *)b->v;
  }

  pb = phash_lookup (ptr_hash, x);
  if (pb) {
    /* error */
    return -1;
  }

  pb = phash_add (ptr_hash, x);
  pb->v = b;
  
  for (i=0; i < A_LEN (e->ptrs); i++) {
    if (!e->ptrs[i]) {
      e->ptrs[i] = x;
      return i;
    }
  }
  A_NEW (e->ptrs, void *);
  A_NEXT (e->ptrs) = x;
  A_INC (e->ptrs);
  return A_LEN (e->ptrs)-1;
}

int ptr_unregister (const char *tag, int idx)
{
  hash_bucket_t *b;
  struct ptr_entries *e;
  phash_bucket_t *pb;

  b = hash_lookup (tag_hash, tag);
  if (!b) {
    return -1;
  }
  e = (struct ptr_entries *)b->v;
  if (idx < 0 || idx >= A_LEN (e->ptrs)) {
    return -1;
  }
  if (e->ptrs[idx] == NULL) {
    return -1;
  }
  pb = phash_lookup (ptr_hash, e->ptrs[idx]);
  if (!pb) {
    return -1;
  }
  phash_delete (ptr_hash, e->ptrs[idx]);
  e->ptrs[idx] = NULL;
  return 0;
}


void *ptr_get (const char *tag, int idx)
{
  hash_bucket_t *b;
  struct ptr_entries *e;
  phash_bucket_t *pb;

  b = hash_lookup (tag_hash, tag);
  if (!b) {
    return NULL;
  }
  e = (struct ptr_entries *)b->v;
  if (idx < 0 || idx >= A_LEN (e->ptrs)) {
    return NULL;
  }
  pb = phash_lookup (ptr_hash, e->ptrs[idx]);
  if (!pb) {
    return NULL;
  }
  return e->ptrs[idx];
}
