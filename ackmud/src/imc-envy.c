/*
 * IMC2 - an inter-mud communications protocol
 *
 * imc-envy.c: Envy2 interface code
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

/* Based on imc-rom.c; conversion from ROM to Envy by Erwin S. Andreasen
 * <erwin@pip.dknet.dk>
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "merc.h"

#define IMC_INTERNALS
#include "imc.h"
#include "imc-comm.h"
#include "imc-envy.h"
#include "imc-mail.h"

/* memory allocation hooks */


void *imc_malloc(int size)
{
  return getmem(size);
}

void imc_free(void *block, int size)
{
  dispose(block, size);
}

char *imc_strdup(const char *src)
{
  return str_dup(src);
}

void imc_strfree(char *str)
{
  free_string(str);
}

/* See imc-envy.h and INSTALL.envy2 for information on how to set the color 
 * stuff up.
 * Read INSTALL.envy2 on changes made from ROM to Envy2
 */
#if 0
#ifdef IMC_NEED_COLOR

static char *escapecolor(const char *s)
{
  static char buf[MAX_STRING_LENGTH];
  char *p=buf;

  while (*s)
  {
    if (*s==IMC_COLOR_CHAR)
      *p++=IMC_COLOR_CHAR;
    *p++=*s++;
  }

  *p=0;

  return buf;
}

static char *nocolor(const char *s)
{
  static char buf[MAX_STRING_LENGTH];
  char *p=buf;

  while (*s)
  {
    if (*s=='@' &&
        *(s+1)=='@')
    {
      s+=2;
      if (*s)
        s++;
    }
    else
      *p++=*s++;
  }

  *p=0;

  return buf;
}


#else

#define nocolor(x) (x)
#define escapecolor(x) (x)

#endif
#endif /* if 0 */

void imc_markmemory(void (*markfn)(void *))
{
  int i;
  imc_reminfo *r;
  imc_mail *mailp;
  imc_qnode *queuep;
  imc_mailid *idp;

  for (i=0; i<IMC_MAX; i++)
    if (imc[i].inuse)
    {
      markfn(imc[i].inbuf);
      markfn(imc[i].outbuf);
    }

  for (r=imc_remoteinfo; r; r=r->next)
    markfn(r);

  for (mailp=imc_ml_head; mailp; mailp=mailp->next)
    markfn(mailp);
  
  for (queuep=imc_mq_head; queuep; queuep=queuep->next)
    markfn(queuep);
  
  for (idp=imc_idlist; idp; idp=idp->next)
    markfn(idp);
}

void imc_markstrings(void (*markfn)(char *))
{
  int i;
  imc_reminfo *r;
  imc_mail *mailp;
  imc_qnode *queuep;
  imc_mailid *idp;

  markfn(imc_name);

  for (i = 0; i < IMC_MAX; i++)
    if (imc_info[i].inuse)
    {
      markfn(imc_info[i].name);
      markfn(imc_info[i].host);
      markfn(imc_info[i].serverpw);
      markfn(imc_info[i].clientpw);
    }
  
  for (r = imc_remoteinfo; r; r = r->next)
   {
    markfn(r->name);
    markfn(r->version);
    markfn(r->route);
   }
  
   for (i = 0; i < IMC_RIGNORE_MAX; i++)
    markfn(imc_rignore[i]);
  
  for (i = 0; i < IMC_MEMORY; i++)
   if (imc_memory[i].timer)
      markfn(imc_memory[i].from); 
  markfn(imc_prefix);
 
  for (mailp=imc_ml_head; mailp; mailp=mailp->next)
  {
    markfn(mailp->from);
    markfn(mailp->to);
    markfn(mailp->date);
    markfn(mailp->text);
    markfn(mailp->subject);
    markfn(mailp->id);
  }
  
  for (queuep=imc_mq_head; queuep; queuep=queuep->next)
    markfn(queuep->tomud);
  
  for (idp=imc_idlist; idp; idp=idp->next)
    markfn(idp->id);
}
  
/*  maps IMC standard -> mud local color codes
 *  let's be unique, noone uses ~ :>
 */

