/*
      This file is part of GNUnet
      (C) 2006, 2007 Christian Grothoff (and other contributing authors)

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
 * @file module/service.c
 * @brief internal GNUnet DV_DHT service
 * @author Christian Grothoff
 */
#include "platform.h"
#include "table.h"
#include "routing.h"
#include "gnunet_dv_dht_service.h"
#include "service.h"

/**
 * Global core API.
 */
static GNUNET_CoreAPIForPlugins *coreAPI;

/**
 * Perform an asynchronous GET operation on the DV_DHT identified by
 * 'table' using 'key' as the key.  The peer does not have to be part
 * of the table (if so, we will attempt to locate a peer that is!).
 *
 * Even in the case of a time-out (once completion callback has been
 * invoked), clients will still call the "stop" function explicitly.
 *
 * @param table table to use for the lookup
 * @param key the key to look up
 * @param timeout how long to wait until this operation should
 *        automatically time-out
 * @param callback function to call on each result
 * @param cls extra argument to callback
 * @return handle to stop the async get
 */
static struct GNUNET_DV_DHT_GetHandle *
dv_dht_get_async_start (unsigned int type,
                        const GNUNET_HashCode * key,
                        GNUNET_ResultProcessor callback, void *cls)
{
  struct GNUNET_DV_DHT_GetHandle *ret;

  ret = GNUNET_malloc (sizeof (struct GNUNET_DV_DHT_GetHandle));
  ret->key = *key;
  ret->callback = callback;
  ret->cls = cls;
  ret->type = type;
  if (GNUNET_OK != GNUNET_DV_DHT_get_start (key, type, callback, cls))
    {
      GNUNET_free (ret);
      return NULL;
    }
  return ret;
}

/**
 * Stop async DV_DHT-get.  Frees associated resources.
 */
static int
dv_dht_get_async_stop (struct GNUNET_DV_DHT_GetHandle *record)
{
  GNUNET_DV_DHT_get_stop (&record->key, record->type, record->callback,
                          record->cls);
  GNUNET_free (record);
  return GNUNET_OK;
}

/**
 * Provide the DV DV_DHT service.  The DV DV_DHT service depends on the
 * RPC and DV services.
 *
 * @param capi the core API
 * @return NULL on errors, DV_DHT_API otherwise
 */
GNUNET_DV_DHT_ServiceAPI *
provide_module_dv_dht (GNUNET_CoreAPIForPlugins * capi)
{
  static GNUNET_DV_DHT_ServiceAPI api;
  if (GNUNET_OK != GNUNET_DV_DHT_table_init (capi))
    {
      GNUNET_GE_BREAK (capi->ectx, 0);
      return NULL;
    }
  if (GNUNET_OK != GNUNET_DV_DHT_init_routing (capi))
    {
      GNUNET_GE_BREAK (capi->ectx, 0);
      GNUNET_DV_DHT_table_done ();
      return NULL;
    }
  coreAPI = capi;
  api.get_start = &dv_dht_get_async_start;
  api.get_stop = &dv_dht_get_async_stop;
  api.put = &GNUNET_DV_DHT_put;
  return &api;
}

/**
 * Shutdown DV_DHT service.
 */
int
release_module_dv_dht ()
{
  GNUNET_DV_DHT_done_routing ();
  GNUNET_DV_DHT_table_done ();
  return GNUNET_OK;
}

/* end of service.c */
