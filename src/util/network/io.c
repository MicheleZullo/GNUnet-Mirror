/*
     This file is part of GNUnet.
     (C) 2003, 2006 Christian Grothoff (and other contributing authors)

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
 * @file util/network/io.c
 * @brief (network) input/output operations
 * @author Christian Grothoff
 */

#include "gnunet_util_network.h"
#include "platform.h"
#include "network.h"

/**
 * Global lock for gethostbyname.
 */
static struct MUTEX * lock;

static struct SignalHandlerContext * sctx;

static void catcher() {
}

void __attribute__ ((constructor)) gnunet_network_io_init() {
  lock = MUTEX_CREATE(NO);
  sctx = signal_handler_install(SIGPIPE, &catcher);
}

void __attribute__ ((destructor)) gnunet_network_io_fini() {
  MUTEX_DESTROY(lock);
  lock = NULL;
  signal_handler_uninstall(SIGPIPE, &catcher, sctx);
  sctx = NULL;
}

/**
 * Get the IP address of the given host.
 * @return OK on success, SYSERR on error
 */
int get_host_by_name(struct GE_Context * ectx,
		     const char * hostname,
                     IPaddr * ip) {
  struct hostent * he;

  /* slight hack: re-use config lock */
  MUTEX_LOCK(lock);
  he = GETHOSTBYNAME(hostname);
  if (he == NULL) {
    GE_LOG(ectx,
	   GE_ERROR | GE_ADMIN | GE_BULK,
	   _("Could not find IP of host `%s': %s\n"),
	   hostname,
	   hstrerror(h_errno));
    MUTEX_UNLOCK(lock);
    return SYSERR;
  }
  if (he->h_addrtype != AF_INET) {
    GE_BREAK(ectx, 0);
    MUTEX_UNLOCK(lock);
    return SYSERR;
  }
  memcpy(ip,
         &((struct in_addr*)he->h_addr_list[0])->s_addr,
         sizeof(struct in_addr));
  MUTEX_UNLOCK(lock);
  return OK;
}



struct SocketHandle * 
socket_create(struct GE_Context * ectx,
	      struct LoadMonitor * mon,
	      int osSocket) {
  SocketHandle * ret;

  ret = MALLOC(sizeof(SocketHandle));
  ret->ectx = ectx;
  ret->mon = mon;
  ret->handle = osSocket;
  return ret;
}

void socket_destroy(struct SocketHandle * s) {
  GE_ASSERT(NULL, s != NULL);
  if ( (0 != SHUTDOWN(s->handle,
		    SHUT_RDWR)) &&
       (errno != ENOTCONN) ) 
    GE_LOG_STRERROR(s->ectx,
		    GE_WARNING | GE_ADMIN | GE_BULK, 
		    "shutdown");
  if (0 != CLOSE(s->handle))
    GE_LOG_STRERROR(s->ectx,
		    GE_WARNING | GE_USER | GE_DEVELOPER | GE_BULK,
		    "close");
  FREE(s);
}

/* TODO: log errors! */
int socket_set_blocking(struct SocketHandle * s, 
			int doBlock) {
#if MINGW
  u_long l = !doBlock;
  if (ioctlsocket(s->handle, 
		  FIONBIO, &l) == SOCKET_ERROR) {
    SetErrnoFromWinsockError(WSAGetLastError());
    
    return -1;
  } else {
    /* store the blocking mode */
    __win_SetHandleBlockingMode(s->handle, doBlock);
    return 0;
  }
#else
  int flags = fcntl(s->handle, F_GETFL);
  if (doBlock)
    flags &= ~O_NONBLOCK;
  else
    flags |= O_NONBLOCK;
  return fcntl(s->handle,
	       F_SETFL,
	       flags);
#endif
}

int socket_test_blocking(struct SocketHandle * s)
{
#ifndef MINGW
 return (fcntl(s->handle, 
	       F_GETFL) & O_NONBLOCK) ? NO : YES;
#else
  return __win_IsHandleMarkedAsBlocking(s->handle);
#endif
}

int socket_recv(struct SocketHandle * s,
		NC_KIND nc,
		void * buf,
		size_t max,
		size_t * read) {
  int flags;
  size_t pos;
  size_t ret;

  socket_set_blocking(s, 
		      0 != (nc & NC_Blocking));
  flags = 0;
#ifdef CYGWIN
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#elif OSX
  /* anything? */
#elif SOMEBSD || SOLARIS
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
#elif LINUX
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#else
  /* good luck */
#endif
  pos = 0;
  do {
    ret = (size_t) RECV(s->handle,
			&((char*)buf)[pos],
			max - pos,
			flags);
    if ( (ret == (size_t) -1) &&
	 (errno == EINTR) &&
	 (0 != (nc & NC_IgnoreInt)) ) 
      continue;    
    if (ret == (size_t) -1) {
      if (errno == EINTR) {
	*read = pos;
	return YES;
      }
      if (errno == EWOULDBLOCK) {
	if (0 != (nc & NC_Blocking)) 
	  continue;	
	*read = pos;
	return (pos == 0) ? NO : YES;
      }
      GE_LOG_STRERROR(s->ectx,
		      GE_DEBUG | GE_USER | GE_REQUEST,
		      "recv");
      *read = pos;
      return SYSERR;
    }
    if (ret == 0) {
      /* most likely: other side closed connection */
      *read = pos;
      return SYSERR; 
    }
    if (s->mon != NULL)
      os_network_monitor_notify_transmission(s->mon,
					     Download,
					     ret);
    pos += ret;
  } while ( (pos < max) &&
	    (0 != (nc & NC_Blocking)) );
  *read = pos;
  return YES;
}

