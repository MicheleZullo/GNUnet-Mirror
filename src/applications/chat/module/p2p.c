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
 * @file applications/chat/chat_p2p.c
 */
#include "p2p.h"

int
p2pHandleChatJoinRequest (struct GNUNET_ClientHandle *client,
                          const GNUNET_MessageHeader * message)
{
  return GNUNET_SYSERR;
}

int
p2pHandleChatMSG (struct GNUNET_ClientHandle *client,
                  const GNUNET_MessageHeader * message)
{
  return GNUNET_SYSERR;
}

int
p2pSendChatMSG (const GNUNET_MessageHeader * message)
{
  return GNUNET_SYSERR;
}

int
p2pGetRoomList ()
{
  return GNUNET_SYSERR;
}

int
p2pSendRoomList ()
{
  return GNUNET_SYSERR;
}