struct {
  char *imc;          /* IMC code to convert */
  char *mud;          /* Equivalent mud code */
}
trans_table[]=
{
  { "~~", "~"  },  /* escape raw tildes */

#ifdef IMC_COLOR
  { "@@",  "@ @" },  /* escape our color character */

  {  "~b", "@@B"},  /* blue    */
  {  "~g", "@@G"},  /* green   */   
  {  "~r", "@@R" },  /* red     */
  {  "~y", "@@b"},  /* yellow  */
  {  "~m", "@@m"},  /* magenta */
  {  "~c", "@@c"},  /* cyan    */
  {  "~w", "@@g"},  /* white   */

  {  "~D", "@@d"},  /* grey        */
  {  "~B", "@@l"},  /* lt. blue    */
  {  "~G", "@@r"},  /* lt. green   */
  {  "~R", "@@e"},  /* lt. red     */
  {  "~Y", "@@y"},  /* lt. yellow  */
  {  "~M", "@@p"},  /* lt. magenta */
  {  "~C" ,"@@a"},  /* lt. cyan    */
  {  "~W", "@@W"},  /* lt. white   */

  {  "~!", "@@N"},  /* reset   */
  {  "~d", "@@N"},  /* default */
  {  "~!", "@@x"},  /* reset   */
  {  "~!", "@@f"},  /* default */
  {  "~!", "@@!"},  /* reset   */
  {  "~!", "@@."},  /* default */
  {  "~!", "@@n"},
  {  "~D", "@@k"},


#else
  { "~b", "" },  /* blue    */
  { "~g", "" },  /* green   */
  { "~r", "" },  /* red     */
  { "~y", "" },  /* yellow  */
  { "~m", "" },  /* magenta */
  { "~c", "" },  /* cyan    */
  { "~w", "" },  /* white   */

  { "~D", "" },  /* grey        */
  { "~B", "" },  /* lt. blue    */
  { "~G", "" },  /* lt. green   */
  { "~R", "" },  /* lt. red     */
  { "~Y", "" },  /* lt. yellow  */
  { "~M", "" },  /* lt. magenta */
  { "~C", "" },  /* lt. cyan    */
  { "~W", "" },  /* lt. white   */
  
  { "~!", "" },  /* reset   */
  { "~d", "" },  /* default */
#endif
};

#define numtrans (sizeof(trans_table)/sizeof(trans_table[0]))

/* convert from imc color -> mud color, with optimisation */
 
const char *color_itom(const char *s)
{
  static char buf[MAX_STRING_LENGTH];
  const char *current;
  char *out;
  int i, l;
  int last=-1;
  
  for (current=s, out=buf; *current; )
  {
    for (i=0; i<numtrans; i++)
    {
      l=strlen(trans_table[i].imc);
      if (l && !strncmp(current, trans_table[i].imc, l))
        break;
    }
  
    if (i==numtrans)      /* no match */
      *out++=*current++;
    else                  /* match */
    {
      if (last!=i)
      {
	strcpy(out, trans_table[i].mud);
	out+=strlen(out);
	last=i;
      }
      current+=l;
    }
  }
  
  *out=0;
  return buf;
}
  
 /* convert from mud color -> imc color */
  
const char *color_mtoi(const char *s)
{
  static char buf[MAX_STRING_LENGTH];
  const char *current;
  char *out;
  int i, l;
  int last=-1;
  
  for (current=s, out=buf; *current; )
  {
    for (i=0; i<numtrans; i++)
    {
      l=strlen(trans_table[i].mud);
      if (l && !strncmp(current, trans_table[i].mud, l))
        break;
    }
    
    if (i==numtrans)      /* no match */
      *out++=*current++;
    else                  /* match */
    {
      if (last!=i)
      {
	strcpy(out, trans_table[i].imc);
	out+=strlen(out);
	last=i;
      }
      current+=l;
    }
  }
  
  *out=0;
  return buf;
}
  
/* There are 2 versions of the IS_SILENT macro: one that checks the race_table
 * and one that doesn't. the RACE_MUTE flag appeared in one of the Envy 2.0
 * patches, but not everyone has it. I did NOT have applied it when creating
 * this patch.
 */

