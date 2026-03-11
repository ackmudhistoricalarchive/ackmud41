
/*
 * IMC2 - an inter-mud communications protocol
 *
 * imc-version.c: packet generation/interpretation for various protocol
 *                versions
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
#include <ctype.h>
#include <string.h>

#define IMC_INTERNALS
#include "imc.h"

static const char *generate0(const imc_internal *);
static const char *generate1(const imc_internal *);
static const char *generate2(const imc_internal *);
static imc_internal *interpret0(const char *);
static imc_internal *interpret1(const char *);
static imc_internal *interpret2(const char *);

_imc_vinfo imc_vinfo[] =
{
  { 0, generate0, interpret0 },
  { 1, generate1, interpret1 },
  { 2, generate2, interpret2 }
};

/* escape2: escape " -> \", \ -> \\, CR -> \r, LF -> \n */

static const char *escape2(const char *data)
{
  static char buf[IMC_DATA_LENGTH];
  char *p;

  for (p=buf; *data && (p-buf < IMC_DATA_LENGTH-1); data++, p++)
  {
    if (*data == '\n')
    {
      *p++='\\';
      *p='n';
    }
    else if (*data == '\r')
    {
      *p++='\\';
      *p='r';
    }
    else if (*data == '\\')
    {
      *p++='\\';
      *p='\\';
    }
    else if (*data == '"')
    {
      *p++='\\';
      *p='"';
    }
    else
      *p=*data;
  }

  *p=0;

  return buf;
}

/* printkeys: print key-value pairs, escaping values */
static const char *printkeys(const imc_data * data)
{
  static char buf[IMC_DATA_LENGTH];
  char temp[IMC_DATA_LENGTH];
  int len=0;
  int i;

  buf[0]=0;

  for (i=0; i<IMC_MAX_KEYS; i++)
  {
    if (!data->key[i])
      continue;
    imc_sncpy(buf+len, data->key[i], IMC_DATA_LENGTH-len-1);
    strcat(buf, "=");
    len = strlen(buf);

    if (!strchr(data->value[i], ' '))
      imc_sncpy(temp, escape2(data->value[i]), IMC_DATA_LENGTH-1);
    else
    {
      temp[0]='"';
      imc_sncpy(temp+1, escape2(data->value[i]), IMC_DATA_LENGTH-3);
      strcat(temp, "\"");
    }

    strcat(temp, " ");
    imc_sncpy(buf+len, temp, IMC_DATA_LENGTH-len);
    len = strlen(buf);
  }

  return buf;
}

/* parsekeys: extract keys from string */

static void parsekeys(const char *string, imc_data * data)
{
  const char *p1;
  char *p2;
  char k[IMC_DATA_LENGTH], v[IMC_DATA_LENGTH];
  int quote;

  p1 = string;

  while (*p1)
  {
    while (*p1 && isspace(*p1))
      p1++;

    p2 = k;
    while (*p1 && *p1 != '=' && p2-k < IMC_DATA_LENGTH-1)
      *p2++=*p1++;
    *p2=0;

    if (!k[0] || !*p1)		/* no more keys? */
      break;

    p1++;			/* skip the '=' */

    if (*p1 == '"')
    {
      p1++;
      quote = 1;
    }
    else
      quote = 0;

    p2=v;
    while (*p1 && (!quote || *p1 != '"') && (quote || *p1 != ' ') &&
	   p2-v < IMC_DATA_LENGTH+1)
    {
      if (*p1 == '\\')
      {
	switch (*(++p1))
	{
	case '\\':
	  *p2++='\\';
	  break;
	case 'n':
	  *p2++='\n';
	  break;
	case 'r':
	  *p2++='\r';
	  break;
	case '"':
	  *p2++='"';
	  break;
	default:
	  *p2++=*p1;
	  break;
	}
	if (*p1)
	  p1++;
      }
      else
	*p2++=*p1++;
    }

    *p2=0;

    if (!v[0])
      continue;

    imc_addkey(data, k, v);

    if (quote && *p1)
      p1++;
  }
}

