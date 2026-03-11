/*
 * IMC2 - an inter-mud communications protocol
 *
 * imc.c: the core protocol code
 *
 * Copyright (C) 1996 Oliver Jowett <oliver@sa-search.massey.ac.nz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#define IMC_INTERNALS
#include "imc.h"
#include <time.h>

/*
 *  Local declarations + some global stuff from imc.h
 */

/* decls of vars from imc.h */

_imc imc[IMC_MAX];
_imc_info imc_info[IMC_MAX];

imc_statistics imc_stats;

imc_reminfo *imc_remoteinfo;

int imc_active;
time_t imc_now;

/* control socket for accepting connections */
static int control;

/* sequence memory */

_imc_memory imc_memory[IMC_MEMORY];

/* imc flags info */

struct
{
  char *name;			/* flag name */
  int value;			/* bit value */
}
imc_flags[] =
{
  { "noauto", IMC_NOAUTO },
  { "client", IMC_CLIENT },
  { "reconnect", IMC_RECONNECT },
  { "broadcast", IMC_BROADCAST },
  { "deny", IMC_DENY },
  { NULL, 0 },
};

unsigned long imc_sequencenumber;	  /* sequence# for outgoing packets */
static char lasterror[IMC_DATA_LENGTH];	  /* last error reported */

char *imc_name;			          /* our IMC name */

/*
 *  imc_reminfo handling
 */

/* find an info entry for "name" */
imc_reminfo *imc_find_reminfo(const char *name)
{
  imc_reminfo *p;

  for (p = imc_remoteinfo; p; p = p->next)
    if (!strcasecmp(name, p->name))
      return p;

  return NULL;
}

/* create a new info entry */
imc_reminfo *imc_new_reminfo(void)
{
  imc_reminfo *p;

  p=imc_malloc(sizeof(imc_reminfo));

  p->name    = NULL;
  p->version = NULL;
  p->route   = NULL;
  p->alive   = 0;
  p->ping    = 0;
  p->next    = imc_remoteinfo;

  imc_remoteinfo=p;
  return p;
}

/* delete the info entry "p" */
void imc_delete_reminfo(imc_reminfo *p)
{
  imc_reminfo *last;

  if (!imc_remoteinfo || !p)
    return;

  if (p == imc_remoteinfo)
    imc_remoteinfo = p->next;
  else
  {
    for (last=imc_remoteinfo; last && last->next != p; last=last->next)
      ;
    if (!last)
      return;
    last->next=p->next;
  }

  imc_strfree(p->name);
  imc_strfree(p->version);
  imc_strfree(p->route);
  imc_free(p, sizeof(imc_reminfo));
}

/* update our routing table based on a packet received with path "path" */
static void updateroutes(const char *path)
{
  imc_reminfo *p;
  const char *sender, *last;
  const char *temp;

  /* loop through each item in the path, and update routes to there */

  last = imc_lastinpath(path);
  temp = path;
  while (temp && temp[0])
  {
    sender=imc_firstinpath(temp);

    if (strcasecmp(sender, imc_name))
    {
      /* not from us */
      /* check if its in the list already */

      p = imc_find_reminfo(sender);
      if (!p)			/* not in list yet, create a new entry */
      {
	p=imc_new_reminfo();

	p->name    = imc_strdup(sender);
	p->ping    = 0;
	p->alive   = imc_now;
	p->route   = imc_strdup(last);
	p->version = imc_strdup("unknown");
      }
      else
      {				/* already in list, update the entry */
	if (strcasecmp(last, p->route))
	{
	  imc_strfree(p->route);
	  p->route=imc_strdup(last);
	}
	p->alive=imc_now;
      }
    }

    /* get the next item in the path */

    temp=strchr(temp, '!');
    if (temp)
      temp++;			/* skip to just after the next '!' */
  }
}

/*
 * Key/value manipulation
 */

/* get the value of "key" from "p"; if it isn't present, return "def" */
const char *imc_getkey(const imc_data * p, const char *key, const char *def)
{
  int i;

  for (i=0; i<IMC_MAX_KEYS; i++)
    if (p->key[i] && !strcasecmp(p->key[i], key))
      return p->value[i];

  return def;
}

/* identical to imc_getkey, except get the integer value of the key */
int imc_getkeyi(const imc_data *p, const char *key, int def)
{
  int i;

  for (i=0; i<IMC_MAX_KEYS; i++)
    if (p->key[i] && !strcasecmp(p->key[i], key))
      return atoi(p->value[i]);

  return def;
}

/* add "key=value" to "p" */
void imc_addkey(imc_data *p, const char *key, const char *value)
{
  int i;

  for (i=0; i<IMC_MAX_KEYS; i++)
    if (p->key[i] && !strcasecmp(key, p->key[i]))
    {
      imc_strfree(p->key[i]);
      imc_strfree(p->value[i]);
      p->key[i]   = NULL;
      p->value[i] = NULL;
      break;
    }

  if (!value)
    return;

  for (i=0; i<IMC_MAX_KEYS; i++)
    if (!p->key[i])
    {
      p->key[i]   = imc_strdup(key);
      p->value[i] = imc_strdup(value);
      return;
    }
}