int socket_recv_from(struct SocketHandle * s,
		     NC_KIND nc,
		     void * buf,
		     size_t max,
		     size_t * read,
		     char * from,
		     unsigned int * fromlen) {
  int flags;
  size_t pos;
  size_t ret;

  socket_set_blocking(s, 
		      0 != (nc & NC_Blocking));
  flags = 0;
#ifdef CYGWIN
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#elif OSX
  /* anything? */
#elif SOMEBSD || SOLARIS
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
#elif LINUX
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#else
  /* good luck */
#endif
  pos = 0;
  do {
    ret = (size_t) RECVFROM(s->handle,
			    &((char*)buf)[pos],
			    max - pos,
			    flags,
			    (struct sockaddr*) from,
			    fromlen);
    if ( (ret == (size_t) -1) &&
	 (errno == EINTR) &&
	 (0 != (nc & NC_IgnoreInt)) )
      continue;
    if (ret == (size_t) -1) {
      if (errno == EINTR) {
	*read = pos;
	return YES;
      }
      if (errno == EWOULDBLOCK) {
	if (0 != (nc & NC_Blocking))
	  continue;
	*read = pos;
	return (pos == 0) ? NO : YES;
      }
      GE_LOG_STRERROR(s->ectx,
		      GE_DEBUG | GE_USER | GE_REQUEST,
		      "recv");
      *read = pos;
      return SYSERR;
    }
    if (ret == 0) {
      /* most likely: other side closed connection */
      *read = pos;
      return SYSERR; 
    }
    if (s->mon != NULL)
      os_network_monitor_notify_transmission(s->mon,
					     Download,
					     ret);
    pos += ret;
  } while ( (pos < max) &&
	    (0 != (nc & NC_Blocking)) );
  *read = pos;
  return YES;
}

int socket_send(struct SocketHandle * s,
		NC_KIND nc,
		const void * buf,
		size_t max,
		size_t * sent) {
  int flags;
  size_t pos;
  size_t ret;

  socket_set_blocking(s, 
		      0 != (nc & NC_Blocking));
  flags = 0;
#if SOMEBSD || SOLARIS
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
#elif OSX
  /* As braindead as Win32? */
#elif CYGWIN
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#elif LINUX
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#else
  /* pray */
#endif

  pos = 0;
  do {
    ret = (size_t) SEND(s->handle,
			&((char*)buf)[pos],
			max - pos,
			flags);
    if ( (ret == (size_t) -1) &&
	 (errno == EINTR) &&
	 (0 != (nc & NC_IgnoreInt)) )
      continue;
    if (ret == (size_t) -1) {
      if (errno == EINTR) {
	*sent = pos;
	return YES;
      }
      if (errno == EWOULDBLOCK) {
	if (0 != (nc & NC_Blocking))
	  continue;
	*sent = pos;
	return (pos == 0) ? NO : YES;
      }
      GE_LOG_STRERROR(s->ectx,
		      GE_DEBUG | GE_USER | GE_REQUEST,
		      "send");
      *sent = pos;
      return SYSERR;
    }
    if (ret == 0) {
      /* strange error; most likely: other side closed connection */
      *sent = pos;
      return SYSERR; 
    }
    if (s->mon != NULL)
      os_network_monitor_notify_transmission(s->mon,
					     Upload,
					     ret);
    pos += ret;
  } while ( (pos < max) &&
	    (0 != (nc & NC_Blocking)) );
  *sent = pos;
  return YES;
}

int socket_send_to(struct SocketHandle * s,
		   NC_KIND nc,
		   const void * buf,
		   size_t max,
		   size_t * sent,
		   const char * dst,
		   unsigned int dstlen) {
  int flags;
  size_t pos;
  size_t ret;

  socket_set_blocking(s, 
		      0 != (nc & NC_Blocking));
  flags = 0;
#if SOMEBSD || SOLARIS
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
#elif OSX
  /* As braindead as Win32? */
#elif CYGWIN
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#elif LINUX
  if (0 == (nc & NC_Blocking))
    flags |= MSG_DONTWAIT;
  if (0 == (nc & NC_IgnoreInt))
    flags |= MSG_NOSIGNAL;
#else
  /* pray */
#endif

  pos = 0;
  do {
    ret = (size_t) SENDTO(s->handle,
			  &((char*)buf)[pos],
			  max - pos,
			  flags,
			  (const struct sockaddr*) dst,
			  dstlen);
    if ( (ret == (size_t) -1) &&
	 (errno == EINTR) &&
	 (0 != (nc & NC_IgnoreInt)) )
      continue;
    if (ret == (size_t) -1) {
      if (errno == EINTR) {
	*sent = pos;
	return YES;
      }
      if (errno == EWOULDBLOCK) {
	if (0 != (nc & NC_Blocking))
	  continue;
	*sent = pos;
	return (pos == 0) ? NO : YES;
      }
      GE_LOG_STRERROR(s->ectx,
		      GE_DEBUG | GE_USER | GE_REQUEST,
		      "send");
      *sent = pos;
      return SYSERR;
    }
    if (ret == 0) {
      /* strange error; most likely: other side closed connection */
      *sent = pos;
      return SYSERR; 
    }
    if (s->mon != NULL)
      os_network_monitor_notify_transmission(s->mon,
					     Upload,
					     ret);
    pos += ret;
  } while ( (pos < max) &&
	    (0 != (nc & NC_Blocking)) );
  *sent = pos;
  return YES;
}

/**
 * Check if socket is valid
 * @return 1 if valid, 0 otherwise
 */
int socket_test_valid(struct SocketHandle * s) {
#ifndef MINGW
  struct stat buf;
  return -1 != fstat(s->handle, 
		     &buf);
#else
  long l;
  return ioctlsocket(s->handle, 
		     FIONREAD, 
		     &l) != SOCKET_ERROR;
#endif
}

/* end of io.c */