/* escape1: escape CR->\r, LF->\n, \->\\ */
static const char *escape1(const char *data)
{
  static char buf[IMC_DATA_LENGTH];
  char *p;

  for (p=buf; *data && (p-buf < IMC_DATA_LENGTH-1); data++, p++)
  {
    if (*data == '\n')
    {
      *p++='\\';
      *p='n';
    }
    else if (*data == '\r')
    {
      *p++='\\';
      *p='r';
    }
    else if (*data == '\\')
    {
      *p++='\\';
      *p='\\';
    }
    else
      *p=*data;
  }

  *p=0;

  return buf;
}

/* unescape1: reverse escape1 */
static const char *unescape1(const char *data)
{
  static char buf[IMC_DATA_LENGTH];
  char *p;
  char ch;

  for (p=buf; *data && (p-buf < IMC_DATA_LENGTH-1); data++, p++)
  {
    if (*data == '\\')
    {
      ch = *(++data);
      switch (ch)
      {
      case 'n':
	*p='\n';
	break;
      case 'r':
	*p='\r';
	break;
      case '\\':
	*p='\\';
	break;
      default:
	*p=ch;
	break;
      }
    }
    else
      *p=*data;
  }

  *p=0;

  return buf;
}

/* escape0: version 0 escape (\n\r -> ~) */
static const char *escape0(const char *string)
{
  static char buf[IMC_DATA_LENGTH];
  char *p=buf;

  while (*string && (p-buf < IMC_DATA_LENGTH-1))
  {
    if (*string == '\n' && *(string+1) == '\r')
    {
      *p++='~';
      string++;
      if (*string)
	string++;
    }
    else
      *p++=*string++;
  }

  *p=0;

  return buf;
}

/* unescape0: version 0 unescape */

static const char *unescape0(const char *string)
{
  static char buf[IMC_DATA_LENGTH];
  char *p=buf;

  while (*string && (p-buf < IMC_DATA_LENGTH-1))
  {
    if (*string == '~')
    {
      *p++='\n';
      *p++='\r';
    }
    else
      *p++=*string;

    string++;
  }

  *p=0;

  return buf;
}

static const char *generate0(const imc_internal * p)
{
  static char temp[IMC_PACKET_LENGTH];
  char newpath[IMC_PATH_LENGTH];
  char fromname[IMC_NAME_LENGTH];

  if (!p->type[0] || !p->from[0] || !p->to[0])
  {
    imc_logerror("BUG: generate0: bad packet!");
    return NULL;		/* catch bad packets here */
  }

  if (!strcmp(imc_nameof(p->from), "*"))
    strcpy(fromname, imc_mudof(p->from));
  else
    strcpy(fromname, imc_nameof(p->from));

  if (!p->path[0])
    strcpy(newpath, imc_name);
  else
    sprintf(newpath, "%s!%s", p->path, imc_name);

  if (!strcasecmp(p->type, "chat"))
    if (imc_getkeyi(&p->data, "channel", 0) == 0)
      sprintf(temp, "MS %s %s %s %s",
	      imc_mudof(p->from), imc_nameof(p->from), newpath,
	      imc_getkey(&p->data, "text", ""));
    else
      return NULL;
  else if (!strcasecmp(p->type, "emote"))
    if (imc_getkeyi(&p->data, "channel", 0) == 0)
      sprintf(temp, "AC %s %s %s %s",
	      imc_mudof(p->from), imc_nameof(p->from), newpath,
	      imc_getkey(&p->data, "text", ""));
    else
      return NULL;
  else if (!strcasecmp(p->type, "tell"))
    sprintf(temp, "TE %s %s %s %s %s",
	    imc_mudof(p->from), fromname, newpath, p->to,
	    imc_getkey(&p->data, "text", ""));
  else if (!strcasecmp(p->type, "who-reply"))
    sprintf(temp, "PE %s %s %s %s %s",
	    imc_mudof(p->from), fromname, newpath,
	    p->to, escape0(imc_getkey(&p->data, "text", "")));
  else if (!strcasecmp(p->type, "who"))
    sprintf(temp, "WH %s %s %s",
	    imc_mudof(p->to), imc_nameof(p->from), newpath);
  else
    return NULL;

  return temp;
}