/* add "key=value" for an integer value */
void imc_addkeyi(imc_data *p, const char *key, int value)
{
  char temp[20];
  sprintf(temp, "%d", value);
  imc_addkey(p, key, temp);
}

/* clear all keys in "p" */
void imc_initdata(imc_data *p)
{
  int i;

  for (i=0; i<IMC_MAX_KEYS; i++)
  {
    p->key[i]   = NULL;
    p->value[i] = NULL;
  }
}

/* free all the keys in "p" */
void imc_freedata(imc_data * p)
{
  int i;

  for (i=0; i<IMC_MAX_KEYS; i++)
  {
    if (p->key[i])
      imc_strfree(p->key[i]);
    if (p->value[i])
      imc_strfree(p->value[i]);
  }
}

/*
 *  Error logging
 */

/* log a string */
void imc_logstring(const char *format, ...)
{
  char buf[IMC_DATA_LENGTH];
  va_list ap;

  va_start(ap, format);
  vsnprintf(buf, IMC_DATA_LENGTH, format, ap);
  va_end(ap);

  imc_log(buf);
}

/* log an error (log string and copy to lasterror) */
void imc_logerror(const char *format,...)
{
  va_list ap;

  va_start(ap, format);
  vsnprintf(lasterror, IMC_DATA_LENGTH, format, ap);
  va_end(ap);

  imc_log(lasterror);
}

/* log an error quietly (just copy to lasterror) */
void imc_qerror(const char *format,...)
{
  va_list ap;

  va_start(ap, format);
  vsnprintf(lasterror, IMC_DATA_LENGTH, format, ap);
  va_end(ap);
}

/* log a system error (log string, ": ", string representing errno)   */
/* this is particularly broken on SunOS (which doesn't have strerror) */
void imc_lerror(const char *format,...)
{
  va_list ap;

  va_start(ap, format);
  vsnprintf(lasterror, IMC_DATA_LENGTH, format, ap);
  strcat(lasterror, ": ");
  strcat(lasterror, strerror(errno));

  imc_log(lasterror);
}



/*
 *  String manipulation functions, mostly exported
 */

/* lowercase what */
void imc_slower(char *what)
{
  char *p=what;
  while (*p)
  {
    *p=tolower(*p);
    p++;
  }
}

/* copy src->dest, max count, null-terminate */
void imc_sncpy(char *dest, const char *src, int count)
{
  strncpy(dest, src, count-1);
  dest[count-1] = 0;
}

/* return 'mud' from 'player@mud' */
const char *imc_mudof(const char *fullname)
{
  static char buf[IMC_MNAME_LENGTH];
  char *where;

  where=strchr(fullname, '@');
  if (!where)
    imc_sncpy(buf, fullname, IMC_MNAME_LENGTH);
  else
    imc_sncpy(buf, where+1, IMC_MNAME_LENGTH);

  return buf;
}

/* return 'player' from 'player@mud' */
const char *imc_nameof(const char *fullname)
{
  static char buf[IMC_PNAME_LENGTH];
  char *where=buf;
  int count=0;

  while (*fullname && *fullname != '@' && count < IMC_PNAME_LENGTH-1)
    *where++=*fullname++, count++;

  *where = 0;
  return buf;
}

/* return 'player@mud' from 'player' and 'mud' */
const char *imc_makename(const char *player, const char *mud)
{
  static char buf[IMC_NAME_LENGTH];

  imc_sncpy(buf, player, IMC_PNAME_LENGTH);
  strcat(buf, "@");
  imc_sncpy(buf + strlen(buf), mud, IMC_MNAME_LENGTH);
  return buf;
}

/* return 'e' from 'a!b!c!d!e' */
const char *imc_lastinpath(const char *path)
{
  const char *where;
  static char buf[IMC_NAME_LENGTH];

  where=path + strlen(path)-1;
  while (*where != '!' && where >= path)
    where--;

  imc_sncpy(buf, where+1, IMC_NAME_LENGTH);
  return buf;
}

/* return 'a' from 'a!b!c!d!e' */
const char *imc_firstinpath(const char *path)
{
  static char buf[IMC_NAME_LENGTH];
  char *p;

  for (p=buf; *path && *path != '!'; *p++=*path++)
    ;

  *p=0;
  return buf;
}

/*  imc_getarg: extract a single argument (with given max length) from
 *  argument to arg; if arg==NULL, just skip an arg, don't copy it out
 */
const char *imc_getarg(const char *argument, char *arg, int length)
{
  int len = 0;

  while (*argument && isspace(*argument))
    argument++;

  if (arg)
    while (*argument && !isspace(*argument) && len < length-1)
      *arg++=*argument++, len++;
  else
    while (*argument && !isspace(*argument))
      argument++;

  while (*argument && !isspace(*argument))
    argument++;

  if (*argument && isspace(*argument))
    argument++;

  if (arg)
    *arg = 0;

  return argument;
}

