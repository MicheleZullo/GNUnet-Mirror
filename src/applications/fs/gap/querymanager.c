/*
      This file is part of GNUnet
      (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008 Christian Grothoff (and other contributing authors)

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
      Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
      Boston, MA 02110-1301, USA.
 */

/**
 * @file fs/gap/querymanager.c
 * @brief management of queries from our clients
 * @author Christian Grothoff
 *
 * This code forwards queries (using GAP and DHT) to other peers and
 * passes replies (from GAP or DHT) back to clients.
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "querymanager.h"
#include "fs.h"
#include "fs_dht.h"
#include "gap.h"
#include "dht.h"
#include "plan.h"
#include "pid_table.h"
#include "shared.h"

#define CHECK_REPEAT_FREQUENCY (5 * GNUNET_CRON_SECONDS)

/**
 * Linked list with information for each client.
 */
struct ClientDataList
{

  /**
   * This is a linked list.
   */
  struct ClientDataList *next;

  /**
   * For which client is this data kept?
   */
  struct GNUNET_ClientHandle *client;

  /**
   * List of active requests for the client.
   */
  struct RequestList *requests;

};

static GNUNET_CoreAPIForPlugins *coreAPI;

/**
 * List of all clients, their active requests and other
 * per-client information.
 */
static struct ClientDataList *clients;

/**
 * A client is asking us to run a query.  The query should be issued
 * until either a unique response has been obtained or until the
 * client disconnects.
 *
 * @param target peer known to have the content, maybe NULL.
 */
void
GNUNET_FS_QUERYMANAGER_start_query (const GNUNET_HashCode * query,
                                    unsigned int key_count,
                                    unsigned int anonymityLevel,
                                    unsigned int type,
                                    struct GNUNET_ClientHandle *client,
                                    const GNUNET_PeerIdentity * target)
{
  struct ClientDataList *cl;
  struct RequestList *request;

  GNUNET_GE_ASSERT (NULL, key_count > 0);

  request =
    GNUNET_malloc (sizeof (struct RequestList) +
                   (key_count - 1) * sizeof (GNUNET_HashCode));
  memset (request, 0, sizeof (struct RequestList));
  request->anonymityLevel = anonymityLevel;
  request->key_count = key_count;
  request->type = type;
  request->primary_target = GNUNET_FS_PT_intern (target);
  request->response_client = client;
  memcpy (&request->queries[0], query, sizeof (GNUNET_HashCode) * key_count);
  GNUNET_mutex_lock (GNUNET_FS_lock);
  cl = clients;
  while ((cl != NULL) && (cl->client != client))
    cl = cl->next;
  if (cl == NULL)
    {
      cl = GNUNET_malloc (sizeof (struct ClientDataList));
      memset (cl, 0, sizeof (struct ClientDataList));
      cl->next = clients;
      clients = cl;
    }
  request->next = cl->requests;
  cl->requests = request;
  GNUNET_FS_PLAN_request (client, 0, request);
  if (request->anonymityLevel == 0)
    {
      request->last_dht_get = GNUNET_get_time ();
      request->dht_back_off = MAX_DHT_DELAY;
      GNUNET_FS_DHT_execute_query (request->type, &request->queries[0]);
    }
  GNUNET_mutex_unlock (GNUNET_FS_lock);
}

/**
 * How many bytes should a bloomfilter be if
 * we have already seen entry_count responses?
 * Note that GAP_BLOOMFILTER_K gives us the
 * number of bits set per entry.  Furthermore,
 * we should not re-size the filter too often
 * (to keep it cheap).
 *
 * Since other peers will also add entries but
 * not resize the filter, we should generally
 * pick a slightly larger size than what the
 * strict math would suggest.
 *
 * @return must be a power of two and smaller
 *         or equal to 2^15.
 */
static unsigned int
compute_bloomfilter_size (unsigned int entry_count)
{
  unsigned short size;
  unsigned short max = 1 << 15;
  unsigned int ideal = (entry_count * GAP_BLOOMFILTER_K) / 8;

  size = 8;
  while ((size < max) && (size < ideal))
    size *= 2;
  return size;
}

struct IteratorClosure
{
  struct ResponseList *pos;
  int mingle_number;
};

/**
 * Iterator over response list.
 *
 * @param arg pointer to a location where we
 *        have our current index into the linked list.
 * @return GNUNET_YES if we have more,
 *         GNUNET_NO if this is the last entry
 */
