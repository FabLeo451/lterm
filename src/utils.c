
/**
 * Copyright (C) 2009-2017 Fabio Leone
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * @file utils.c
 * @brief Implementa funzioni di utilità
 */

#include <glib.h>
#include <signal.h>
#include <openssl/des.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#ifndef __APPLE__
#include <crypt.h>
#endif

#include "main.h"
#include "utils.h"


/**
 * is_numeric ()
 * verifica che una stringa rappresenti un numero
 * @param[in] s stringa da controllare
 * @return 1 se la stringa è un numero, 0 altrimenti
 */

int
is_numeric (char *s)
{
  int ret, i;

  ret = 1;

  for (i = 0; i < strlen (s) && ret; i++)
    if (s[i] < '0' || s[i] > '9')
        ret = 0;

  return (ret);
}


/**
 * diff_time ()
 * calcola la differenza tra due timestamp in ore, minuti e secondi
 * @param[in] tm1 timestamp inferiore
 * @param[in] tm2 timestamp superiore
 * @param[out] hour ore
 * @param[out] min minuti
 * @param[out] sec secondi
 * @return intervallo (tm2-tm1)
 */

time_t
diff_time (time_t tm1, time_t tm2, short *hour, short *min, short *sec)
{
   time_t tdiff;
   short h, m, s;

   tdiff = tm2 - tm1;
   h = tdiff / 3600;
   *hour = h;
   m = (tdiff - 3600 * h) / 60;
   *min = m;
   s = tdiff - 3600 * h - 60 * m;
   *sec = s;

   return (tdiff);
}

char *
timestamp_to_date (char *format, time_t t)
{
  static char buff[128];
  struct tm *tml;

  tml = localtime (&t);
  
  strftime (buff, sizeof (buff), format, tml);
  
  return (buff);
}