/* return 1 if 'name' is a part of 'path'  (internal) */
static int inpath(const char *path, const char *name)
{
  char buf[IMC_MNAME_LENGTH+3];
  char tempn[IMC_MNAME_LENGTH], tempp[IMC_PATH_LENGTH];

  imc_sncpy(tempn, name, IMC_MNAME_LENGTH);
  imc_sncpy(tempp, path, IMC_PATH_LENGTH);
  imc_slower(tempn);
  imc_slower(tempp);

  if (!strcmp(tempp, tempn))
    return 1;

  sprintf(buf, "%s!", tempn);
  if (!strncmp(tempp, buf, strlen(buf)))
    return 1;

  sprintf(buf, "!%s", tempn);
  if (strlen(buf) < strlen(tempp) &&
      !strcmp(tempp + strlen(tempp) - strlen(buf), buf))
    return 1;

  sprintf(buf, "!%s!", tempn);
  if (strstr(tempp, buf))
    return 1;

  return 0;
}


/*
 *  Flag interpretation
 */

/* return the name of a particular set of flags */
const char *imc_flagname(int value)
{
  static char buf[200];
  int i;

  buf[0]=0;

  for (i=0; imc_flags[i].name; i++)
    if (value & imc_flags[i].value)
    {
      strcat(buf, imc_flags[i].name);
      strcat(buf, " ");
    }

  if (buf[0])
    buf[strlen(buf)-1] = 0;
  else
    strcpy(buf, "none");

  return buf;
}

/* return the value corresponding to a set of names */
int imc_flagvalue(const char *name)
{
  char buf[20];
  int i;
  int value = 0;

  while (1)
  {
    name=imc_getarg((char *) name, buf, 20);
    if (!buf[0])
      return value;

    for (i=0; imc_flags[i].name; i++)
      if (!strcasecmp(imc_flags[i].name, buf))
	value |= imc_flags[i].value;
  }
}


/*
 * Utility functions
 */

/* get index of given mud */
int imc_getindex(const char *mud)
{
  int i;

  for (i=0; i<IMC_MAX; i++)
    if (imc_info[i].inuse && !strcasecmp(mud, imc_info[i].name))
      return i;

  return -1;
}

/* get name of descriptor */
const char *imc_getdescname(int i)
{
  static char buf[IMC_NAME_LENGTH];
  char *n;

  if (imc[i].info != -1)
    n = imc_info[imc[i].info].name;
  else
    n = "unknown";

  sprintf(buf, "%s[%d]", n, i);
  return buf;
}


/*
 *  Core functions (all internal)
 */

/* accept a connection on the control port */
static void do_accept()
{
  int d;
  int i;
  struct sockaddr_in sa;
  int size = sizeof(sa);
  int r;

  d=accept(control, (struct sockaddr *) &sa, &size);
  if (d<0)
  {
    imc_lerror("accept");
    return;
  }

  r=fcntl(d, F_GETFL, 0);
  if (r<0 || fcntl(d, F_SETFL, O_NONBLOCK | r)<0)
  {
    imc_lerror("do_accept: fcntl");
    close(d);
    return;
  }

  for (i=0; i<IMC_MAX; i++)
    if (!imc[i].inuse)
      break;

  if (i==IMC_MAX)
  {
    imc_logerror("out of descriptors for accept");
    close(d);
    return;
  }

  imc[i].inuse    = 1;
  imc[i].state    = IMC_WAIT1;
  imc[i].desc     = d;
  imc[i].inbuf    = imc_malloc(IMC_BUFFER_SIZE);
  imc[i].outbuf   = imc_malloc(IMC_BUFFER_SIZE);
  imc[i].inbuf[0] = imc[i].outbuf[0] = 0;
  imc[i].info     = -1;

  imc_logstring("connection from %s:%d on descriptor %d",
		inet_ntoa(sa.sin_addr), ntohs(sa.sin_port), i);
}

/* close given connection */
static void do_close(int i)
{
  close(imc[i].desc);
  imc[i].inuse=0;
  if (imc[i].state == IMC_CONNECTED)
    imc_info[imc[i].info].connected=0;

  /* handle reconnects */
  if (imc[i].info != -1)
    if ((imc_info[imc[i].info].flags & IMC_RECONNECT) &&
	!(imc_info[imc[i].info].flags & IMC_DENY) &&
	!(imc_info[imc[i].info].flags & IMC_CLIENT))
    {
      if (imc[i].state == IMC_CONNECTED)	/* first reconnect is quick */
	imc_info[imc[i].info].timer=time(NULL) + IMC_RECONNECT_TIME_1;
      else
	imc_info[imc[i].info].timer=time(NULL) + IMC_RECONNECT_TIME;

      /*  add a bit of randomness so we don't get too many simultaneous
       *  reconnects
       */

      imc_info[imc[i].info].timer += (rand()%41)-20;
    }

  imc_free(imc[i].inbuf, IMC_BUFFER_SIZE);
  imc_free(imc[i].outbuf, IMC_BUFFER_SIZE);

  /* only log after we've done _everything_, in case imc_logstring sends
   * packets itself (problems with eg. output buffer overflow). Note that
   * imc_getdescname is okay because imc[i].info hasn't been clobbered..
   */
  imc_logstring("%s: closing link", imc_getdescname(i));
}