static int
response_bf_iterator (GNUNET_HashCode * next, void *arg)
{
  struct IteratorClosure *cls = arg;
  struct ResponseList *r = cls->pos;

  if (NULL == r)
    return GNUNET_NO;
  GNUNET_FS_HELPER_mingle_hash (&r->hash, cls->mingle_number, next);
  cls->pos = r->next;
  return GNUNET_YES;
}

/**
 * We got a response for a client request.
 * Check if we have seen this response already.
 * If not, check if it truly matches (namespace!).
 * If so, transmit to client and update response
 * lists and bloomfilter accordingly.
 *
 * @
 * @param value how much is this response worth to us?
 *        the function should increment value accordingly
 * @return GNUNET_OK if this was the last response
 *         and we should remove the request entry.
 *         GNUNET_NO if we should continue looking
 *         GNUNET_SYSERR on serious errors
 */
static int
handle_response (PID_INDEX sender,
                 struct GNUNET_ClientHandle *client,
                 struct RequestList *rl,
                 const GNUNET_HashCode * primary_key,
                 GNUNET_CronTime expirationTime,
                 unsigned int size, const DBlock * data, unsigned int *value)
{
  struct IteratorClosure ic;
  CS_fs_reply_content_MESSAGE *msg;
  GNUNET_HashCode hc;
  int ret;
  unsigned int bf_size;

  /* check that content matches query */
  ret = GNUNET_FS_SHARED_test_valid_new_response (rl,
                                                  primary_key,
                                                  size, data, &hc);
  if (ret != GNUNET_OK)
    return ret;
  if (sender == 0)              /* dht produced response */
    rl->dht_back_off = MAX_DHT_DELAY;   /* go back! */
  /* send to client */
  msg = GNUNET_malloc (sizeof (CS_fs_reply_content_MESSAGE) + size);
  msg->header.size = htons (sizeof (CS_fs_reply_content_MESSAGE) + size);
  msg->header.type = htons (GNUNET_CS_PROTO_GAP_RESULT);
  msg->anonymityLevel = htonl (0);      /* unknown */
  msg->expirationTime = GNUNET_htonll (expirationTime);
  memcpy (&msg[1], data, size);
  coreAPI->cs_send_to_client (client,
                              &msg->header,
                              (rl->type != GNUNET_ECRS_BLOCKTYPE_DATA)
                              ? GNUNET_NO : GNUNET_YES);
  GNUNET_free (msg);

  /* update *value */
  *value += 1 + rl->value;
  GNUNET_FS_PLAN_success (sender, client, 0, rl);

  if (rl->type == GNUNET_ECRS_BLOCKTYPE_DATA)
    return GNUNET_OK;           /* the end */

  /* update bloom filter */
  rl->bloomfilter_entry_count++;
  bf_size = compute_bloomfilter_size (rl->bloomfilter_entry_count);
  if (rl->bloomfilter == NULL)
    {
      rl->bloomfilter_mutator
        = GNUNET_random_u32 (GNUNET_RANDOM_QUALITY_WEAK, -1);
      rl->bloomfilter_size = bf_size;
      rl->bloomfilter = GNUNET_bloomfilter_init (NULL,
                                                 NULL,
                                                 rl->bloomfilter_size,
                                                 GAP_BLOOMFILTER_K);
    }
  else if (rl->bloomfilter_size != bf_size)
    {
      rl->bloomfilter_mutator
        = GNUNET_random_u32 (GNUNET_RANDOM_QUALITY_WEAK, -1);
      ic.pos = rl->responses;
      ic.mingle_number = rl->bloomfilter_mutator;
      GNUNET_bloomfilter_resize (rl->bloomfilter,
                                 &response_bf_iterator,
                                 &ic, bf_size, GAP_BLOOMFILTER_K);
    }
  GNUNET_FS_SHARED_mark_response_seen (rl, &hc);

  /* we want more */
  return GNUNET_NO;
}

/**
 * Handle the given response (by forwarding it to
 * other peers as necessary).
 *
 * @param sender who send the response (good too know
 *        for future routing decisions)
 * @param primary_query hash code used for lookup
 *        (note that namespace membership may
 *        require additional verification that has
 *        not yet been performed; checking the
 *        signature has already been done)
 * @param size size of the data
 * @param data the data itself (a DBlock)
 * @return how much was this content worth to us?
 */