/*
#define IS_SILENT(ch) (IS_AFFECTED(ch,AFF_MUTE) || \
		   IS_SET(race_table[ch->race].race_abilities, RACE_MUTE) || \
		   IS_SET(ch->in_room->room_flags, ROOM_CONE_OF_SILENCE))
*/					   

#define IS_SILENT(ch) (IS_SET(ch->act,PLR_SILENCE) || \
		   IS_SET(ch->in_room->room_flags, ROOM_QUIET))

static struct {
  int	number;
  const char *name;
  const char *chatstr;
  const char *emotestr;
  int flag;
  int minlevel;
  char *to;
} 
/* channels[]=
  {
    { "RChat", "@@W[@@yRCHAT@@W] $t:'@@g$T@@W'@@N", "[RChat]$t $T.", IMC_NORCHAT, 5 },
    { "RImm",  "@@W[@@mRIMM@@W] $t:'@@g$T@@W'@@N",  "[RImm]$t $T.",  IMC_NORIMM,  LEVEL_HERO },
    { "RInfo", "@@W[@@aRINFO@@W] $t:'@@g$T@@W'@@N", "[RInfo]$t $T.", IMC_NORINFO, LEVEL_HERO },
    { "RCode", "@@W[@@eRCODE@@W] $t:'@@g$T@@W'@@N", "[RCode]$t $T.", IMC_NORCODE, LEVEL_HERO },         
    { "ALLimm", "@@a*@@W<@@l[$t@@l]@@W>@@a*@@g $T@@N", "[^*^]$t $T.", IMC_NOALLIMM, LEVEL_HERO },
  };
  */
channels[]=
{
  {
    0,                       /* channel number                         */
    "RChat",                 /* channel name                           */
    "@@W[@@yRCHAT@@W] $t:'@@g$T@@W'@@N",        /* act for normal transmission            */
    "[RChat]$t $T.",          /* act for emoting                        */
    IMC_NORCHAT,             /* flag in ch->pcdata->imc to turn it off */
    20,                       /* min. level to see it                   */
    "*"                      /* muds to send to                        */
  },

  {
    1,
    "RImm",
    "@@W[@@mRIMM@@W] $t:'@@g$T@@W'@@N",  
    "[RImm]$t $T.",  
    IMC_NORIMM, 
    L_SUP,
    "*"
  },

  {
    2,
    "RInfo",
    "@@W[@@aRINFO@@W] $t:'@@g$T@@W'@@N",
    "[RInfo]$t $T.",
    IMC_NORINFO,
    L_SUP,
    "*",
  },

  {
    3,
    "RCode",
    "@@W[@@eRCODE@@W] $t:'@@g$T@@W'@@N",
    "[RCode]$t $T",
    IMC_NORCODE,
    L_SUP,
    "*"
  },
  {
    4,
    "ALLimm",
    "@@a*@@W<@@l[$t@@l]@@W>@@a*@@g $T@@N",
    "[^*^]$t $T.",
    IMC_NOALLIMM,
    LEVEL_HERO,
    "SOETEST SOEBLD SOE Abyss AA LOTS"
  }
};
  
#define numchannels (sizeof(channels)/sizeof(channels[0]))

static int getlevel(int l)
{
  if (l<LEVEL_HERO)
    return l;
  else
    return l-MAX_LEVEL-1;
}

static const imc_char_data *getdata(CHAR_DATA *ch)
{
  static imc_char_data d;

  if (!ch) /* fake system character */
  {
    d.wizi=d.invis=d.see=0;
    d.level=-1;
    d.sex=2;
    strcpy(d.name, "*");
    return &d;
  }

  /* Envy2 has only either full wizinvis or no wizinvis */
  d.wizi=getlevel( IS_SET(ch->act, PLR_WIZINVIS) ? ch->invis : 0);

  d.invis=0;
  if (IS_AFFECTED(ch, AFF_INVISIBLE))
    d.invis|=IMC_INVIS;
  if (IS_AFFECTED(ch, AFF_SNEAK))
    d.invis|=IMC_HIDDEN;

  d.see=0;
  if (IS_AFFECTED(ch, AFF_DETECT_INVIS))
    d.see|=IMC_INVIS;
  if (IS_AFFECTED(ch, AFF_DETECT_HIDDEN))
    d.see|=IMC_HIDDEN;

  d.level=getlevel(get_trust(ch));
  strcpy(d.name, ch->name);

  d.sex=(ch->sex+1)%2;

  return &d;
}