static const char *generate1(const imc_internal * p)
{
  static char temp[IMC_PACKET_LENGTH];
  char newpath[IMC_PATH_LENGTH];

  if (!p->type[0] || !p->from[0] || !p->to[0])
  {
    imc_logerror("BUG: generate1: bad packet!");
    return NULL;		/* catch bad packets here */
  }

  if (!p->path[0])
    strcpy(newpath, imc_name);
  else
    sprintf(newpath, "%s!%s", p->path, imc_name);

  if (!strcasecmp(p->type, "chat") || !strcasecmp(p->type, "emote"))
    if (imc_getkeyi(&p->data, "channel", 0) == 0)
      sprintf(temp, "%s %ld %s %s %s %s",
	      p->from, p->sequence, newpath, p->type, p->to,
	      escape1(imc_getkey(&p->data, "text", "")));
    else
      return NULL;
  else if (!strcasecmp(p->type, "tell"))
    sprintf(temp, "%s %ld %s %s %s %s",
	    p->from, p->sequence, newpath, p->type, p->to,
	    escape1(imc_getkey(&p->data, "text", "")));
  else if (!strcasecmp(p->type, "who"))
    sprintf(temp, "%s %ld %s %s %s",
	    p->from, p->sequence, newpath, p->type, p->to);
  else if (!strcasecmp(p->type, "who-reply"))
    sprintf(temp, "%s %ld %s %s %s %s",
	    p->from, p->sequence, newpath, p->type, p->to,
	    escape1(imc_getkey(&p->data, "text", "")));
  else
    sprintf(temp, "%s %ld %s %s %s %s",
	    p->from, p->sequence, newpath, p->type, p->to,
	    printkeys(&p->data));

  return temp;
}

static const char *generate2(const imc_internal * p)
{
  static char temp[IMC_PACKET_LENGTH];
  char newpath[IMC_PATH_LENGTH];

  if (!p->type[0] || !p->from[0] || !p->to[0])
  {
    imc_logerror("BUG: generate2: bad packet!");
    return NULL;		/* catch bad packets here */
  }

  if (!p->path[0])
    strcpy(newpath, imc_name);
  else
    sprintf(newpath, "%s!%s", p->path, imc_name);

  sprintf(temp, "%s %lu %s %s %s %s",
	  p->from, p->sequence, newpath, p->type, p->to,
	  printkeys(&p->data));

  return temp;
}