unsigned int
GNUNET_FS_QUERYMANAGER_handle_response (const GNUNET_PeerIdentity * sender,
                                        const GNUNET_HashCode * primary_query,
                                        GNUNET_CronTime expirationTime,
                                        unsigned int size,
                                        const DBlock * data)
{
  struct ClientDataList *cl;
  struct RequestList *rl;
  struct RequestList *prev;
  unsigned int value;
  PID_INDEX rid;

  rid = GNUNET_FS_PT_intern (sender);
  GNUNET_mutex_lock (GNUNET_FS_lock);
  value = 0;
  cl = clients;
  while (cl != NULL)
    {
      rl = cl->requests;
      prev = NULL;
      while (rl != NULL)
        {
          if (GNUNET_OK ==
              handle_response (rid,
                               cl->client,
                               rl,
                               primary_query,
                               expirationTime, size, data, &value))
            {
              if (prev != NULL)
                prev->next = rl->next;
              else
                cl->requests = rl->next;
              GNUNET_FS_SHARED_free_request_list (rl);
              if (prev == NULL)
                rl = cl->requests;
              else
                rl = prev->next;
            }
          else
            {
              prev = rl;
              rl = rl->next;
            }
        }
      cl = cl->next;
    }

  GNUNET_mutex_unlock (GNUNET_FS_lock);
  GNUNET_FS_PT_change_rc (rid, -1);
  return value;
}

/**
 * Method called whenever a given client disconnects.
 * Frees all of the associated data structures.
 */
static void
handle_client_exit (struct GNUNET_ClientHandle *client)
{
  struct ClientDataList *cl;
  struct ClientDataList *prev;
  struct RequestList *rl;

  GNUNET_mutex_lock (GNUNET_FS_lock);
  cl = clients;
  prev = NULL;
  while ((cl != NULL) && (cl->client != client))
    {
      prev = cl;
      cl = cl->next;
    }
  if (cl != NULL)
    {
      while (cl->requests != NULL)
        {
          rl = cl->requests;
          cl->requests = rl->next;
          GNUNET_FS_SHARED_free_request_list (rl);
        }
      if (prev == NULL)
        clients = cl->next;
      else
        prev->next = cl->next;
      GNUNET_free (cl);
    }
  GNUNET_mutex_unlock (GNUNET_FS_lock);
}

/**
 * Cron-job to periodically check if we should
 * repeat requests.
 */
static void
repeat_requests_job (void *unused)
{
  struct ClientDataList *client;
  struct RequestList *request;
  GNUNET_CronTime now;

  GNUNET_mutex_lock (GNUNET_FS_lock);
  now = GNUNET_get_time ();
  client = clients;
  while (client != NULL)
    {
      request = client->requests;
      while (request != NULL)
        {
          if ((NULL == request->plan_entries) &&
              ((request->expiration == 0) ||
               (request->expiration > now)) &&
              (request->last_ttl_used * GNUNET_CRON_SECONDS +
               request->last_request_time > now))
            GNUNET_FS_PLAN_request (client->client, 0, request);

          if ((request->anonymityLevel == 0) &&
              (request->last_dht_get + request->dht_back_off < now))
            {
              if (request->dht_back_off * 2 > request->dht_back_off)
                request->dht_back_off *= 2;
              request->last_dht_get = now;
              GNUNET_FS_DHT_execute_query (request->type,
                                           &request->queries[0]);
            }
          request = request->next;
        }
      client = client->next;
    }
  GNUNET_mutex_unlock (GNUNET_FS_lock);
}

int
GNUNET_FS_QUERYMANAGER_init (GNUNET_CoreAPIForPlugins * capi)
{
  coreAPI = capi;
  GNUNET_GE_ASSERT (capi->ectx,
                    GNUNET_SYSERR !=
                    capi->cs_exit_handler_register (&handle_client_exit));
  GNUNET_cron_add_job (capi->cron,
                       &repeat_requests_job,
                       CHECK_REPEAT_FREQUENCY, CHECK_REPEAT_FREQUENCY, NULL);
  return 0;
}

int
GNUNET_FS_QUERYMANAGER_done ()
{
  GNUNET_cron_del_job (coreAPI->cron,
                       &repeat_requests_job, CHECK_REPEAT_FREQUENCY, NULL);
  GNUNET_GE_ASSERT (coreAPI->ectx,
                    GNUNET_SYSERR !=
                    coreAPI->
                    cs_exit_handler_unregister (&handle_client_exit));
  return 0;
}

/* end of querymanager.c */