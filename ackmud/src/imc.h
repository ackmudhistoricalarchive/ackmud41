/*
 * IMC2 - an inter-mud communications protocol
 *
 * imc.h: the core protocol definitions
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

#ifndef IMC_H
#define IMC_H

#include <sys/time.h>


/**  Configurable stuff  **/


/* system defines: #define NO_xxxx if your system doesn't support a function */

/* for memmove() */
#undef NO_MEMMOVE

/* for vsnprintf() (probably most non-linux systems) */
#undef NO_VSNPRINTF

/* for strerror() (eg. some versions of SunOS) */
#undef NO_STRERROR

/* for strtoul() */
#undef NO_STRTOUL

/* #define this if you aren't using gcc (or if __attribute__ isn't supported
 * for some other reason)
 */

#undef NO_ATTRIBUTE


/* IMC tweakable values */

/* maximum number of muds we know about (direct connections, not indirect) */
#define IMC_MAX    20

/* Increase the memory #defines if you start getting echoing */

/* number of packets to remember at a time */
#define IMC_MEMORY 500
/* how long until packets time out of the memory (seconds) */
#define IMC_MEMORY_TIMEOUT 900

/* Reconnection timeouts */

/* How often to try to reconnect (seconds) */
#define IMC_RECONNECT_TIME 1800
/* How soon the first reconnect attempt is (seconds) */
#define IMC_RECONNECT_TIME_1 120

/* Packet spam thresholds - these will need to be tweaked depending on
 * how often imc_ll_idle is called. Stock ROM/Envy will call it every
 * 250ms or thereabouts.
 */

/* counter 1: long-term threshold
 *            allow 0.5/second normally
 *            allow 2/second for up to 20s
 */
#define IMC_SPAM1SIZE 8
#define IMC_SPAM1LIMIT 240

/* counter 2: short-term threshold, catch bad spamming early
 *            allow 1/second normally
 *            allow 4/second for 5s
 */
#define IMC_SPAM2SIZE 4
#define IMC_SPAM2LIMIT 60



/** Less tweakable stuff - don't change this unless you understand it! **/

/* This is the protocol version */
#define IMC_VERSION 2
/* This is the code version ID (used in keepalives) */
#define IMC_VERSIONID "imc2-0.7a"

/* enable paranoia in packet forwarding (generally a Good Thing) */
#define IMC_PARANOIA

/* various buffer sizes */

#define IMC_BUFFER_SIZE   16384
#define IMC_PACKET_LENGTH 16300
#define IMC_MNAME_LENGTH  20
#define IMC_PNAME_LENGTH  40
#define IMC_NAME_LENGTH   (IMC_MNAME_LENGTH+IMC_PNAME_LENGTH)
#define IMC_PATH_LENGTH   200
#define IMC_TYPE_LENGTH   20
#define IMC_PW_LENGTH     20
#define IMC_DATA_LENGTH   (IMC_PACKET_LENGTH-2*IMC_NAME_LENGTH-IMC_PATH_LENGTH-IMC_TYPE_LENGTH-20)
#define IMC_MAX_KEYS      20



/** End of configurable section **/




/* Don't change below here unless you know what you're doing! */

/* handle system-specific #defines */

/* our version of memmove (ugly!) */
#ifdef NO_MEMMOVE
#define memmove(dest,src,size)              \
do {                                        \
  char *d=(char *)(dest), *s=(char *)(src); \
  int sz=(size);                            \
  if (d<s)                                  \
    for ( ; sz; --sz)                       \
      *d++=*s++;                            \
  else                                      \
    for (s+=sz,d+=sz; sz; --sz)             \
      *(--d)=*(--s);                        \
} while(0)
#endif

/* try strtol if we don't have strtoul */
#ifdef NO_STRTOUL
#define strtoul(p,e,b) ((unsigned long)strtol((p),(e),(b)))
#endif

/* fake a strerror if we don't have it - ick */
#ifdef NO_STRERROR
#define strerror(e) \
((e)==ECONNRESET   ? "Connection reset by peer" : \
 (e)==ENETUNREACH  ? "Network unreachable"      : \
 (e)==ETIMEDOUT    ? "Connection timed out"     : \
 (e)==ECONNREFUSED ? "Connection refused"       : \
 (e)==EHOSTUNREACH ? "No route to host"         : \
                     "Unknown error")
#endif

/* map vsnprintf to vsprintf if it isn't available (we lose some buffer
 * overflow protection in the logging fns, though)
 */

#ifdef NO_VSNPRINTF
#define vsnprintf(buf, len, fmt, ap) vsprintf(buf, fmt, ap)
#endif

/* nuke __attribute__ if it's not supported */

#ifdef NO_ATTRIBUTE
#define __attribute__(x) /*nothing*/
#endif

/* connection states */

#define IMC_CLOSED     0
#define IMC_CONNECTING 1
#define IMC_WAIT1      2
#define IMC_WAIT2      3
#define IMC_CONNECTED  4

/* mud flags */

#define IMC_NOAUTO 1
#define IMC_CLIENT 2
#define IMC_RECONNECT 4
#define IMC_BROADCAST 8
#define IMC_DENY 16

/* typedefs */