/* read waiting data from descriptor */
static void do_read(int i)
{
  int size;
  int r;

  size=strlen(imc[i].inbuf);

  if (size == IMC_BUFFER_SIZE)
  {
    imc_logerror("%s: input buffer overflow", imc_getdescname(i));
    do_close(i);
    return;
  }

  r=read(imc[i].desc, imc[i].inbuf+size, IMC_BUFFER_SIZE-size);
  if (!r || (r<0 && errno != EAGAIN))
  {
    if (r<0)                    /* read error */
      imc_lerror("%s: read", imc_getdescname(i));
    else                        /* socket was closed */
      imc_logerror("%s: read: EOF", imc_getdescname(i));
    do_close(i);
    return;
  }

  if (r<0)			/* EAGAIN error */
    return;

  imc[i].inbuf[size+r]=0;	/* terminate buffer */
  imc_stats.rx_bytes += r;
}

/* write to descriptor */
static void do_write(int i)
{
  int size, w;

  if (imc[i].state == IMC_CONNECTING)
  {
    /* Wait for server password */
    imc[i].state=IMC_WAIT2;
    return;
  }

  size = strlen(imc[i].outbuf);
  if (!size)			/* nothing to write */
    return;

  w=write(imc[i].desc, imc[i].outbuf, size);
  if (!w || (w<0 && errno != EAGAIN))
  {
    if (w<0)			/* write error */
      imc_lerror("%s: write", imc_getdescname(i));
    else			/* socket was closed */
      imc_logerror("%s: write: EOF", imc_getdescname(i));
    do_close(i);
    return;
  }

  if (w<0)			/* EAGAIN */
    return;

  /* throw away data we wrote */
  memmove(imc[i].outbuf, imc[i].outbuf+w, size-w+1);
  imc_stats.tx_bytes += w;
}

/* put a line onto descriptors output buffer */
static void do_send(int i, const char *line)
{
  if (!imc[i].inuse)
  {
    imc_logerror("BUG: do_send with !inuse desc %d", i);
    return;
  }

  imc_debug(i, 1, line);	/* log outgoing traffic */

  if (strlen(imc[i].outbuf) + strlen(line) >= IMC_BUFFER_SIZE-3)
  {
    imc_logerror("%s: output buffer overflow", imc_getdescname(i));
    do_close(i);
    return;
  }

  strcat(imc[i].outbuf, line);
  strcat(imc[i].outbuf, "\n\r");
}

/*  try to read a line from the input buffer, NULL if none ready
 *  all lines are \n\r terminated in theory, but take other combinations
 */
static const char *imc_getline(char *buffer)
{
  int i;
  static char buf[IMC_PACKET_LENGTH];

  /* copy until \n, \r, end of buffer, or out of space */
  for (i=0; buffer[i] && buffer[i] != '\n' && buffer[i] != '\r' &&
       i+1 < IMC_PACKET_LENGTH; i++)
    buf[i] = buffer[i];

  /* end of buffer and we haven't hit the maximum line length */
  if (!buffer[i] && i+1 < IMC_PACKET_LENGTH)
    return NULL;		/* so no line available */

  /* terminate return string */
  buf[i]=0;

  /* strip off extra control codes */
  while (buffer[i] && (buffer[i] == '\n' || buffer[i] == '\r'))
    i++;

  /* remove the line from the input buffer */
  memmove(buffer, buffer+i, strlen(buffer+i) + 1);

  return buf;
}

/* checkrepeat: check for repeats in the memory table */
static int checkrepeat(const char *mud, unsigned long seq)
{
  int i;

  /* look for a repeated entry, timer==0 indicates end-of-table */
  for (i=0; i<IMC_MEMORY && imc_memory[i].timer; i++)
    if (!strcasecmp(mud, imc_memory[i].from) && seq == imc_memory[i].sequence)
      return 1;

  /* not a repeat, so log it */

  if (i == IMC_MEMORY)		/* out of space, throw away the oldest entry */
  {
    imc_strfree(imc_memory[0].from);
    memmove(imc_memory, imc_memory+1,
	    sizeof(imc_memory)-sizeof(imc_memory[0]));
    i--;
  }

  imc_memory[i].timer    = imc_now + IMC_MEMORY_TIMEOUT;  /* set expiry time */
  imc_memory[i].from     = imc_strdup(mud);
  imc_memory[i].sequence = seq;

  return 0;
}

/* expire old memory entries */
static void expire(void)
{
  int i;

  /*  work out how many should expire, and free the strings in preparation for
   *  the move
   */
  for (i=0; i<IMC_MEMORY && imc_memory[i].timer &&
	 imc_memory[i].timer < imc_now; i++)
  {
    imc_strfree(imc_memory[i].from);
    imc_memory[i].timer = 0;
  }

  if (!i)
    return;			/* nothing to move */

  /* move memory, reset timers on unused slots */
  memmove(imc_memory, imc_memory+i,
	  sizeof(imc_memory[0])*IMC_MEMORY - sizeof(imc_memory[0])*i);

  for (i=IMC_MEMORY-i; i<IMC_MEMORY; i++)
    imc_memory[i].timer = 0;
}

