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
 * CHAT CORE. This is the code that is plugged
 * into the GNUnet core to enable chatting.
 *
 * @author Christian Grothoff
 * @author Nathan Evans
 * @file applications/chat/chat.c
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "gnunet_util.h"
#include "gnunet_core.h"
#include "chat.h"

/**
 * Linked list of our current clients.
 */
struct GNUNET_CS_chat_client
{
  struct GNUNET_CS_chat_client *next;

  struct GNUNET_ClientHandle *client;

  struct GNUNET_RSA_PrivateKey *private_key;

  char *room;

  /**
   * Hash of the public key (for convenience).
   */
  GNUNET_HashCode id;

  unsigned int msg_options;

};

static struct GNUNET_CS_chat_client *client_list_head;

static GNUNET_CoreAPIForPlugins *coreAPI;

static struct GNUNET_Mutex *chatMutex;

static int
csHandleTransmitRequest (struct GNUNET_ClientHandle *client,
                         const GNUNET_MessageHeader * message)
{
  const CS_chat_MESSAGE_TransmitRequest *cmsg;
  CS_chat_MESSAGE_ReceiveNotification *rmsg;
  struct GNUNET_CS_chat_client *pos;
  const char *room;
  unsigned int msg_len;

  if (ntohs (message->size) < sizeof (CS_chat_MESSAGE_TransmitRequest))
    {
      GNUNET_GE_BREAK (NULL, 0);
      return GNUNET_SYSERR;     /* invalid message */
    }
  cmsg = (const CS_chat_MESSAGE_TransmitRequest *) message;
  msg_len = ntohs (message->size) - sizeof (CS_chat_MESSAGE_TransmitRequest);
  rmsg = GNUNET_malloc (sizeof (CS_chat_MESSAGE_ReceiveNotification) +
                        msg_len);
  rmsg->header.size = htons (sizeof (CS_chat_MESSAGE_ReceiveNotification) +
                             msg_len);
  rmsg->header.type = htons (GNUNET_CS_PROTO_CHAT_MESSAGE_NOTIFICATION);
  rmsg->msg_options = cmsg->msg_options;
  GNUNET_mutex_lock (chatMutex);

  pos = client_list_head;
  while ((pos != NULL) && (pos->client != client))
    pos = pos->next;
  if (pos == NULL)
    {
      GNUNET_mutex_unlock (chatMutex);
      GNUNET_GE_BREAK (NULL, 0);
      GNUNET_free (rmsg);
      return GNUNET_SYSERR;     /* not member of chat room! */
    }
  room = pos->room;
  if ((ntohl (cmsg->msg_options) & GNUNET_CHAT_MSG_ANONYMOUS) == 0)
    rmsg->sender = pos->id;
  else
    memset (&rmsg->sender, 0, sizeof (GNUNET_HashCode));
  pos = client_list_head;
  while (pos != NULL)
    {
      if (0 == strcmp (room, pos->room))
        {
          /* FIXME: private msg delivery, blocking options,
             confirmation */
          coreAPI->cs_send_message (pos->client, &rmsg->header, GNUNET_YES);
        }
      pos = pos->next;
    }
  GNUNET_mutex_unlock (chatMutex);
  GNUNET_free (rmsg);
  return GNUNET_OK;
}

static int
csHandleChatJoinRequest (struct GNUNET_ClientHandle *client,
                         const GNUNET_MessageHeader * message)
{
  const CS_chat_MESSAGE_JoinRequest *cmsg;
  char *room_name;
  const char *roomptr;
  unsigned int header_size;
  unsigned int meta_len;
  unsigned int room_name_len;
  struct GNUNET_CS_chat_client *entry;
  GNUNET_RSA_PublicKey pkey;
  CS_chat_MESSAGE_JoinNotification *nmsg;

  if (ntohs (message->size) < sizeof (CS_chat_MESSAGE_JoinRequest))
    {
      GNUNET_GE_BREAK (NULL, 0);
      return GNUNET_SYSERR;     /* invalid message */
    }
  cmsg = (const CS_chat_MESSAGE_JoinRequest *) message;
  header_size = ntohs (cmsg->header.size);
  room_name_len = ntohs (cmsg->room_name_len);
  if (header_size - sizeof (CS_chat_MESSAGE_JoinRequest) <= room_name_len)
    {
      GNUNET_GE_BREAK (NULL, 0);
      return GNUNET_SYSERR;
    }
  meta_len =
    header_size - sizeof (CS_chat_MESSAGE_JoinRequest) - room_name_len;

  roomptr = (const char *) &cmsg[1];
  room_name = GNUNET_malloc (room_name_len + 1);
  memcpy (room_name, roomptr, room_name_len);
  room_name[room_name_len] = '\0';

  entry = GNUNET_malloc (sizeof (struct GNUNET_CS_chat_client));
  memset (entry, 0, sizeof (struct GNUNET_CS_chat_client));
  entry->client = client;
  entry->room = room_name;
  entry->private_key = GNUNET_RSA_decode_key (&cmsg->private_key);
  if (entry->private_key == NULL)
    {
      GNUNET_GE_BREAK (NULL, 0);
      GNUNET_free (room_name);
      GNUNET_free (entry);
      return GNUNET_SYSERR;
    }
  GNUNET_RSA_get_public_key (entry->private_key, &pkey);
  GNUNET_hash (&pkey, sizeof (GNUNET_RSA_PublicKey), &entry->id);
  entry->msg_options = ntohl (cmsg->msg_options);


  nmsg = GNUNET_malloc (sizeof (CS_chat_MESSAGE_JoinNotification) + meta_len);
  nmsg->header.type = htons (GNUNET_CS_PROTO_CHAT_JOIN_NOTIFICATION);
  nmsg->header.size =
    htons (sizeof (CS_chat_MESSAGE_JoinNotification) + meta_len);
  nmsg->msg_options = cmsg->msg_options;
  nmsg->public_key = pkey;
  memcpy (&nmsg[1], &roomptr[room_name_len], meta_len);
  GNUNET_mutex_lock (chatMutex);
  entry->next = client_list_head;
  client_list_head = entry;
  while (entry != NULL)
    {
      if (0 == strcmp (room_name, entry->room))
        coreAPI->cs_send_message (entry->client, &nmsg->header, GNUNET_YES);
      entry = entry->next;
    }
  GNUNET_mutex_unlock (chatMutex);
  GNUNET_free (nmsg);
  return GNUNET_OK;
}