static int visible(const imc_char_data *viewer, const imc_char_data *viewed)
{
  /* If you can follow this, you're better than me :) .. I've got it wrong
   * at least 3 times now   -- Spectrum
   */

  return
    !(
      (viewer->level>=0 &&                  /* mortal viewer */
       (viewer->see & viewed->invis)!=viewed->invis) ||

      (viewed->wizi<0 &&                    /* imm level wizi */
       (viewer->level>=0 ||                 /* mortal viewer */
        viewer->level<viewed->wizi)) ||     /* imm less than wizi level */

      (viewed->wizi>0 &&                    /* mortal level wizi */
       viewer->level>=0 &&                  /* mortal viewer */
       viewer->level<viewed->wizi)          /* level less than wizi */
     );
}

static const char *getname(CHAR_DATA *ch, const imc_char_data *vict)
{
  static char buf[IMC_NAME_LENGTH];

  if (visible(getdata(ch),vict))
    return vict->name;

  sprintf(buf, "%s@%s", 
  ( getdata(ch)->wizi ? "A Mystical Being" : "Someone" ), 
  imc_mudof(vict->name));
  return buf;
}

void send_rchannel(CHAR_DATA *ch, char *argument, int number)
{
  char arg[MAX_STRING_LENGTH];
  char *arg2;
  int  chan;

  if ( IS_NPC( ch ) )
    return;
  for (chan=0; chan<numchannels; chan++)
    if (channels[chan].number==number)
      break;

  if (chan==numchannels)
   return; /* oops */

  
    if (!argument[0])
    {
      if (IS_SET(ch->pcdata->imc, channels[chan].flag))
      {
        sprintf(arg, "%s channel is now ON.\n\r", channels[chan].name);
        send_to_char(arg, ch);
        REMOVE_BIT(ch->pcdata->imc, channels[chan].flag);
        return;
      }
      
      sprintf(arg, "%s channel is now OFF.\n\r", channels[chan].name);
      send_to_char(arg, ch);
  
      SET_BIT(ch->pcdata->imc, channels[chan].flag);
      return;
    }
  


  if (IS_SET(ch->act, PLR_SILENCE))
  {
    send_to_char("The gods have revoked your channel priviliges.\n\r", ch);
    return;
  }

  if (IS_SILENT(ch))
  {
    send_to_char ("You can't seem to break the silence.\n\r",ch);
    return;
  }  
  
  REMOVE_BIT(ch->pcdata->imc, channels[chan].flag);

  arg2=one_argument(argument, arg);
  if (!str_cmp(arg, ",") || !str_cmp(arg, "emote"))
    imc_send_emote(getdata(ch), chan, color_mtoi(arg2), channels[chan].to);
  else
    imc_send_chat(getdata(ch), chan, color_mtoi(argument), channels[chan].to);
}

void do_rchat(CHAR_DATA *ch, char *argument)
{
  send_rchannel(ch, argument, 0);
}

void do_rimm(CHAR_DATA *ch, char *argument)
{
  send_rchannel(ch, argument, 1);
}

void do_rinfo(CHAR_DATA *ch, char *argument)
{
  send_rchannel(ch, argument, 2);
}

void do_rcode(CHAR_DATA *ch, char *argument)
{
  send_rchannel(ch, argument, 3);
}

void do_allimm(CHAR_DATA *ch, char *argument)
{
  send_rchannel(ch, argument, 4);
}