/* send a packet to a descriptor using the right version */
static void do_send_packet(int i, const imc_internal *p)
{
  const char *output;
  int v;

  v=imc[imc_info[i].index].version;
  if (v>IMC_VERSION)
    v=IMC_VERSION;

  output=(*imc_vinfo[v].generate)(p);

  if (output)
  {
    imc_stats.tx_pkts++;
    do_send(imc_info[i].index, output);
  }
}

/* forward a packet - main routing function, all packets pass through here */
static void forward(const imc_internal * p)
{
  int i;
  imc_packet tomud;
  int broadcast, isbroadcast;
  const char *to;
  imc_reminfo *route;
  int direct;

  /* check for duplication, and register the packet in the sequence memory */

  if (p->sequence && checkrepeat(imc_mudof(p->from), p->sequence))
    return;

  /* check for packets we've already forwarded */

  if (inpath(p->path, imc_name))
    return;

  /* update our routing info */

  updateroutes(p->path);

  /* forward to our mud if it's for us */

  if (!strcmp(imc_mudof(p->to), "*") ||
      !strcasecmp(imc_mudof(p->to), imc_name))
  {
    strcpy(tomud.to, imc_nameof(p->to));    /* strip the name from the 'to' */
    strcpy(tomud.from, p->from);
    strcpy(tomud.type, p->type);
    tomud.data=p->data;

    imc_recv(&tomud);
  }

  /* if its only to us (ie. not broadcast) don't forward it */
  if (!strcasecmp(imc_mudof(p->to), imc_name))
    return;

  /* convert a specific destination to a broadcast in some cases */

  to=imc_mudof(p->to);

  isbroadcast=!strcmp(to, "*");	  /* broadcasts are, well, broadcasts */
  broadcast=1;		          /* unless we know better, flood packets */
  i=0;  			  /* make gcc happy */
  direct=-1;			  /* no direct connection to send on */

  /* convert 'to' fields that we have a route for to a hop along the route */

  if (!isbroadcast &&
      (route=imc_find_reminfo(to)) != NULL &&
      route->route != NULL &&
      !inpath(p->path, route->route))	/* avoid circular routing */
  {
    /*  check for a direct connection: if we find it, and the route isn't
     *  to it, then the route is a little suspect.. also send it direct
     */
    if (strcasecmp(to, route->route) &&
	(i=imc_getindex(to)) != -1 &&
	imc_info[i].connected)
      direct=i;
    to=route->route;
  }

  /* check for a direct connection */

  if (!isbroadcast &&
      (i=imc_getindex(to)) != -1 &&
      imc_info[i].connected &&
      !(imc_info[i].flags & IMC_BROADCAST))
    broadcast=0;

  if (broadcast)
  {				/* need to forward a packet */
    for (i=0; i<IMC_MAX; i++)
      if (imc_info[i].connected)
      {
	/* don't forward to sites that have already received it,
	 * or sites that don't need this packet
	 */
	if (inpath(p->path, imc_info[i].name) ||
	    (p->stamp & imc_info[i].noforward)!=0)
	  continue;

	do_send_packet(i, p);
      }
  }
  else
    /* forwarding to a specific connection */
  {
    /* but only if they haven't seen it (sanity check) */
    if (!inpath(p->path, imc_info[i].name))
      do_send_packet(i, p);

    /* send on direct connection, if we have one */
    if (direct >= 0 && direct != i && !inpath(p->path, imc_info[direct].name))
      do_send_packet(direct, p);
  }
}

/* handle a password from a client */
static void clientpassword(int i, const char *argument)
{
  char arg1[3], name[IMC_MNAME_LENGTH], pw[IMC_PW_LENGTH], version[20];
  int index;
  char response[IMC_PACKET_LENGTH];

  argument=imc_getarg(argument, arg1, 3);      /* packet type (has to be PW) */
  argument=imc_getarg(argument, name, IMC_MNAME_LENGTH);  /* remote mud name */
  argument = imc_getarg(argument, pw, IMC_PW_LENGTH);	         /* password */
  argument = imc_getarg(argument, version, 20);	/* optional version=n string */

  if (strcasecmp(arg1, "PW"))
  {
    imc_logstring("%s: non-PW password packet", imc_getdescname(i));
    do_close(i);
    return;
  }

  /* do we know them, and do they have the right password? */
  index = imc_getindex(name);
  if (index == -1 || strcmp(imc_info[index].clientpw, pw))
  {
    imc_logstring("%s: password failure for %s", imc_getdescname(i), name);
    do_close(i);
    return;
  }

  /* deny access if deny flag is set (good for eg. muds that start crashing
   * on rwho)
   */
  if (imc_info[index].flags & IMC_DENY)
  {
    imc_logstring("%s: denying connection", name);
    do_close(i);
    return;
  }

  if (imc_info[index].connected)	/* kill old connections */
    do_close(imc_info[index].index);

  /* register them */
  imc_info[index].connected = 1;
  imc_info[index].index     = i;

  imc[i].state          = IMC_CONNECTED;
  imc[i].info           = index;
  imc[i].spamcounter1   = 0;
  imc[i].spamcounter2   = 0;

  /* check for a version string (assume version 0 if not present) */
  imc_slower(version);
  if (!strncmp(version, "version=", 8))
    imc[i].version=atoi(version + 8);
  else
    imc[i].version=0;

  /* check for generator/interpreter */
  if (!imc_vinfo[imc[i].version].generate ||
      !imc_vinfo[imc[i].version].interpret)
  {
    imc_logstring("%s: unsupported version %d",
		  imc_getdescname(i), imc[i].version);
    do_close(i);
    return;
  }

  /* send our response */

  sprintf(response, "PW %s %s version=%d",
	  imc_name, imc_info[index].serverpw, IMC_VERSION);
  do_send(i, response);

  imc_logstring("%s: connected (version %d)",
		imc_getdescname(i), imc[i].version);
}