static void
chatClientExitHandler (struct GNUNET_ClientHandle *client)
{
  struct GNUNET_CS_chat_client *entry;
  struct GNUNET_CS_chat_client *pos;
  struct GNUNET_CS_chat_client *prev;
  CS_chat_MESSAGE_LeaveNotification lmsg;

  GNUNET_mutex_lock (chatMutex);
  pos = client_list_head;
  prev = NULL;
  while ((pos != NULL) && (pos->client != client))
    {
      prev = pos;
      pos = pos->next;
    }
  if (pos == NULL)
    {
      GNUNET_mutex_unlock (chatMutex);
      return;                   /* nothing to do */
    }
  if (prev == NULL)
    client_list_head = pos->next;
  else
    prev->next = pos->next;
  entry = client_list_head;
  lmsg.header.size = htons (sizeof (CS_chat_MESSAGE_LeaveNotification));
  lmsg.header.type = htons (GNUNET_CS_PROTO_CHAT_LEAVE_NOTIFICATION);
  lmsg.reserved = htonl (0);
  GNUNET_RSA_get_public_key (pos->private_key, &lmsg.user);
  while (entry != NULL)
    {
      if (0 == strcmp (entry->room, pos->room))
        coreAPI->cs_send_message (entry->client, &lmsg.header, GNUNET_YES);
      entry = entry->next;
    }
  GNUNET_mutex_unlock (chatMutex);
  GNUNET_free (pos->room);
  GNUNET_RSA_free_key (pos->private_key);
  GNUNET_free (pos);
}

int
initialize_module_chat (GNUNET_CoreAPIForPlugins * capi)
{
  int ok = GNUNET_OK;

  coreAPI = capi;
  GNUNET_GE_LOG (capi->ectx,
                 GNUNET_GE_DEBUG | GNUNET_GE_REQUEST | GNUNET_GE_USER,
                 _("`%s' registering CS handlers %d and %d\n"),
                 "chat",
                 GNUNET_CS_PROTO_CHAT_JOIN_REQUEST,
                 GNUNET_CS_PROTO_CHAT_TRANSMIT_REQUEST);

  if (GNUNET_SYSERR ==
      capi->cs_disconnect_handler_register (&chatClientExitHandler))
    ok = GNUNET_SYSERR;
  if (GNUNET_SYSERR ==
      capi->cs_handler_register (GNUNET_CS_PROTO_CHAT_JOIN_REQUEST,
                                 &csHandleChatJoinRequest))
    ok = GNUNET_SYSERR;
  if (GNUNET_SYSERR ==
      capi->cs_handler_register (GNUNET_CS_PROTO_CHAT_TRANSMIT_REQUEST,
                                 &csHandleTransmitRequest))
    ok = GNUNET_SYSERR;
  GNUNET_GE_ASSERT (capi->ectx,
                    0 == GNUNET_GC_set_configuration_value_string (capi->cfg,
                                                                   capi->ectx,
                                                                   "ABOUT",
                                                                   "chat",
                                                                   _
                                                                   ("enables P2P-chat (incomplete)")));
  chatMutex = GNUNET_mutex_create (GNUNET_NO);
  return ok;
}

void
done_module_chat ()
{
  coreAPI->cs_disconnect_handler_unregister (&chatClientExitHandler);
  coreAPI->cs_handler_unregister (GNUNET_CS_PROTO_CHAT_TRANSMIT_REQUEST,
                                  &csHandleTransmitRequest);
  coreAPI->cs_handler_unregister (GNUNET_CS_PROTO_CHAT_JOIN_REQUEST,
                                  &csHandleChatJoinRequest);
  GNUNET_mutex_destroy (chatMutex);
  coreAPI = NULL;
}


/* end of chat.c */