char * 
bytes_to_human_readable (double size, char *buf) 
{
  int i = 0;
  const char* units[] = {"bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
  
  while (size > 1024) 
    {
      size /= 1024;
      i++;
    }
    
  sprintf (buf, "%.*f %s", i, size, units[i]);
  return (buf);
}

char *
seconds_to_hhmmdd (uint64_t seconds, char *buf)
{
  uint64_t hour, min, sec;
  
  hour = seconds / 3600;
  seconds = seconds % 3600;
  min = seconds / 60;
  seconds = seconds % 60;
  sec = seconds;
  
  sprintf (buf, "%02lld:%02lld:%02lld", hour, min, sec);
  
  return (buf);
}

char *
permissions_octal_to_string (uint32_t value, char *buf)
{
  int i, n;
  char triple[16], *flags = "rwxrwxrwx";

  for (i=0; i<9; i++)
    {
      triple[i] = (value & (0x01 << 8-i)) ? flags[i] : '-';
    }
    
  triple[9] = 0;
  
  strcpy (buf, triple);
  
  return (buf);
}

/**
 * duplicate_apx ()
 * duplica gli apici presenti in una stringa
 * @param s1 stringa originale
 * @param s2 stringa con apici duplicati
 */

void
duplicate_apx (char *s1, char *s2)
{
  char temp[2048];
  int i, j;

  j = 0;

  for (i = 0; i < strlen (s1); i++)
    {
      if (s1[i] == '\'')
        temp[j++] = '\'';

      temp[j++] = s1[i];
    }

  temp[j] = '\0';

  strcpy (s2, temp);
}

char *replace_str (const char *str, const char *old, const char *new)
{
	char *ret, *r;
	const char *p, *q;
	size_t oldlen = strlen(old);
	size_t count, retlen, newlen = strlen(new);

	if (oldlen != newlen) {
		for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
			count++;
		/* this is undefined if p - str > PTRDIFF_MAX */
		retlen = p - str + strlen(p) + count * (newlen - oldlen);
	} else
		retlen = strlen(str);

	if ((ret = malloc(retlen + 1)) == NULL)
		return NULL;

	for (r = ret, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen) {
		/* this is undefined if q - p > PTRDIFF_MAX */
		ptrdiff_t l = q - p;
		memcpy(r, p, l);
		r += l;
		memcpy(r, new, newlen);
		r += newlen;
	}
	strcpy(r, p);

	return ret;
}

/**
 * cut_newline ()
 * tronca una stringa all'ultimo carattere che precede
 * un linefeed o un carriage return
 * @param s stringa da tagliare
 */

void
cut_newline (char *s)
{
  int i;

  for (i=0; i<strlen (s); i++)
    if (s[i] == '\n' || s[i] == '\r' || s[i] == '\0')
      {
         s[i] = '\0';
         break;
      }
}

void
ltrim (char *s)
{
  int i;
  char *pc;

  i = 0;
  pc = &s[i];

  while (*pc != 0 && *pc == ' ')
    pc = &s[++i];
    
  strcpy (s, pc);
}

void
rtrim (char *s)
{
  int i;

  i = strlen (s) - 1; /* last char */

  while (i >= 0 && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f'))
    i --;

  /* now whe are on the last good char */

  s[i + 1] = 0;
}

void
trim (char *s)
{
  ltrim (s);
  rtrim (s);
}

void
list_init (char *list)
{
  strcpy (list, "");
}


int
list_count (char *list, char sep)
{
  int i;
  int n;

  n = 0;

  if (strlen (list) > 0)
    {
      for (i=0; i<strlen (list); i++)
        {
          if (list[i] == sep)
            n++;
        }
      
      n++; /* # elementi = # separatori + 1 */
    }

  return (n);
}

int
list_get_nth (char *list, int n, char sep, char *elem)
{
  int n_cur;
  char *pc;
  char *pstart;
  char *pend;
  //char tmp[2048*5];
  char *tmp = NULL;

  strcpy (elem, "");
  pstart = list;

  n_cur = 1;

  while (pstart)
    {
      if (n_cur == n)
        {
#ifdef DEBUG
          //printf ("list_get_nth(): n_cur = %d pstart = %s\n", n_cur, pstart);
#endif

          tmp = (char *) malloc (strlen (pstart)+1);
          strcpy (tmp, pstart);
          
#ifdef DEBUG
          //printf ("list_get_nth(): tmp = %s\n", tmp);
#endif

          pend = (char *) strchr (tmp, sep);
#ifdef DEBUG
          //printf ("list_get_nth(): pend = %ld\n", pend);
#endif

          if (pend)
            {
              *pend = 0;
            }

          strcpy (elem, tmp);
          free (tmp);
#ifdef DEBUG
          //printf ("list_get_nth(): elem = %s\n", elem);
#endif

          break;
        }

      pc = (char *) strchr (pstart, sep);

      if (pc)
        pstart = pc+1;
      else
        pstart = 0;

      n_cur++;
    }

  return (pstart ? 1 : 0);
}

int
list_get_nth_not_null (char *list, int n, char sep, char *elem)
{
  int cnt, i, item_i;
  char elem_tmp[256];

  strcpy (elem_tmp, "");
  cnt = list_count (list, sep);

  if (n > cnt)
    return 0;

  item_i = 0;

  for (i=1; i<=cnt; i++)
    {
      list_get_nth (list, i, sep, elem_tmp);
      
      if (elem_tmp[0] != '\0')
	{
	  item_i ++;

	  if (item_i == n)
	    break;
	}
    }

  strcpy (elem, elem_tmp);

  return (item_i == n ? 1 : 0);
}

int
in_list (char *item, char *list, char sep)
{
  int n, i, found;
  char item_tmp[1024];

  found = 0;
  n = list_count (list, sep);

  for (i=1;i<=n;i++)
    {
      list_get_nth (list, i, sep, item_tmp);

      if (!strcmp (item, item_tmp))
        found = 1;
    }
  
  return (found);
}

void
list_append_item (char *item, char *list, char sep)
{
  int n;
  char list_tmp[2048];

  n = list_count (list, sep);

  if (n)
    {
      sprintf (list_tmp, "%s%c%s", list, sep, item);
      strcpy (list, list_tmp);
    }
  else
    strcpy (list, item);
}

void
list_remove_item (char *item, char *list, char sep)
{
  int n, i;
  char item_tmp[1024];
  char list_src[2048];

  n = list_count (list, sep);

  if (n)
    {
      strcpy (list_src, list);
      list_init (list);

      for (i=1;i<=n;i++)
        {
          list_get_nth (list_src, i, sep, item_tmp);

          if (strcmp (item, item_tmp))
            list_append_item (item_tmp, list, sep);
        }
    }
}

int
timestamp_from_date (char *date, int with_time)
{
  time_t ts_t;
  struct tm *tm_date;
  char tmp[64];

  ts_t = time (NULL);
  tm_date = localtime (&ts_t);

  tmp[0] = date[0];
  tmp[1] = date[1];
  tmp[2] = 0;
  tm_date->tm_mday = atoi (tmp);

  tmp[0] = date[3];
  tmp[1] = date[4];
  tmp[2] = 0;
  tm_date->tm_mon = atoi (tmp) - 1;
     
  tmp[0] = date[6];
  tmp[1] = date[7];
  tmp[2] = date[8];
  tmp[3] = date[9];
  tmp[4] = 0;
  tm_date->tm_year = atoi (tmp) - 1900;

  if (with_time)
    {
      tmp[0] = date[11];
      tmp[1] = date[12];
      tmp[2] = 0;
      tm_date->tm_hour = atoi (tmp);

      tmp[0] = date[14];
      tmp[1] = date[15];
      tmp[2] = 0;
      tm_date->tm_min = atoi (tmp);

      tmp[0] = date[17];
      tmp[1] = date[18];
      tmp[2] = 0;
      tm_date->tm_sec = atoi (tmp);
    }
  else
    {
      tm_date->tm_hour = 0;
      tm_date->tm_min = 0;
      tm_date->tm_sec = 0;
    }

  return (mktime (tm_date));
}

void
reverse (char *s)
{
  int i, c, len;

  len = strlen (s);

  for (i=0;i<len/2;i++)
    {
      c = s[i];
      s[i] = s[len - i - 1];
      s[len - i - 1] = c;
    }
}

void
lower (char *s)
{
  int i;
  
  for (i = 0; i < strlen (s); i ++)
    s[i] = tolower (s[i]);

  s[i] = 0;
}

void
upper (char *s)
{
  int i;
  
  for (i = 0; i < strlen (s); i ++)
    s[i] = toupper (s[i]);

  s[i] = 0;
}

/**
 * split_string()
 * Split a string into an array
 * "splitted" should be initialized to null and freed after beeng used
 */ 
void
split_string (char *str, char **splitted, char *delimiters/*, int skipNulls*/)
{
  char *p = strtok (str, delimiters);
  int n_spaces = 0, i;

  /* split string and append tokens to 'res' */

  while (p) {
    splitted = realloc (splitted, sizeof (char*) * ++n_spaces);

    if (splitted == NULL)
      exit (-1); /* memory allocation failed */

    splitted[n_spaces-1] = p;

    p = strtok (NULL, delimiters);
  }

  /* realloc one extra element for the last NULL */

  splitted = realloc (splitted, sizeof (char*) * (n_spaces+1));
  splitted[n_spaces] = 0;


for (i = 0; i < (n_spaces+1); ++i)
  printf ("splitted[%d] = %s\n", i, splitted[i]);
}


char **
splitString (char *str, char *delimiters, int skipNulls, char *quotes, int trailingNull, int *pCount)
{
  char **splitted = NULL;
  int i = 0, k, t, n = 0;
  char *pstart, *tmp = NULL;
  int insideQuotes = 0;

  if (str == NULL)
    return 0;

  while (i < strlen(str))
    {
      pstart = &str[i];

//printf("pstart = %s\n", pstart);

      // Get next token
      insideQuotes = 0;
      k = 0;
      t = 0;
      tmp = (char *) malloc (strlen (pstart)+1);
      memset (tmp, 0, strlen (pstart)+1);
      while (pstart[k] != 0) {

        // If the first char is a quote go ahead
        if (quotes && k == 0 && strchr (quotes, pstart[k])) {
          insideQuotes = 1; // Enter the quoted string
          k ++;
          continue;
        }

        if (quotes && insideQuotes && strchr (quotes, pstart[k])) {
          insideQuotes = 0; // Exit the quoted string
        }

        if (!insideQuotes && strchr (delimiters, pstart[k])) {
          k ++;
          break;
        }

        if (!quotes || (quotes && !strchr (quotes, pstart[k])))
          {
            if (insideQuotes || (!insideQuotes && !strchr (delimiters, pstart[k])))
              tmp[t++] = pstart[k];
          }

        k++;

        if (!insideQuotes && strchr (delimiters, pstart[k])) {
          k++;
          break;
        }
      }

      tmp[t] = 0;
//printf("i = %d k = %d tmp = %s\n", i, k, tmp);


      i += k;

      //if (!skipNulls || (skipNulls && tmp[0])) {
      if (tmp[0] || (tmp[0] == 0 && !skipNulls)) {
        splitted = realloc (splitted, sizeof (char *) * n+1);
        splitted[n] = (char *) malloc (strlen (tmp)+1);
        memcpy(splitted[n], tmp, strlen (tmp)+1);
        free (tmp);
       //printf ("i = %d splitted[%d] = '%s'\n", i, n, splitted[n]);
        n ++;
      }

//printf("i=%d strlen(str)=%d\n", i, strlen(str));

    }

  if (trailingNull) {
    splitted = realloc (splitted, sizeof (char *) * n+1);
    splitted[n] = 0;
  }

  if (pCount)
    *pCount = n;

//for (int i = 0; i < (n); ++i)
//  printf ("splitted[%d] = %s\n", i, splitted[i]);

  return (splitted);
}


/*
char *
get_desktop ()
{
  if (getenv ("GNOME_KEYRING_CONTROL"))
    return "GNOME";
  else if (getenv ("KDE_FULL_SESSION"))
    return "KDE";
  else
    return "OTHER";
}
*/

int
get_desktop_environment ()
{
  int de = DE_UNKNOWN;
  char *value;
  
#ifdef __APPLE__
  return (DE_MAC_OS_X);
#endif
  
  /* try desktop_session value */
  
  if (value = getenv ("DESKTOP_SESSION"))
    {
      if (!strcmp (value, "gnome"))
        return (DE_GNOME);
      else if (!strcmp (value, "xfce") || !strcmp (value, "xubuntu"))
        return (DE_XFCE);
      else if (strstr (value, "kde") != NULL)
        return (DE_KDE);
    }
  
  /* try more */
  
  if (value = getenv ("GNOME_KEYRING_PID"))
    return (DE_GNOME);

  if (getenv ("CINNAMON_VERSION"))
    return (DE_CINNAMON);
  
  if (getenv ("KDE_FULL_SESSION"))
    return (DE_KDE);
  
  return (de);
}

char *
get_desktop_environment_name (int id)
{
  switch (id) {
    case DE_GNOME: return "GNOME"; break;
    case DE_KDE: return "KDE"; break;
    case DE_XFCE: return "XFCE"; break;
    case DE_CINNAMON: return "Cinnamon"; break;
    case DE_MAC_OS_X: return "MAC OS X"; break;
    default: return "unknown"; break;
  }

  return ("unknown");
}

char *
shortenString (char *original, int threshold, char *shortened)
{
  char *between = "[...]";
  int keepLength, newLenght;

  if (strlen (original) <= threshold) {
    strcpy (shortened, original);
  }
  else {
    keepLength = (threshold - strlen (between)) / 2;
    newLenght = 2*keepLength + strlen (between) + 1;
    newLenght += 3; // For safety
    //shortened = (char *)malloc(len);
    memset (shortened, 0, newLenght);
    strcpy (shortened, "");
    memcpy (shortened, original, keepLength);
    strcat (shortened, between);
    strcat (shortened, &original[strlen (original)-keepLength]);
  }

  return shortened;
}

char *
Encrypt (char *Key, char *Msg, int size)
{
  static char *Res;
  int n = 0;
  DES_cblock Key2;
  DES_key_schedule schedule;

  Res = (char *) malloc (size);

  /* Prepare the key for use with DES_cfb64_encrypt */
  memcpy (Key2, Key, 8);
  DES_set_odd_parity (&Key2);
  DES_set_key_checked (&Key2, &schedule);

  /* Encryption occurs here */
  DES_cfb64_encrypt ((unsigned char *) Msg, (unsigned char *) Res, size, &schedule, &Key2, &n, DES_ENCRYPT);

  return (Res);
}

char *
Decrypt (char *Key, char *Msg, int size)
{
  static char* Res;
  int n = 0;

  DES_cblock Key2;
  DES_key_schedule schedule;

  Res = (char *) malloc (size);

  /* Prepare the key for use with DES_cfb64_encrypt */
  memcpy (Key2, Key, 8);
  DES_set_odd_parity (&Key2);
  DES_set_key_checked (&Key2, &schedule);

  /* Decryption occurs here */
  DES_cfb64_encrypt (( unsigned char *) Msg, (unsigned char *) Res, size, &schedule, &Key2, &n, DES_DECRYPT);

  return (Res);
}

char *
des_encrypt_b64 (char *clear_text)
{
  unsigned char *p_enc;
  char *p_enc_b64;

  if (strlen (clear_text) == 0)
    return ("");

  p_enc = Encrypt (KEY, clear_text, strlen (clear_text));

  if (strlen (p_enc) > strlen (clear_text))
    p_enc[strlen (clear_text)] = 0;

  p_enc_b64 = g_base64_encode ((const unsigned char *) p_enc, strlen (p_enc));

  //log_debug("%s -> %s\n", clear_text, p_enc_b64);

  return (p_enc_b64);
}

char *
des_decrypt_b64 (char *ecrypted_text)
{
  static char clear_text[4096];
  char *p_enc, *p_decr;
  gsize len;
  
  if (strlen (ecrypted_text) == 0)
    return ("");

  p_enc = g_base64_decode (ecrypted_text, &len);

  p_decr = (char *) Decrypt (KEY, p_enc, (int)len/*strlen (p_enc)*/);
  
  free (p_enc);
  
  strcpy (clear_text, p_decr);
  //memcpy (clear_text, p_decr/*(char *) Decrypt (KEY, p_enc, strlen (p_enc))*/, len);
  //clear_text[len] = 0;
  
  return (clear_text);
}

int
file_exists (char *filename)
{
  if (access (filename, R_OK) != -1)
    return (1);
  else
    return (0);
}

int
findin (char *dir, char *cp)
{
  DIR *dirp;
  struct direct *dp;
  char *d, *dd;
  int l;
  char dirbuf[1024];
  struct stat statbuf;

  dd = index (dir, '*');

  if (!dd)
    goto noglob;

  l = strlen (dir);

  if (l < sizeof (dirbuf))      /* refuse excessively long names */
    {
      strcpy (dirbuf, dir);

      d = index (dirbuf, '*');
      *d = 0;

      dirp = opendir (dirbuf);

      if (dirp == NULL)
        return (0);
      
      //log_debug("dirbuf=%s\n", dirbuf);

      while ((dp = readdir (dirp)) != NULL)
        {
          if (!strcmp (dp->d_name, ".") || !strcmp (dp->d_name, ".."))
            continue;

          if (strlen (dp->d_name) + l > sizeof (dirbuf))
            continue;

          sprintf (d, "%s", dp->d_name);

          if (stat (dirbuf, &statbuf))
            continue;

          if (!S_ISDIR (statbuf.st_mode))
            continue;

          strcat (d, dd + 1);
          findin (dirbuf, cp);
        }

      closedir (dirp);
    }

  return (0);

noglob:
  dirp = opendir (dir);

  if (dirp == NULL)
    return (0);

  while ((dp = readdir (dirp)) != NULL)
    {
      //log_debug("dir=%s\n", dir);
      if (!strcmp (cp, dp->d_name))
        {
          closedir (dirp);
          return (1);
        }
    }

  closedir (dirp);

  return (0);
}

int
find (char **dirs, char *cp)
{
  int found = 0;

  while (*dirs && !found)
    found = findin (*dirs++, cp);

  return (found);
}

int
check_command (char *command) 
{
  int i, l, found = 0;
  char *env_path;
  char path[10000];

  /* in case of absolute path try if command can be immediately found */
  
  if (access (command, R_OK|X_OK) != -1)
    {
      return (1);
    }

  /* not found, search in known paths */
  
  l = strlen ((char *) getenv ("PATH"));

#ifdef DEBUG
  //printf ("check_command(): l = %d\n", l);
#endif

  env_path = (char *) malloc (l+1);

  strcpy (env_path, (char *) getenv ("PATH"));

  for (i=1;i<list_count (env_path, ':') && !found;i++)
    {
      list_get_nth (env_path, i, ':', path);
      
#ifdef DEBUG
      //printf ("check_command(): path : %s\n", path);
#endif

      found = findin (path, command);
    }
    
  free (env_path);
  
  return (found);
}

void
get_system (char *sys_name)
{
  strcpy (sys_name, "");
  
#ifdef __linux__
  strcpy (sys_name, "Linux");
#endif
  
#ifdef __APPLE__
  strcpy (sys_name, "Mac OS X");
#endif
  
#if defined WIN32
  strcpy (sys_name, "Windows");
#endif
}

char *
readFile (filename)
{
  char * buffer = 0;
  long length;
  FILE * f = fopen (filename, "rb");

  if (f)
    {
      fseek (f, 0, SEEK_END);
      length = ftell (f);
      fseek (f, 0, SEEK_SET);
      
      buffer = malloc (length);
      
      if (buffer)
        {
          fread (buffer, 1, length, f);
        }
        
      fclose (f);
    }
    
  return (buffer);
}

