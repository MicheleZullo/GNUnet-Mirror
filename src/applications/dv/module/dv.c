/*
 This file is part of GNUnet.
 (C) 2008 Christian Grothoff (and other contributing authors)

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
 * @author Nathan Evans
 * @file applications/dv/module/dv.c
 * @brief Core of distance vector routing algorithm.  Loads the service,
 * initializes necessary routing tables, and schedules updates, etc.
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "gnunet_util.h"
#include "gnunet_core.h"
#include "dv.h"
#include "heap.h"

#define DEBUG_DV


struct GNUNET_DV_Context
{
  unsigned long long fisheye_depth;
  unsigned long long max_table_size;
  unsigned int send_interval;

  unsigned int curr_neighbor_table_size;
  unsigned int curr_connected_neighbor_table_size;
  unsigned short closing;

  struct GNUNET_Mutex *dvMutex;
  struct GNUNET_MultiHashMap *direct_neighbors;

  struct GNUNET_MultiHashMap *extended_neighbors;
  struct GNUNET_dv_heap neighbor_min_heap;
  struct GNUNET_dv_heap neighbor_max_heap;


};

static struct GNUNET_DV_Context *ctx;
static struct GNUNET_ThreadHandle *sendingThread;
static GNUNET_CoreAPIForPlugins *coreAPI;

// CG: unless defined in a header and used by
//     other C source files (or used with dlsym),'
//     make sure all of your functions are declared "static"
static int
printTableEntry (const GNUNET_HashCode * key,
    void *value, void *cls)
{
  struct GNUNET_dv_neighbor *neighbor = (struct GNUNET_dv_neighbor *)value;
  char *type = (char *)cls;
  GNUNET_EncName encPeer;

  GNUNET_hash_to_enc (&neighbor->referrer->hashPubKey, &encPeer);
	fprintf (stderr, "%s\tNeighbor: %s\nCost: %d",type, (char *) &encPeer, neighbor->cost);

	return GNUNET_OK;
}

static void print_tables()
{
  fprintf (stderr, "Printing directly connected neighbors:\n");
	GNUNET_multi_hash_map_iterate(ctx->direct_neighbors, &printTableEntry, "DIRECT");

	fprintf (stderr, "Printing extended neighbors:\n");
	GNUNET_multi_hash_map_iterate(ctx->extended_neighbors, &printTableEntry, "EXTENDED");

	return;
}

/*
static int
p2pHandleDVRouteMessage (const GNUNET_PeerIdentity * sender,
                            const GNUNET_MessageHeader * message)
{

	return GNUNET_OK;
}
*/

/*
 * Handles when a peer is either added due to being newly connected
 * or having been gossiped about, also called when a cost for a neighbor
 * needs to be updated.
 *
 * @param neighbor - ident of the peer whose info is being added/updated
 * @param referrer - if this is a gossiped peer, who did we hear it from?
 * @param cost - the cost to this peer (the actual important part!)
 *
 */