typedef struct
{
  char *key[IMC_MAX_KEYS];
  char *value[IMC_MAX_KEYS];
} imc_data;

typedef struct
{
  char to[IMC_NAME_LENGTH];	/* destination of packet */
  char from[IMC_NAME_LENGTH];	/* source of packet      */
  char type[IMC_TYPE_LENGTH];	/* type of packet        */
  imc_data data;		/* data of packet        */
} imc_packet;

typedef struct
{
  int desc;			/* descriptor */
  int info;			/* index into imc_info */
  int state;			/* IMC_xxxx state */
  int inuse;			/* in use? */
  int version;		        /* version of remote site */
  
  char *inbuf;		        /* input buffer */
  char *outbuf;		        /* output buffer */

  int spamcounter1;             /* packet spam counters */
  int spamcounter2;

} _imc;

typedef struct
{
  char *name;			/* name of remote mud */
  char *host;			/* hostname */
  int index;			/* index of connected descriptor */
  unsigned short port;		/* remote port */
  char *serverpw;		/* server password */
  char *clientpw;		/* client password */
  int connected;		/* fully connected? */
  int flags;			/* connection flags */
  time_t timer;		        /* time of next reconnect attempt */
  int inuse;                    /* this entry is in use? */

  int rcvstamp;                 /* packets get this stamp on arrival */
  int noforward;                /* packets with these bits set don't get
				 * forwarded here */
} _imc_info;

typedef struct
{
  time_t start;		        /* when statistics started */
  
  long rx_pkts;		        /* received packets */
  long tx_pkts;		        /* transmitted packets */
  long rx_bytes;		/* received bytes */
  long tx_bytes;		/* transmitted bytes */
} imc_statistics;

extern imc_statistics imc_stats;

typedef struct _imc_reminfo
{
  char *name;
  char *version;
  time_t alive;
  int ping;
  char *route;
  struct _imc_reminfo *next;
} imc_reminfo;

extern imc_reminfo *imc_remoteinfo;


/* internal stuff not exported outside IMC */
#ifdef IMC_INTERNALS

/* imc_internal is the internal packet representation */

typedef struct
{
  char to[IMC_NAME_LENGTH];
  char from[IMC_NAME_LENGTH];
  char path[IMC_PATH_LENGTH];
  char type[IMC_TYPE_LENGTH];
  imc_data data;
  unsigned long sequence;
  int stamp;
} imc_internal;

typedef struct
{
  int version;
  const char *(*generate) (const imc_internal *);
  imc_internal *(*interpret) (const char *);
} _imc_vinfo;

extern _imc_vinfo imc_vinfo[];

typedef struct
{
  char *from;
  unsigned long sequence;
  int timer;
} _imc_memory;

extern _imc_memory imc_memory[IMC_MEMORY];

#endif



/* data structures */

extern char *imc_name;
extern _imc imc[IMC_MAX];
extern _imc_info imc_info[IMC_MAX];
extern time_t imc_now;	/* set before calling imc_ll_idle */
extern unsigned long imc_sequencenumber;

/* exported functions */

/* imc data handlers */
const char *imc_getkey(const imc_data * p, const char *key, const char *def);
int imc_getkeyi(const imc_data * p, const char *key, int def);
void imc_addkey(imc_data * p, const char *key, const char *value);
void imc_addkeyi(imc_data * p, const char *key, int value);
void imc_initdata(imc_data * p);
void imc_freedata(imc_data * p);

/* reminfo handling */
imc_reminfo *imc_find_reminfo(const char *name);
imc_reminfo *imc_new_reminfo(void);
void imc_delete_reminfo(imc_reminfo * p);

/* other stuff */
void imc_send(const imc_packet * p);
int imc_ll_startup(const char *name, int port);
void imc_ll_shutdown(void);
void imc_ll_idle(void);
int imc_connect(const char *mud);
int imc_disconnect(const char *mud);

const char *imc_error(void);

const char *imc_flagname(int value);
int imc_flagvalue(const char *name);

int imc_getmud(const char *name);

const char *imc_nameof(const char *name);
const char *imc_mudof(const char *name);
const char *imc_makename(const char *name, const char *mud);
const char *imc_firstinpath(const char *path);
const char *imc_lastinpath(const char *path);
const char *imc_getarg(const char *arg, char *buf, int length);

void imc_recv(const imc_packet * p);
void imc_log(const char *string);
void imc_debug(int desc, int out, const char *packet);

void imc_logstring(const char *format,...) __attribute__((format(printf,1,2)));
void imc_logerror(const char *format,...) __attribute__((format(printf,1,2)));
void imc_qerror(const char *format,...) __attribute__((format(printf,1,2)));
void imc_lerror(const char *format,...) __attribute__((format(printf,1,2)));

void imc_slower(char *what);
void imc_sncpy(char *dest, const char *src, int count);
const char *imc_makename(const char *player, const char *mud);
int imc_getindex(const char *mud);
const char *imc_getdescname(int i);

/* memory allocation hooks
 * these aren't #defines so we don't have to know about the actual mud
 * function declarations in imc.c/imc-comm.c
 */

void *imc_malloc(int size);
void imc_free(void *block, int size);
char *imc_strdup(const char *src);
void imc_strfree(char *str);

#endif