void do_rtell(CHAR_DATA *ch, char *argument)
{
  char buf[MAX_STRING_LENGTH], buf1[MAX_STRING_LENGTH];

  argument=one_argument(argument, buf);

  if (!buf[0] || !strchr(buf, '@') || !argument[0])
  {
    send_to_char("rtell who@where what?\n\r", ch);
    return;
  }

  if (IS_SET(ch->act, PLR_NO_TELL) || IS_SET(ch->act, PLR_SILENCE))
  {
    send_to_char("You cannot rtell!\n\r", ch);
    return;
  }

  if (IS_SILENT(ch))
  {
  	send_to_char ("You can't seem to break the silence.\n\r",ch);
  	return;
  }  


  imc_send_tell(getdata(ch), buf, color_mtoi(argument), 0);
  
  sprintf(buf1, "@@NYou @@ertell @@y%s @@W'@@g%s@@W'@@N\n\r", buf, argument );
  send_to_char(buf1, ch);
}

void do_rreply(CHAR_DATA *ch, char *argument)
{
  char buf1[MAX_STRING_LENGTH];

  if (IS_NPC(ch))
  {
    send_to_char("Uh... no.\n\r", ch);
    return;
  }

  if (!ch->pcdata->rreply)
  {
    send_to_char("rreply to who?\n\r", ch);
    return;
  }

  if (!argument[0])
  {
    send_to_char("rreply what?\n\r", ch);
    return;
  }

  if (IS_SET(ch->act, PLR_NO_TELL))
  {
    send_to_char("You cannot rtell!\n\r", ch);
    return;
  }

  if (IS_SILENT(ch))
  {
  	send_to_char ("You can't seem to break the silence.\n\r",ch);
  	return;
  }  

  imc_send_tell(getdata(ch), ch->pcdata->rreply, color_mtoi(argument), 1);

  sprintf(buf1, "@@NYou @@ertell @@y%s @@W'@@g%s@@W'@@N\n\r", ch->pcdata->rreply_name, 
          argument);
  send_to_char(buf1, ch);
} 

void do_rwho(CHAR_DATA *ch, char *argument)
{
  char arg[MAX_STRING_LENGTH];

  if (IS_NPC(ch))
  {
    send_to_char("Uh... no.\n\r", ch);
    return;
  }

  argument=one_argument(argument, arg);

  if (!arg[0])
  {
    send_to_char("rwho where?\n\r", ch);
    return;
  }

  imc_send_who(getdata(ch), arg, argument[0] ? argument : "who");
}

void do_rquery(CHAR_DATA *ch, char *argument)
{
  char arg[MAX_STRING_LENGTH];

  if (IS_NPC(ch))
  {
    send_to_char("Uh... no.\n\r", ch);
    return;
  }

  argument=one_argument(argument, arg);

  if (!arg[0])
  {
    send_to_char("rquery where?\n\r", ch);
    return;
  }

  imc_send_who(getdata(ch), arg, argument[0] ? argument : "help");
}

void do_mailqueue(CHAR_DATA *ch, char *argument)
{
  send_to_char(imc_mail_showqueue(), ch);
}

void do_istats(CHAR_DATA *ch, char *argument)
{
  send_to_char(imc_getstats(), ch);
}

void do_rbeep(CHAR_DATA *ch, char *argument)
{
  char buf[MAX_STRING_LENGTH];

  if (IS_NPC(ch))
  {
    send_to_char("Uh... no.\n\r", ch);
    return;
  }

  if (!argument[0] || !strchr(argument, '@'))
  {
    send_to_char("rbeep who@where?\n\r", ch);
    return;
  }

  imc_send_beep(getdata(ch), argument);
  sprintf(buf, "@@NYou @@erbeep @@y%s@@W.@@N\n\r", argument);
  send_to_char(buf, ch);
}

void do_imclist(CHAR_DATA *ch, char *argument)
{
  if (get_trust(ch)>=MAX_LEVEL-1)
    send_to_char(imc_list(2), ch);
  else if (IS_IMMORTAL(ch))
    send_to_char(imc_list(1), ch);
  else
    send_to_char(imc_list(0), ch);

  send_to_char("\n\r", ch);
}

void do_rsockets(CHAR_DATA *ch, char *argument)
{
  send_to_char(imc_sockets(), ch);
  send_to_char("\n\r", ch);
}