static int
addUpdateNeighbor (const GNUNET_PeerIdentity * peer,
                   const GNUNET_PeerIdentity * referrer, unsigned int cost)
{
  int ret;
  struct GNUNET_dv_neighbor *neighbor;
#ifdef DEBUG_DV
  GNUNET_EncName encPeer;

	fprintf (stderr, "Entering addUpdateNeighbor\n");
	if (referrer == NULL)
		fprintf (stderr, "Referrer is NULL\n");
  GNUNET_hash_to_enc (&peer->hashPubKey, &encPeer);
  fprintf (stderr, "Adding/Updating Node %s\n", (char *) &encPeer);
#endif
  ret = GNUNET_OK;

  GNUNET_mutex_lock(ctx->dvMutex);

  if (GNUNET_YES != GNUNET_multi_hash_map_contains(ctx->extended_neighbors,&peer->hashPubKey))
	{
		neighbor = GNUNET_malloc(sizeof (struct GNUNET_dv_neighbor));
		neighbor->cost = cost;
		neighbor->neighbor = GNUNET_malloc(sizeof(GNUNET_PeerIdentity));
		memcpy(neighbor->neighbor,peer,sizeof(GNUNET_PeerIdentity));
		GNUNET_multi_hash_map_put (ctx->extended_neighbors,&peer->hashPubKey,
																													 neighbor,
																													 GNUNET_MultiHashMapOption_REPLACE);

		GNUNET_DV_Heap_insert(&ctx->neighbor_max_heap, neighbor);
		GNUNET_DV_Heap_insert(&ctx->neighbor_min_heap, neighbor);

	}
	else
	{
		neighbor = GNUNET_multi_hash_map_get(ctx->extended_neighbors,&peer->hashPubKey);

		if (neighbor->cost > cost)
		{
			if (memcmp(neighbor->referrer, peer, sizeof(GNUNET_PeerIdentity)) == 0)
			{
				neighbor->cost = cost;
				GNUNET_DV_Heap_updatedCost(&ctx->neighbor_max_heap, neighbor);
				GNUNET_DV_Heap_updatedCost(&ctx->neighbor_min_heap, neighbor);
			}
			else
			{
				GNUNET_DV_Heap_removeNode(&ctx->neighbor_max_heap, neighbor);
				GNUNET_DV_Heap_removeNode(&ctx->neighbor_min_heap, neighbor);
				GNUNET_free(neighbor->neighbor);
				GNUNET_free(neighbor->referrer);
				GNUNET_free(neighbor);

				neighbor = GNUNET_malloc(sizeof (struct GNUNET_dv_neighbor));
				neighbor->cost = cost;
				neighbor->neighbor = GNUNET_malloc(sizeof(GNUNET_PeerIdentity));
				memcpy(neighbor->neighbor,peer,sizeof(GNUNET_PeerIdentity));
				if (referrer == NULL)
					neighbor->referrer = NULL;
				else
					memcpy(neighbor->referrer, referrer, sizeof(GNUNET_PeerIdentity));

				GNUNET_multi_hash_map_put (ctx->extended_neighbors,&peer->hashPubKey,
																															 neighbor,
																															 GNUNET_MultiHashMapOption_REPLACE);

				GNUNET_DV_Heap_insert(&ctx->neighbor_max_heap, neighbor);
				GNUNET_DV_Heap_insert(&ctx->neighbor_min_heap, neighbor);
			}
		}
		else if ((neighbor->cost < cost) && (memcmp(neighbor->referrer, referrer, sizeof(GNUNET_PeerIdentity)) == 0))
		{
			neighbor->cost = cost;

			GNUNET_DV_Heap_updatedCost(&ctx->neighbor_max_heap, neighbor);
			GNUNET_DV_Heap_updatedCost(&ctx->neighbor_min_heap, neighbor);

		}

	}

#ifdef DEBUG_DV
  print_tables();
  fprintf (stderr, "Exiting addUpdateNeighbor\n");
#endif

  GNUNET_mutex_unlock (ctx->dvMutex);
  return ret;
}

static int
p2pHandleDVNeighborMessage (const GNUNET_PeerIdentity * sender,
                            const GNUNET_MessageHeader * message)
{
  int ret = GNUNET_OK;
  const p2p_dv_MESSAGE_NeighborInfo *nmsg;
#ifdef DEBUG_DV
  GNUNET_EncName from;
  GNUNET_EncName about;
#endif

  if (ntohs (message->size) < sizeof (p2p_dv_MESSAGE_NeighborInfo))
    {
      GNUNET_GE_BREAK (NULL, 0);
      return GNUNET_SYSERR;     /* invalid message */
    }
  nmsg = (const p2p_dv_MESSAGE_NeighborInfo *) message;

  ret = addUpdateNeighbor (&nmsg->neighbor, sender, ntohl (nmsg->cost));

  if (GNUNET_OK != ret)
    GNUNET_GE_LOG (coreAPI->ectx,
                   GNUNET_GE_DEBUG | GNUNET_GE_REQUEST | GNUNET_GE_USER,
                   _("Problem adding/updating neighbor in `%s'\n"), "dv");

#ifdef DEBUG_DV
  GNUNET_hash_to_enc (&sender->hashPubKey, &from);
  GNUNET_hash_to_enc (&nmsg->neighbor.hashPubKey, &about);
  fprintf (stderr,
           "Received info about peer %s from directly connected peer %s\n",
           (char *) &about, (char *) &from);
#endif
  return ret;
}

/*
 * Handles a peer connect notification, eliminates any need for polling.
 * @param peer - ident of the connected peer
 * @param unused - unused closure arg
 */
static void
peer_connect_handler (const GNUNET_PeerIdentity * peer, void *unused)
{
#ifdef DEBUG_DV
  fprintf (stderr, "Entering peer_connect_handler:\n");
  print_tables();

#endif
	struct GNUNET_dv_neighbor *neighbor;
	unsigned int cost = GNUNET_DV_LEAST_COST;

	if (GNUNET_YES != GNUNET_multi_hash_map_contains(ctx->direct_neighbors,&peer->hashPubKey))
	{
		neighbor = GNUNET_malloc(sizeof (struct GNUNET_dv_neighbor));
		neighbor->cost = cost;
		neighbor->neighbor = GNUNET_malloc(sizeof(GNUNET_PeerIdentity));
		memcpy(neighbor->neighbor,peer, sizeof(GNUNET_PeerIdentity));
		GNUNET_multi_hash_map_put (ctx->direct_neighbors,&peer->hashPubKey,
					                                                 neighbor,
					                                                 GNUNET_MultiHashMapOption_REPLACE);
	}
	else
	{
		neighbor = GNUNET_multi_hash_map_get(ctx->direct_neighbors,&peer->hashPubKey);


		if (neighbor->cost != cost)
		{
			GNUNET_mutex_lock(ctx->dvMutex);
			GNUNET_multi_hash_map_put (ctx->direct_neighbors,&peer->hashPubKey,
			                                                 neighbor,
			                                                 GNUNET_MultiHashMapOption_REPLACE);
			GNUNET_mutex_unlock(ctx->dvMutex);
		}

	}

	addUpdateNeighbor(peer, NULL, cost);

#ifdef DEBUG_DV
	print_tables();
#endif
	return;

}