/* handle a password response from a server */
static void serverpassword(int i, const char *argument)
{
  char arg1[3], name[IMC_MNAME_LENGTH], pw[IMC_PW_LENGTH], version[20];
  int index;

  argument=imc_getarg(argument, arg1, 3);	/* has to be PW */
  argument=imc_getarg(argument, name, IMC_MNAME_LENGTH);
  argument=imc_getarg(argument, pw, IMC_PW_LENGTH);
  argument=imc_getarg(argument, version, 20);

  if (strcasecmp(arg1, "PW"))
  {
    imc_logstring("%s: non-PW password packet", imc_getdescname(i));
    do_close(i);
    return;
  }

  index=imc_getindex(name);
  if (index == -1 || strcmp(imc_info[index].serverpw, pw) ||
      index != imc[i].info)
  {
    imc_logstring("%s: password failure for %s", imc_getdescname(i), name);
    do_close(i);
    return;
  }

  if (imc_info[index].connected)	/* kill old connections */
    do_close(imc_info[index].index);

  imc_info[index].connected = 1;
  imc_info[index].index     = i;

  imc[i].state              = IMC_CONNECTED;
  imc[i].spamcounter1       = 0;
  imc[i].spamcounter2       = 0;

  imc_slower(version);
  if (!strncmp(version, "version=", 8))
    imc[i].version = atoi(version + 8);
  else
    imc[i].version = 0;

  /* check for generator/interpreter */
  if (!imc_vinfo[imc[i].version].generate ||
      !imc_vinfo[imc[i].version].interpret)
  {
    imc_logstring("%s: unsupported version %d",
		  imc_getdescname(i), imc[i].version);
    do_close(i);
    return;
  }

  imc_logstring("%s: connected (version %d)",
		imc_getdescname(i), imc[i].version);
}

/* low-level startup of imc (set active, set up listen socket, etc) */
int imc_ll_startup(const char *mudname, int port)
{
  struct sockaddr_in sa;
  int i;

  if (imc_active)
  {
    imc_logstring("imc_ll_startup: called when IMC already active");
    return 0;
  }

  imc_sequencenumber=(unsigned long)time(NULL);
  strcpy(lasterror, "no error");

  imc_name = imc_strdup(mudname);
  imc_logstring("IMC initialising for %s on port %d", imc_name, port);

  control = socket(AF_INET, SOCK_STREAM, 0);
  if (control<0)
  {
    imc_lerror("imc_ll_startup: socket");
    return 0;
  }

  i=1;
  if (setsockopt(control, SOL_SOCKET, SO_REUSEADDR, (void *)&i, sizeof(i))<0)
  {
    imc_lerror("imc_ll_startup: SO_REUSEADDR");
    close(control);
    return 0;
  }

  sa.sin_family      = AF_INET;
  sa.sin_port        = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(control, (struct sockaddr *)&sa, sizeof(sa))<0)
  {
    imc_lerror("imc_ll_startup: bind");
    close(control);
    return 0;
  }

  if (listen(control, 1)<0)
  {
    imc_lerror("imc_ll_startup: listen");
    close(control);
    return 0;
  }

  for (i=0; i<IMC_MAX; i++)
    imc[i].inuse=0;

  imc_active=1;

  for (i=0; i<IMC_MAX; i++)
    if (imc_info[i].inuse && !(imc_info[i].flags & IMC_NOAUTO) &&
	!(imc_info[i].flags & IMC_CLIENT) && !(imc_info[i].flags & IMC_DENY))
      imc_connect(imc_info[i].name);

  imc_stats.start    = imc_now;
  imc_stats.rx_pkts  = 0;
  imc_stats.tx_pkts  = 0;
  imc_stats.rx_bytes = 0;
  imc_stats.tx_bytes = 0;

  return 1;
}

