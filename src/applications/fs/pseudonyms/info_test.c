/*
     This file is part of GNUnet.
     (C) 2005, 2006 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file applications/fs/pseudonym/info_test.c
 * @brief testcase for info.c
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util.h"
#include "gnunet_ecrs_lib.h"
#include "gnunet_pseudonym_lib.h"
#include "gnunet_pseudonym_lib.h"

#define CHECK(a) if (!(a)) { ok = GNUNET_NO; GNUNET_GE_BREAK(ectx, 0); goto FAILURE; }

int
main (int argc, char *argv[])
{
  int ok;
  struct GNUNET_ECRS_MetaData *meta = NULL;
  GNUNET_HashCode id;
  int old;
  int newVal;
  struct GNUNET_GC_Configuration *cfg;
  struct GNUNET_GE_Context *ectx;

  GNUNET_disable_entropy_gathering ();
  cfg = GNUNET_GC_create ();
  if (-1 == GNUNET_GC_parse_configuration (cfg, "check.conf"))
    {
      GNUNET_GC_free (cfg);
      return -1;
    }
  /* ACTUAL TEST CODE */
  old = GNUNET_PSEUDO_list_all (ectx, cfg, NULL, NULL);
  meta = GNUNET_ECRS_meta_data_create ();
  GNUNET_ECRS_meta_data_insert (meta, 0, "test");
  GNUNET_create_random_hash (&id);
  GNUNET_PSEUDO_add (ectx, cfg, &id, meta);
  newVal = GNUNET_PSEUDO_list_all (ectx, cfg, NULL, NULL);
  CHECK (old < newVal);
  /* END OF TEST CODE */
FAILURE:
  GNUNET_ECRS_meta_data_destroy (meta);
  GNUNET_GC_free (cfg);
  return (ok == GNUNET_YES) ? 0 : 1;
}

/* end of info_test.c */