/*
 * May use as a callback for deleting nodes from heaps...
 */
static void
delete_callback(struct GNUNET_dv_neighbor *neighbor, struct GNUNET_dv_heap *root,GNUNET_PeerIdentity * toMatch)
{
	if (memcmp(neighbor->referrer, toMatch, sizeof(GNUNET_PeerIdentity)) == 0)
	{
		GNUNET_DV_Heap_removeNode(&ctx->neighbor_max_heap, neighbor);
		GNUNET_DV_Heap_removeNode(&ctx->neighbor_min_heap, neighbor);
		GNUNET_multi_hash_map_remove_all(ctx->extended_neighbors, &neighbor->neighbor->hashPubKey);

		GNUNET_free(neighbor->neighbor);
		GNUNET_free(neighbor->referrer);
		GNUNET_free(neighbor);
	}
	return;
}

/*
 * Handles the receipt of a peer disconnect notification.
 *
 * @param peer - the peer that has disconnected from us
 */
static void
peer_disconnect_handler (const GNUNET_PeerIdentity * peer, void *unused)
{
/*
 * Update for heap and hashmap structures.  Namely, replace linked list
 * iteration with hashmap lookup.  Will also need to traverse *both* heaps
 * to find and remove any peers that have peer as their referrer! A
 * callback implementation probably makes more sense...
 */
  struct GNUNET_dv_neighbor *neighbor;

#ifdef DEBUG_DV
  GNUNET_EncName myself;
  fprintf (stderr, "Entering peer_disconnect_handler\n");
  GNUNET_hash_to_enc (&peer->hashPubKey, &myself);
  fprintf (stderr, "disconnected peer: %s\n", (char *) &myself);
  print_tables();
#endif

  GNUNET_mutex_lock (ctx->dvMutex);

  if (GNUNET_YES == GNUNET_multi_hash_map_contains(ctx->direct_neighbors, &peer->hashPubKey))
	{
  	neighbor = GNUNET_multi_hash_map_get(ctx->direct_neighbors, &peer->hashPubKey);
  	if (neighbor != NULL)
  	{
  		GNUNET_multi_hash_map_remove_all(ctx->direct_neighbors, &peer->hashPubKey);

  		GNUNET_DV_Heap_Iterator (&delete_callback, &ctx->neighbor_max_heap, ctx->neighbor_max_heap.root, peer);

  		GNUNET_free(neighbor->neighbor);
			if (neighbor->referrer)
				GNUNET_free(neighbor->referrer);

			GNUNET_free(neighbor);

  	}
	}

  GNUNET_mutex_unlock (ctx->dvMutex);
#ifdef DEBUG_DV
  print_tables ();
  fprintf (stderr, "Exiting peer_disconnect_handler\n");
#endif
  return;
}

static struct GNUNET_dv_neighbor *
chooseToNeighbor ()
{
  if (GNUNET_multi_hash_map_size(ctx->direct_neighbors) == 0)
    return NULL;

  return (struct GNUNET_dv_neighbor *)GNUNET_multi_hash_map_get_random(ctx->direct_neighbors);
}

static struct GNUNET_dv_neighbor *
chooseAboutNeighbor ()
{
	if (ctx->neighbor_min_heap.size == 0)
    return NULL;

#ifdef DEBUG_DV
  fprintf (stderr, "Min heap size %d\nMax heap size %d\n", ctx->neighbor_min_heap.size,ctx->neighbor_max_heap.size);
#endif

  return GNUNET_DV_Heap_Walk_getNext(&ctx->neighbor_min_heap);

}