static imc_internal *interpret0(const char *argument)
{
  static imc_internal out;
  char type[3];
  char arg1[IMC_NAME_LENGTH], arg2[IMC_NAME_LENGTH];
  char arg3[IMC_PATH_LENGTH];
  const char *orig=argument;

  out.sequence=imc_sequencenumber++;
  imc_initdata(&out.data);

  argument=imc_getarg(argument, type, 3);
  argument=imc_getarg(argument, arg1, IMC_NAME_LENGTH);
  argument=imc_getarg(argument, arg2, IMC_NAME_LENGTH);
  argument=imc_getarg(argument, arg3, IMC_PATH_LENGTH);

  if (!type[0] || !arg1[0] || !arg2[0] || !arg3[0])
  {
    imc_logerror("interpret0: bad packet received, discarding");
    return NULL;
  }

  if (!strcasecmp(type, "AC"))
  {
    strcpy(out.type, "emote");
    strcpy(out.to, "*@*");
    strcpy(out.from, imc_makename(arg2, arg1));
    strcpy(out.path, arg3);
    imc_addkey(&out.data, "text", argument);
    imc_addkeyi(&out.data, "channel", 0);
  }
  else if (!strcasecmp(type, "MS"))
  {
    strcpy(out.type, "chat");
    strcpy(out.to, "*@*");
    strcpy(out.from, imc_makename(arg2, arg1));
    strcpy(out.path, arg3);
    imc_addkey(&out.data, "text", argument);
    imc_addkeyi(&out.data, "channel", 0);
  }
  else if (!strcasecmp(type, "TE"))
  {
    strcpy(out.type, "tell");
    argument = imc_getarg(argument, out.to, IMC_NAME_LENGTH);
    if (!strcasecmp(arg2, arg1))	/* handle system messages */
      strcpy(out.from, imc_makename("*", arg1));
    else
      strcpy(out.from, imc_makename(arg2, arg1));
    strcpy(out.path, arg3);
    imc_addkey(&out.data, "text", argument);
  }
  else if (!strcasecmp(type, "PE"))
  {
    strcpy(out.type, "who-reply");
    argument = imc_getarg(argument, out.to, IMC_NAME_LENGTH);
    if (!strcasecmp(arg2, arg1))	/* handle system messages */
      strcpy(out.from, imc_makename("*", arg1));
    else
      strcpy(out.from, imc_makename(arg2, arg1));
    strcpy(out.path, arg3);
    imc_addkey(&out.data, "text", unescape0(argument));
  }
  else if (!strcasecmp(type, "WH"))
  {
    strcpy(out.type, "who");
    strcpy(out.to, imc_makename("*", arg1));
    strcpy(out.from, imc_makename(arg2, imc_lastinpath(arg3)));
    strcpy(out.path, arg3);
  }
  else if (!type[0] || !strcasecmp(type, "PW"))
    return NULL;
  else
  {
    imc_logerror("unknown version0 type: %s", orig);
    return NULL;
  }

  return &out;
}

static imc_internal *interpret1(const char *argument)
{
  char seq[20];
  static imc_internal out;

  imc_initdata(&out.data);

  argument=imc_getarg(argument, out.from, IMC_NAME_LENGTH);
  argument=imc_getarg(argument, seq, 20);
  argument=imc_getarg(argument, out.path, IMC_PATH_LENGTH);
  argument=imc_getarg(argument, out.type, IMC_TYPE_LENGTH);
  argument=imc_getarg(argument, out.to, IMC_NAME_LENGTH);

  if (!out.from[0] || !seq[0] || !out.path[0] || !out.type[0] || !out.to[0])
  {
    imc_logerror("interpret1: bad packet received, discarding");
    return NULL;
  }

  if (!strcasecmp(out.type, "who"))
    imc_addkey(&out.data, "level", "0");
  else if (!strcasecmp(out.type, "chat") || !strcasecmp(out.type, "emote"))
  {
    imc_addkey(&out.data, "text", unescape1(argument));
    imc_addkeyi(&out.data, "channel", 0);
  }
  else if (!strcasecmp(out.type, "tell"))
  {
    imc_addkey(&out.data, "text", unescape1(argument));
  }
  else if (!strcasecmp(out.type, "who-reply"))
    imc_addkey(&out.data, "text", unescape1(argument));
  else
    parsekeys(argument, &out.data);

  out.sequence = strtoul(seq, NULL, 10);
  return &out;
}

static imc_internal *interpret2(const char *argument)
{
  char seq[20];
  static imc_internal out;

  imc_initdata(&out.data);

  argument=imc_getarg(argument, out.from, IMC_NAME_LENGTH);
  argument=imc_getarg(argument, seq, 20);
  argument=imc_getarg(argument, out.path, IMC_PATH_LENGTH);
  argument=imc_getarg(argument, out.type, IMC_TYPE_LENGTH);
  argument=imc_getarg(argument, out.to, IMC_NAME_LENGTH);

  if (!out.from[0] || !seq[0] || !out.path[0] || !out.type[0] || !out.to[0])
  {
    imc_logerror("interpret2: bad packet received, discarding");
    return NULL;
  }

  parsekeys(argument, &out.data);

  out.sequence=strtoul(seq, NULL, 10);
  return &out;
}