void do_imc(CHAR_DATA *ch, char *argument)
{
  int r;

  r=imc_command(argument);

  if (r>0)
  {
    send_to_char("Ok.\n\r", ch);
    return;
  }
  else if (r==0)
  {
    send_to_char("Syntax:  imc add <mudname>\n\r"
		 "         imc delete <mudname>\n\r"
		 "         imc set <mudname> all <host> <port> <clientpw> <serverpw> [<flags>]\n\r"
		 "         imc set <mudname> host|port|clientpw|serverpw|flags <value>\n\r"
		 "         imc rename <oldmudname> <newmudname>\n\r",
		 ch);
    return;
  }

  send_to_char(imc_error(), ch);
  send_to_char("\n\r", ch);
}

void do_rignore(CHAR_DATA *ch, char *argument)
{
  send_to_char(imc_ignore(argument), ch);
  send_to_char("\n\r", ch);
}

void do_rconnect(CHAR_DATA *ch, char *argument)
{
  if (!argument[0])
  {
    send_to_char("rconnect to where?\n\r", ch);
    return;
  }

  if (imc_connect(argument))
  {
    send_to_char("Ok.\n\r", ch);
    return;
  }

  send_to_char(imc_error(), ch);
  send_to_char("\n\r", ch);
}

void do_rdisconnect(CHAR_DATA *ch, char *argument)
{
  if (!argument[0])
  {
    send_to_char("rdisconnect where?\n\r", ch);
    return;
  }
  if (imc_disconnect(argument))
  {
    send_to_char("Ok.\n\r", ch);
    return;
  }

  send_to_char(imc_error(), ch);
  send_to_char("\n\r", ch);
}

static void do_rchannel(const imc_char_data *from, int chan,
			const char *argument, int emote)
{
  DESCRIPTOR_DATA *d;
  CHAR_DATA *victim;
  const char *str;
  int position;

  if (chan>=numchannels)
    return;

  str=emote ? channels[chan].emotestr : channels[chan].chatstr;

  for (d=first_desc; d; d=d->next)
  {
    if (d->connected==CON_PLAYING &&
	(victim=d->original ? d->original : d->character)!=NULL &&
	!IS_SET(victim->pcdata->imc, channels[chan].flag) &&
	!IS_SET(victim->in_room->room_flags, ROOM_QUIET) &&
	get_trust(victim)>=channels[chan].minlevel)
    {
      char color_string[MSL];
      const char *this_name;
      this_name = getname( victim, from );

      sprintf( color_string, "@@m%s@@W%s%s@@a%s@@N", imc_nameof(this_name ), 
      (  ( !str_cmp( imc_mudof( this_name ), imc_name ) ) ? "" : "@" ),
      (  ( !str_cmp( imc_mudof( this_name ), imc_name ) ) ? "" : "@@" ),
      (  ( !str_cmp( imc_mudof( this_name ), imc_name ) ) ? "" : imc_mudof( this_name) ) );
 
      position = victim->position;
      victim->position = POS_STANDING;
      
      act(str, victim, color_string,
              color_itom(argument), TO_CHAR);
      
      victim->position = position;
    }
  }
}

void imc_recv_chat(const imc_char_data *from, int channel, const char *argument)
{
  do_rchannel(from, channel, argument, 0);
}

void imc_recv_emote(const imc_char_data *from, int channel, const char *argument)
{
  do_rchannel(from, channel, argument, 1);
}