/* close down low-level imc */
void imc_ll_shutdown()
{
  int i;

  if (!imc_active)
  {
    imc_logstring("imc_ll_shutdown: called when !imc_active");
    return;
  }

  imc_logstring("IMC terminating");
  imc_logstring("rx %ld packets, %ld bytes (%ld/second)",
		imc_stats.rx_pkts,
		imc_stats.rx_bytes,
		(imc_now == imc_stats.start) ? 0 :
		imc_stats.rx_bytes / (imc_now - imc_stats.start));
  imc_logstring("tx %ld packets, %ld bytes (%ld/second)",
		imc_stats.tx_pkts,
		imc_stats.tx_bytes,
		(imc_now == imc_stats.start) ? 0 :
		imc_stats.tx_bytes / (imc_now - imc_stats.start));

  close(control);

  for (i=0; i<IMC_MAX; i++)
    if (imc[i].inuse)
      do_close(i);

  imc_strfree(imc_name);

  imc_active=0;
}

/* interpret an incoming packet using the right version */
static imc_internal *do_interpret_packet(int i, const char *line)
{
  int v;
  imc_internal *p;

  if (!line[0])
    return NULL;

  v=imc[i].version;
  if (v>IMC_VERSION)
    v=IMC_VERSION;

  p=(*imc_vinfo[v].interpret)(line);
  if (p)
    if (imc[i].info!=-1)
      p->stamp=imc_info[imc[i].info].rcvstamp;
    else
      p->stamp=0;

  return p;
}

/* low-level idle function: read/write buffers as needed, etc */
void imc_ll_idle()
{
  fd_set read, write, exc;
  int i;
  struct timeval timeout;
  const char *command;
  imc_internal *p;
  int maxfd;
  int handled;
  
  static int antispam_delay=0;

  expire();

  if (imc_sequencenumber < (unsigned long)imc_now)
    imc_sequencenumber=(unsigned long)imc_now;

  /* try to reconnect where necessary */

  for (i=0; i<IMC_MAX; i++)
    if (imc_info[i].inuse && !imc_info[i].connected && imc_info[i].timer &&
	imc_now > imc_info[i].timer)
      imc_connect(imc_info[i].name);

  /* set up fd_sets for select */

  FD_ZERO(&read);
  FD_ZERO(&write);
  FD_ZERO(&exc);

  maxfd = control+1;
  FD_SET(control, &read);

  for (i=0; i<IMC_MAX; i++)
  {
    if (!imc[i].inuse)
      continue;

    if (maxfd <= imc[i].desc)
      maxfd = imc[i].desc+1;

    switch (imc[i].state)
    {
    case IMC_CONNECTING:	/* connected/error when writable */
      FD_SET(imc[i].desc, &write);
      break;
    case IMC_CONNECTED:
    case IMC_WAIT1:
    case IMC_WAIT2:
      FD_SET(imc[i].desc, &read);
      FD_SET(imc[i].desc, &write);   /* set write even when no output waiting
				      * since we may generate data after a read
				      */
      break;
    }
  }

  timeout.tv_sec = timeout.tv_usec = 0;		/* return immediately */

  while ((i=select(maxfd, &read, &write, NULL, &timeout)) < 0 &&
	 errno == EINTR)	/* loop, ignoring signals */
    ;

  if (i<0)
  {
    imc_lerror("imc_ll_idle: select");
    /* shut down imc, just in case */
    imc_ll_shutdown();
    return;
  }
  if (i <= 0)
    return;

  /* now handle results of the select */

  if (FD_ISSET(control, &read))
    do_accept();

  if (antispam_delay>0)
    antispam_delay--;

  for (i=0; i<IMC_MAX; i++)
  {
    if (imc[i].inuse)
    {
      if (imc[i].spamcounter1)
	imc[i].spamcounter1--;

      if (imc[i].spamcounter2)
	imc[i].spamcounter2--;

      if (FD_ISSET(imc[i].desc, &read))
	do_read(i);
    }

    handled=0;

    while (imc[i].inuse &&
	   imc[i].spamcounter1<=IMC_SPAM1LIMIT &&
	   imc[i].spamcounter2<=IMC_SPAM2LIMIT &&
	   (command = imc_getline(imc[i].inbuf)) != NULL)
    {
      handled=1;

      imc[i].spamcounter1+=IMC_SPAM1SIZE;
      imc[i].spamcounter2+=IMC_SPAM2SIZE;

      imc_debug(i, 0, command);	/* log incoming packets */

      switch (imc[i].state)
      {
      case IMC_WAIT1:
	clientpassword(i, command);
	break;
      case IMC_WAIT2:
	serverpassword(i, command);
	break;
      case IMC_CONNECTED:
	p = do_interpret_packet(i, command);
	if (p)
	{
#ifdef IMC_PARANOIA
	  /* paranoia: check the last entry in the path is the same as the
	   * sending mud. Also check the first entry to see that it matches
	   * the sender.
	   */

	  imc_stats.rx_pkts++;

	  if (strcasecmp(imc_info[imc[i].info].name,
			 imc_lastinpath(p->path)))
	    imc_logerror("PARANOIA: packet from %s allegedly from %s",
			 imc_info[imc[i].info].name,
			 imc_lastinpath(p->path));
	  else if (strcasecmp(imc_mudof(p->from), imc_firstinpath(p->path)))
	    imc_logerror("PARANOIA: packet from %s has firstinpath %s",
			 p->from,
			 imc_firstinpath(p->path));
	  else
	    forward(p);		/* only forward if its a valid packet! */
#else
	  imc_stats.rx_pkts++;
	  forward(p);
#endif
	  imc_freedata(&p->data);
	}
	break;
      }
    }

    if (handled && !antispam_delay && imc[i].inuse &&
	(imc[i].spamcounter1>IMC_SPAM1LIMIT ||
	 imc[i].spamcounter2>IMC_SPAM2LIMIT))
    {
      imc_logerror("Spam from %s", imc_info[imc[i].info].name);
      antispam_delay=40; /* 10s between reports */
    }

    if (imc[i].inuse && FD_ISSET(imc[i].desc, &write))
      do_write(i);
  }
}