static void *
neighbor_send_thread (void *rcls)
{
#ifdef DEBUG_DV
  fprintf (stderr, "Entering neighbor_send_thread...\n");
  GNUNET_EncName encPeerAbout;
  GNUNET_EncName encPeerTo;
#endif
  struct GNUNET_dv_neighbor *about = NULL;
  struct GNUNET_dv_neighbor *to = NULL;

  p2p_dv_MESSAGE_NeighborInfo *message =
    GNUNET_malloc (sizeof (p2p_dv_MESSAGE_NeighborInfo));

  message->header.size = htons (sizeof (p2p_dv_MESSAGE_NeighborInfo));
  message->header.type = htons (GNUNET_P2P_PROTO_DV_NEIGHBOR_MESSAGE);


  while (!ctx->closing)
    {
      //updateSendInterval();
      about = chooseAboutNeighbor ();
      to = chooseToNeighbor ();

      if ((about != NULL) && (to != NULL)
          && (memcmp (about->neighbor, to->neighbor, sizeof (GNUNET_HashCode))
              != 0))
        {
#ifdef DEBUG_DV
          GNUNET_hash_to_enc (&about->neighbor->hashPubKey, &encPeerAbout);
          GNUNET_hash_to_enc (&to->neighbor->hashPubKey, &encPeerTo);
          fprintf (stderr,
                   "Sending info about peer %s to directly connected peer %s\n",
                   (char *) &encPeerAbout, (char *) &encPeerTo);
#endif
          message->cost = htonl (about->cost);
          memcpy (&message->neighbor, about->neighbor,
                  sizeof (GNUNET_PeerIdentity));
          coreAPI->ciphertext_send (to->neighbor, &message->header, 0,
                                    ctx->send_interval * GNUNET_CRON_MILLISECONDS);
        }

      GNUNET_thread_sleep (ctx->send_interval * GNUNET_CRON_MILLISECONDS);
    }

  GNUNET_free (message);
#ifdef DEBUG_DV
  fprintf (stderr, "Exiting neighbor_send_thread...\n");
#endif
  return NULL;
}

int
initialize_module_dv (GNUNET_CoreAPIForPlugins * capi)
{
  int ok = GNUNET_OK;
  unsigned long long max_hosts;
  ctx->dvMutex = GNUNET_mutex_create (GNUNET_NO);
  coreAPI = capi;
  GNUNET_GE_LOG (capi->ectx,
                 GNUNET_GE_DEBUG | GNUNET_GE_REQUEST | GNUNET_GE_USER,
                 _("`%s' registering P2P handler %d\n"),
                 "dv", GNUNET_P2P_PROTO_DV_NEIGHBOR_MESSAGE);



  if (GNUNET_SYSERR ==
      coreAPI->
      peer_disconnect_notification_register (&peer_disconnect_handler, NULL))
    ok = GNUNET_SYSERR;

  if (GNUNET_SYSERR ==
      coreAPI->
      peer_connect_notification_register (&peer_connect_handler, NULL))
    ok = GNUNET_SYSERR;

  if (GNUNET_SYSERR ==
      coreAPI->
      p2p_ciphertext_handler_register (GNUNET_P2P_PROTO_DV_NEIGHBOR_MESSAGE,
                                       &p2pHandleDVNeighborMessage))
    ok = GNUNET_SYSERR;


  sendingThread =
    GNUNET_thread_create (&neighbor_send_thread, &coreAPI, 1024 * 1);


  GNUNET_GC_get_configuration_value_number (coreAPI->cfg,
                                            "DV",
                                            "FISHEYEDEPTH",
                                            0, -1, 3, &ctx->fisheye_depth);

  GNUNET_GC_get_configuration_value_number (coreAPI->cfg,
                                            "DV",
                                            "TABLESIZE",
                                            0, -1, 100, &ctx->max_table_size);

  GNUNET_GC_get_configuration_value_number (coreAPI->cfg,
                                            "gnunetd","connection-max-hosts",1,-1,50,
                                            &max_hosts);

  ctx->direct_neighbors = GNUNET_multi_hash_map_create (max_hosts);
  if (ctx->direct_neighbors == NULL)
  {
  	ok = GNUNET_SYSERR;
  }

  ctx->extended_neighbors = GNUNET_multi_hash_map_create (ctx->max_table_size * 3);
  if (ctx->extended_neighbors == NULL)
	{
		ok = GNUNET_SYSERR;
	}

  GNUNET_GE_ASSERT (capi->ectx,
                    0 == GNUNET_GC_set_configuration_value_string (capi->cfg,
                                                                   capi->ectx,
                                                                   "ABOUT",
                                                                   "dv",
                                                                   _
                                                                   ("enables distance vector type routing (wip)")));

  return ok;
}

void
done_module_dv ()
{
  ctx->closing = 1;
  coreAPI->
    p2p_ciphertext_handler_unregister (GNUNET_P2P_PROTO_DV_NEIGHBOR_MESSAGE,
                                       &p2pHandleDVNeighborMessage);

  coreAPI->peer_disconnect_notification_unregister (&peer_disconnect_handler,
                                                    NULL);
  coreAPI->peer_disconnect_notification_unregister (&peer_connect_handler,
                                                      NULL);

  GNUNET_mutex_destroy (ctx->dvMutex);
  coreAPI = NULL;
}

/* end of dv.c */