void imc_recv_who(const imc_char_data *from, const char *type)
{
  char buf[MAX_STRING_LENGTH];
  char buf2[MAX_STRING_LENGTH];
  char output[MAX_STRING_LENGTH];
  DESCRIPTOR_DATA *d;
  CHAR_DATA *victim;
  int count=0;
  char arg[MAX_STRING_LENGTH];

  type= imc_getarg(type, arg, MAX_STRING_LENGTH);
  
  if (!str_cmp(arg, "who"))
  {
   
/*    bool	is_imm = FALSE;
    bool	is_adept = FALSE;
    bool	is_remort = FALSE;  */
    output[0]=0;
    sprintf( output, "%s", color_mtoi("@@rWho's Playing " mudnamecolor ":\n\r" ));

    for (d=first_desc; d; d = d->next)
    {
      if (d->connected==CON_PLAYING &&
	  (victim = d->original ? d->original : d->character)!=NULL &&
	  !IS_NPC(victim) &&
	  visible(from, getdata(victim)))
      {
	count++;
	sprintf(buf2, "@@W[@@p%s@@W] @@G%s@@N%s@@N\n\r",
           ( ( IS_IMMORTAL( victim ) || ( victim->adept_level > 0 ) ) ? victim->pcdata->who_name : 
               is_remort( victim ) ? "    @@mREMORT@@N    " :
               "    @@cMORTAL@@N    " ),
           victim->name,
	   victim->pcdata->title );
        sprintf( buf, "%s", color_mtoi( buf2 ));
	strcat(output,buf);
      }
    }
    
    sprintf(output+strlen(output), "\n\rRWHO for %s [total %d]\n\r",
	    imc_name, count);
  }
#ifdef IMC_MUD_INFO
  else if (!str_cmp(arg, "info"))
    strcpy(output, IMC_MUD_INFO);
#endif
  else if (!str_cmp(arg, "list"))
    strcpy(output, imc_list(0));
  else if (!str_cmp(arg, "options") || !str_cmp(arg, "services") ||
	   !str_cmp(arg, "help"))
    strcpy(output,
           "Available rquery types:\n\r"
	   "options,\n\r"
	   "services,\n\r"
	   "help       - this list\n\r"
	   "who        - who listing\n\r"
#ifdef IMC_MUD_INFO
	   "info       - mud information\n\r"
#endif
	   "list       - active IMC connections\n\r");
  else
    strcpy(output, "Sorry, no information is available of that type.\n\r");

  imc_send_whoreply(from->name, output);
}

void imc_recv_whoreply(const char *to, const char *text)
{
  DESCRIPTOR_DATA *d;
  CHAR_DATA *victim;

  for (d=first_desc; d; d=d->next)
  {
    if (d->connected==CON_PLAYING &&
	(victim=d->original ? d->original : d->character)!=NULL &&
        is_name((char *)to, victim->name))
    {
      send_to_char(color_itom(text), victim);
      return;
    }
  }
}

void imc_recv_tell(const imc_char_data *from, const char *to,
		   const char *argument, int isreply)
{
  DESCRIPTOR_DATA *d;
  CHAR_DATA *victim;
  char buf[IMC_DATA_LENGTH];
  int position;

  if (!strcmp(to, "*")) /* ignore messages to system */
    return;

  for (d=first_desc; d; d=d->next)
  {
    if (d->connected==CON_PLAYING &&
	(victim=d->original ? d->original : d->character)!=NULL &&
        is_name((char *)to, victim->name) &&
        (isreply || visible(from, getdata(victim))))
    {
      if ( ( !IS_NPC( victim ) && IS_SET(victim->pcdata->pflags, PFLAG_AFK)) ||
          IS_SET(victim->in_room->room_flags, ROOM_QUIET) )
      {
	sprintf(buf, "@@y%s @@Wis not receiving @@etells@@W.@@N", to);
	imc_send_tell(NULL, from->name, buf, 1);
	return;
      }

      if (victim->pcdata->rreply)
	free_string(victim->pcdata->rreply);
      if (victim->pcdata->rreply_name)
	free_string(victim->pcdata->rreply_name);

      victim->pcdata->rreply=str_dup(from->name);
      victim->pcdata->rreply_name=str_dup(getname(victim, from));

	  position = victim->position;      
      victim->position = POS_STANDING;

      act("@@y$t @@ertells @@Wyou '@@g$T@@W'@@N", victim, victim->pcdata->rreply_name,
              color_itom(argument), TO_CHAR);
      
      victim->position = position;

      return;
    }
  }

  sprintf(buf, "@@y%s @@Wis not here.@@N", to);
  imc_send_tell(NULL, from->name, buf, 1);
}