/* connect to given mud */
int imc_connect(const char *mud)
{
  int i;
  int d;
  int desc;
  struct sockaddr_in sa;
  char buf[IMC_DATA_LENGTH];
  int r;

  i=imc_getindex(mud);
  if (i<0)
  {
    imc_qerror("%s: unknown mud name", mud);
    return 0;
  }

  if (imc_info[i].connected)
  {
    imc_qerror("%s: already connected", mud);
    return 0;
  }

  if (imc_info[i].flags & IMC_CLIENT)
  {
    imc_qerror("%s: client-only flag is set", mud);
    return 0;
  }

  if (imc_info[i].flags & IMC_DENY)
  {
    imc_qerror("%s: deny flag is set", mud);
    return 0;
  }

  imc_logstring("connect to %s", mud);
  imc_info[i].timer=0;

  for (d = 0; d < IMC_MAX; d++)
    if (!imc[d].inuse)
      break;

  if (d == IMC_MAX)
  {
    imc_logerror("imc_connect: out of descriptors");
    return 0;
  }

  /*  warning: this blocks. It would be better to farm the query out to
   *  another process, but that is really unportable. You may want to change
   *  this code if you have an existing resolver running.
   */

  if ((sa.sin_addr.s_addr=inet_addr(imc_info[i].host)) == -1UL)
  {
    struct hostent *hostinfo;

    if (NULL == (hostinfo=gethostbyname(imc_info[i].host)))
    {
      imc_logerror("imc_connect: couldn't resolve hostname");
      return 0;
    }

    sa.sin_addr.s_addr = *(unsigned long *) hostinfo->h_addr;
  }

  sa.sin_port   = htons(imc_info[i].port);
  sa.sin_family = AF_INET;

  desc=socket(AF_INET, SOCK_STREAM, 0);
  if (desc<0)
  {
    imc_lerror("socket");
    return 0;
  }

  r=fcntl(desc, F_GETFL, 0);
  if (r<0 || fcntl(desc, F_SETFL, O_NONBLOCK | r)<0)
  {
    imc_lerror("imc_connect: fcntl");
    close(desc);
    return 0;
  }

  if (connect(desc, (struct sockaddr *)&sa, sizeof(sa))<0)
    if (errno != EINPROGRESS)
    {
      imc_lerror("connect");
      close(desc);
      return 0;
    }

  imc[d].inuse    = 1;
  imc[d].desc     = desc;
  imc[d].state    = IMC_CONNECTING;
  imc[d].info     = i;
  imc[d].inbuf    = imc_malloc(IMC_BUFFER_SIZE);
  imc[d].outbuf   = imc_malloc(IMC_BUFFER_SIZE);
  imc[d].inbuf[0] = imc[d].outbuf[0] = 0;

  sprintf(buf, "PW %s %s version=%d",
	  imc_name,
	  imc_info[i].clientpw,
	  IMC_VERSION);
  do_send(d, buf);

  return 1;
}

int imc_disconnect(const char *mud)
{
  int i, j;

  i=imc_getindex(mud);
  if (i == -1)
  {
    imc_qerror("%s: unknown mud", mud);
    return 0;
  }

  imc_logstring("disconnect %s", mud);

  for (j=0; j<IMC_MAX; j++)
    if (imc[j].inuse && imc[j].info == i)
      do_close(j);

  return 1;
}

void imc_send(const imc_packet * p)
{
  imc_internal out;

  out.path[0]  = 0;
  out.sequence = imc_sequencenumber++;
  if (!imc_sequencenumber)
    imc_sequencenumber++;
  imc_sncpy(out.to, p->to, IMC_NAME_LENGTH);
  imc_sncpy(out.from, p->from, IMC_NAME_LENGTH - 1);
  strcat(out.from, "@");
  imc_sncpy(out.from + strlen(out.from), imc_name,
	    IMC_NAME_LENGTH - strlen(out.from));
  imc_sncpy(out.type, p->type, IMC_TYPE_LENGTH);
  out.data = p->data;
  out.stamp = 0;

  forward(&out);
}

const char *imc_error()
{
  return lasterror;
}
