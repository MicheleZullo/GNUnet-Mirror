/*
      This file is part of GNUnet
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
 * @file include/gnunet_remote_lib.h
 * @brief remote testing library for running gnunetd on multiple machines
 * @author Nathan Evans
 */

#ifndef GNUNET_REMOTE_LIB_H
#define GNUNET_REMOTE_LIB_H

#include "gnunet_util.h"

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

typedef enum
{
  GNUNET_REMOTE_CLIQUE,
  GNUNET_REMOTE_SMALL_WORLD,
  GNUNET_REMOTE_RING,
  GNUNET_REMOTE_2D_TORUS,
  GNUNET_REMOTE_ERDOS_RENYI,
  GNUNET_REMOTE_NONE,

} GNUNET_REMOTE_TOPOLOGIES;

/**
 * Linked list of information about daemon processes.
 */
struct GNUNET_REMOTE_TESTING_DaemonContext
{
  struct GNUNET_REMOTE_TESTING_DaemonContext *next;
  struct GNUNET_GC_Configuration *config;
  GNUNET_PeerIdentity *peer;
  unsigned short port;
  char *pid;
  char *hostname;
  char *username;
};

/**
 * Starts a single gnunet daemon on a remote machine
 *
 * @param gnunetd_home directory where gnunetd is on remote machine
 * @param localConfigPath local configuration path for config file
 * @param remote_config_path remote path to copy local config to
 * @param configFileName  file to copy and use on remote machine
 * @param ip_address ip address of remote machine
 * @param username username to use for ssh (assumed to be used with ssh-agent)
 */
int GNUNET_REMOTE_start_daemon (char *gnunetd_home,
                                char *localConfigPath, char *configFileName,
                                char *remote_config_path, char *ip_address,
                                char *username,
                                char *remote_friend_file_path);

/**
 * Main start function to be called.  Needs a remote config specified, as well
 * as the number of daemons to start and the type of topology.  Available topology
 * types are defined in gnunet_remote_lib.h  Returns a linked list of hosts started
 */
struct GNUNET_REMOTE_TESTING_DaemonContext
  *GNUNET_REMOTE_start_daemons (struct GNUNET_GC_Configuration *newcfg,
                                unsigned long long number_of_daemons);

int
GNUNET_REMOTE_kill_daemon (struct GNUNET_REMOTE_TESTING_DaemonContext
                           *tokill);

#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

#endif