void imc_recv_beep(const imc_char_data *from, const char *to)
{
  DESCRIPTOR_DATA *d;
  CHAR_DATA *victim;
  char buf[IMC_DATA_LENGTH];
  int position;

  if (!strcmp(to, "*")) /* ignore messages to system */
    return;

  for (d=first_desc; d; d=d->next)
  {
    if (d->connected==CON_PLAYING &&
	(victim=d->original ? d->original : d->character)!=NULL &&
        is_name((char *)to, victim->name) &&
	visible(from, getdata(victim)))

    {
    if (IS_SET(victim->in_room->room_flags, ROOM_QUIET ))
      {
	sprintf(buf, "@@y%s @@Wis not receiving @@ebeeps@@W.@@N", to);
	imc_send_tell(NULL, from->name, buf, 1);
	return;
      }

     position = victim->position;      
     victim->position = POS_STANDING;
 
     act("\a@@y$t @@erbeeps @@Wyou.@@N", victim, getname(victim, from), NULL,
	      TO_CHAR);

      victim->position = position;

      return;
    }
  }

  sprintf(buf, "@@y%s @@Wis not here.@@N", to);
  imc_send_tell(NULL, from->name, buf, 1);
}

void imc_log(const char *string)
{
  char buf[MAX_STRING_LENGTH];
  sprintf(buf, "imc: %s", string);

  monitor_chan(buf, MONITOR_CONNECT );

/*  wiznet(buf, NULL, NULL, WIZ_IMC, 0, 0); */
}

void imc_debug(int i, int out, const char *string)
{

  
  
  /* Envy2 does not have an equivalent to wiznet, although it is a very handy
     thing, so I leave this here if you create something like it */
     
#if 0
  char buf[MAX_STRING_LENGTH];
  char *dir;

  dir=out ? "<" : ">";

  sprintf(buf, "%s %s %s", imc_getdescname(i), dir, string);
  monitor_chan( buf, MONITOR_CONNECT );

/*  wiznet(escapecolor(buf), NULL, NULL, WIZ_IMCDEBUG, 0, 0);  */
#endif
return;
}

/* 
 * Posting a note: This code mostly copied from act_comm.c of Envy
 * If you are using my code (ftp://pip.dknet.dk/pub/pip1773/board-2.tgz)
 * there is another imc_mail_arrived() below - Erwin
 */
/*
char   *imc_mail_arrived (const char *from, const char *to, const char *date,
   const char *subject, const char *text)
{
	NOTE_DATA *pnote, *prev_note;
	FILE   *fp;
	char   *strtime;


	pnote = alloc_perm (sizeof (*pnote));
	pnote->next = NULL;
	strtime = ctime (&current_time);
	strtime[strlen (strtime) - 1] = '\0';

	pnote->date = str_dup (strtime);
	pnote->date_stamp = current_time;
	pnote->sender = str_dup (from);
	pnote->to_list = str_dup (to);
	pnote->date = str_dup (date);
	pnote->subject = str_dup (subject);
	pnote->text = str_dup (text);


	smash_tilde (pnote->sender);
	smash_tilde (pnote->to_list);
	smash_tilde (pnote->date);
	smash_tilde (pnote->subject);
	smash_tilde (pnote->text);


	if (!note_list)
	{
		note_list = pnote;
	}
	else
	{
		for (prev_note = note_list; prev_note->next; prev_note = prev_note->next)
			;
		prev_note->next = pnote;
	}

	fclose (fpReserve);
	if (!(fp = fopen (NOTE_FILE, "a")))
	{
		perror (NOTE_FILE);
	}
	else
	{
		fprintf (fp, "Sender  %s~\n", pnote->sender);
		fprintf (fp, "Date    %s~\n", pnote->date);
		fprintf (fp, "Stamp   %ld\n", (unsigned long) pnote->date_stamp);
		fprintf (fp, "To      %s~\n", pnote->to_list);
		fprintf (fp, "Subject %s~\n", pnote->subject);
		fprintf (fp, "Text\n%s~\n\n", pnote->text);
		fclose (fp);
	}
	fpReserve = fopen (NULL_FILE, "r");



	return NULL;			
}
*/

/* If you are using the board snippet: 
 * Note that this assumes that your make_note() does a smash_tilde(), the
 * latest version of the board snippet does, but your may not. Oh well.
 */

#if defined (BOARDS)
char   *imc_mail_arrived (const char *from, const char *to, const char *date,
   const char *subject, const char *text)
{
/*	make_note ("Personal", from, to, subject, 14, text);  */
	return "Shades of Evil does not have this implemented yet.\n\r";
}

#endif
