
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
 * @file connection_list.c
 * @brief
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "protocol.h"
#include "connection.h"
#include "connection_list.h"
#include "main.h"
#include "utils.h"

extern Globals globals;
extern Prefs prefs;
extern struct Connection_List conn_list;
extern struct Protocol_List g_prot_list;

void
connection_init (SConnection *pConn)
{
  memset (pConn, 0, sizeof (SConnection));
  pConn->directories = g_ptr_array_new ();
}

void
cl_init (struct Connection_List *p_cl)
{
  p_cl->head = NULL;
  p_cl->tail = NULL;
}

void
cl_release_chain (struct Connection *p_head)
{
  if (p_head)
    {
      cl_release_chain (p_head->next);
      free (p_head);
    }
}

void
cl_release (struct Connection_List *p_cl)
{
  cl_release_chain (p_cl->head);

  p_cl->head = 0;
  p_cl->tail = 0;
}

struct Connection *
cl_append (struct Connection_List *p_cl, struct Connection *p_new)
{
  struct Connection *p_new_decl;

  p_new_decl = (struct Connection *) malloc (sizeof (struct Connection));

  memset (p_new_decl, 0, sizeof (struct Connection));
  memcpy (p_new_decl, p_new, sizeof (struct Connection));

  p_new_decl->next = 0;

  if (p_cl->head == 0)
    {
      p_cl->head = p_new_decl;
      p_cl->tail = p_new_decl;
    }
  else
    {
      p_cl->tail->next = p_new_decl;
      p_cl->tail = p_new_decl;
    }

  return (p_new_decl);
}

struct Connection *
cl_insert_sorted (struct Connection_List *p_cl, struct Connection *p_new)
{
  struct Connection *p_new_decl, *p, *p_prec;

  p_new_decl = (struct Connection *) malloc (sizeof (struct Connection));

  memset (p_new_decl, 0, sizeof (struct Connection));
  memcpy (p_new_decl, p_new, sizeof (struct Connection));

  p_new_decl->next = 0;

  if (p_cl->head == 0)
    {
      p_cl->head = p_new_decl;
      p_cl->tail = p_new_decl;
    }
  else
    {
      p = p_cl->head;
      p_prec = 0;

      while (p)
        {
          if (strcasecmp (p->name, p_new_decl->name) > 0)
            {
              break;
            }

          p_prec = p;
          p = p->next;
        }

      if (p == NULL)           /* last */
        {
          p_cl->tail->next = p_new_decl;
          p_cl->tail = p_new_decl;
        }
      else
        {
          if (p == p_cl->head) /* first */
            {
              p_new_decl->next = p;
              p_cl->head = p_new_decl;
            }
          else                 /* middle */
            {
              p_prec->next = p_new_decl;
              p_new_decl->next = p;
            }
        }
    }

  return (p_new_decl);
}

struct Connection *
cl_host_search (struct Connection_List *p_cl, char *host, char *skip_this)
{
  struct Connection *p_conn;
  
  p_conn = p_cl->head;
  
  while (p_conn)
    {
      if (!strcmp (p_conn->host, host))
        { 
          if (skip_this)
            {
              if (strcmp (p_conn->name, skip_this))
                break;
            }
          else 
            break;
        }

      p_conn = p_conn->next;
    }
   
  return (p_conn);
}

struct Connection *
cl_get_by_index (struct Connection_List *p_cl, int index)
{
  struct Connection *p_conn;
  int i;

  // 

  //log_debug("list address: %ld\n", p_cl);

  p_conn = (struct Connection *) p_cl->head;
  i = 0;

  //log_debug("searching...\n");
  
  while (p_conn)
    {
      if (i == index)
        break;

      p_conn = p_conn->next;
      i ++;
    }

  //
   
  return (p_conn);
}

struct Connection *
cl_get_by_name (struct Connection_List *p_cl, char *name)
{
  struct Connection *p_conn;

#ifdef DEBUG
  //printf ("cl_get_by_index()\n");
#endif  

  p_conn = p_cl->head;
  
  while (p_conn)
    {
      if (!strcmp (p_conn->name, name))
        break;

      p_conn = p_conn->next;
    }
   
  return (p_conn);
}

/*
struct Connection *
cl_get_current () { return (&conn_list); }
*/

/**
 * cl_check() - check connections and set warnings
 */
void
cl_check (struct Connection_List *p_cl)
{
  struct Connection *p_conn, *pc;
  struct Protocol *p_protocol;

  if (!prefs.check_connections)
    return;

  p_conn = p_cl->head;
  
  while (p_conn)
    {
      p_conn->warnings = CONN_WARNING_NONE;

      if (p_conn->flags & CONN_FLAG_IGNORE_WARNINGS)
        {
          p_conn = p_conn->next;
          continue;
        }

      if (cl_host_search (p_cl, p_conn->host, p_conn->name))
        p_conn->warnings |= CONN_WARNING_HOST_DUPLICATED;

      if ((p_protocol = get_protocol (&g_prot_list, p_conn->protocol)))
        {
          if (!check_command (p_protocol->command))
            p_conn->warnings |= CONN_WARNING_PROTOCOL_COMMAND_NOT_FOUND;
        }
      else
        p_conn->warnings |= CONN_WARNING_PROTOCOL_NOT_FOUND;

      //log_debug("%s warnings = %d\n", p_conn->name, p_conn->warnings);

      p_conn = p_conn->next;
    }
}

void
cl_remove (struct Connection_List *p_cl, char *name)
{
  struct Connection *p_del, *p_prec;

  p_prec = 0;
  p_del = p_cl->head;

  while (p_del)
    {
      if (!strcmp (p_del->name, name))
        {
          if (p_prec)
            p_prec->next = p_del->next;
          else
            p_cl->head = p_del->next;

          if (p_cl->tail == p_del)
            p_cl->tail = p_prec;

          free (p_del);

#ifdef DEBUG
          printf ("cl_remove() : removed %s\n", name);
#endif

          break;
        }
 
      p_prec = p_del;
      p_del = p_del->next;
    }
}

int
cl_count (struct Connection_List *p_cl)
{
  struct Connection *c;
  int n;

  c = p_cl->head;
  n = 0;

  while (c)
    {
      n ++;
      c = c->next;
    }

  return (n);
}

void
cl_dump (struct Connection_List *p_cl)
{
  struct Connection *c;

  c = p_cl->head;

  while (c)
    {
      printf ("%s %s %s %d\n", c->name, c->host, c->protocol, c->port);
      c = c->next;
    }
}

void
connection_copy (struct Connection *p_dst, struct Connection *p_src)
{
/*
  strcpy (p_dst->name, p_src->name);
  strcpy (p_dst->host, p_src->host);
  strcpy (p_dst->protocol, p_src->protocol);
  p_dst->port = p_src->port;
  //strcpy (p_dst->emulation, p_src->emulation);
  strcpy (p_dst->last_user, p_src->last_user);
  
  //p_dst->auth = p_src->auth;  
  p_dst->auth_mode = p_src->auth_mode;  
  strcpy (p_dst->auth_user, p_src->auth_user);
  strcpy (p_dst->auth_password, p_src->auth_password);
  strcpy (p_dst->auth_password_encrypted, p_src->auth_password_encrypted);

  strcpy (p_dst->user, p_src->user);
  strcpy (p_dst->password, p_src->password);
  strcpy (p_dst->password_encrypted, p_src->password_encrypted);

  strcpy (p_dst->user_options, p_src->user_options);
  strcpy (p_dst->directory, p_src->directory);
  strcpy (p_dst->note, p_src->note);
  strcpy (p_dst->sftp_dir, p_src->sftp_dir);
  p_dst->flags = p_src->flags;  

  p_dst->warnings = p_src->warnings;

  p_dst->history.head = p_src->history.head;
  p_dst->history.tail = p_src->history.tail;

  strcpy (p_dst->upload_dir, p_src->upload_dir);
  strcpy (p_dst->download_dir, p_src->download_dir);

  p_dst->x11Forwarding = p_src->x11Forwarding;
  p_dst->disableStrictKeyChecking = p_src->disableStrictKeyChecking;
  strcpy (p_dst->identityFile, p_src->identityFile);
*/
  memcpy(p_dst, p_src, sizeof(struct Connection));
}

/**
 * connection_fill_from_string() - fills a Connetion struct with values from a connection string
 * @param[in] p_conn pointer connection to be filled
 * @param[in] connection_string string in the format "user/password@connectionname[protocol]"
 * @param[out] dest string buffer receiving the expanded string
 * @return 0 if ok, 1 otherwise
 */
int
connection_fill_from_string (struct Connection *p_conn, char *connection_string)
{
  int i, c, j;
  int step;
  char user[256];
  char password[256];
  char name[256];
  char protocol[256];
  struct Connection *p_conn_saved;
  
  memset (p_conn, 0, sizeof (struct Connection));

  strcpy (user, "");
  strcpy (password, "");
  strcpy (name, "");
  strcpy (protocol, "");
  
  step = 1; /* user */
  j = 0;
  
  for (i=0; i<strlen (connection_string); i++)
    {
      c = connection_string[i];
      
      switch (step) {
        case 1: /* user */
          if (c == '/' || c == '@')
            {
              user[j] = 0;
              j = 0;
              
              if (c == '/')
                step = 2;
              else
                step = 3;
              
              continue;
            }
          
          user[j++] = c;
          break;
          
        case 2: /* password */
          if (c == '@')
            {
              password[j] = 0;
              j = 0;
              step ++;
              continue;
            }
          
          password[j++] = c;
          break;
          
        case 3: /* name */
          if (c == '[')
            {
              name[j] = 0;
              trim (name);
              j = 0;
              step ++;
              continue;
            }

          name[j++] = c;
          break;
          
        case 4: /* protocol */
          if (c == ']')
            {
              protocol[j] = 0;
              j = 0;
              step ++;
              continue;
            }
          
          protocol[j++] = c;
          break;
      }
    }
    
  /* add trailing null to last element */

  switch (step) {
    case 1: user[j] = 0; break;
    case 2: password[j] = 0; break;
    case 3: name[j] = 0; break;
    case 4: protocol[j] = 0; break;
    default: break;
  }

  log_debug ("last step %d: %s/%s@%s[%s]\n", step, user, password, name, protocol);

  p_conn_saved = get_connection (&conn_list, name);
  
  if (p_conn_saved)
    {
      log_debug("found: %s\n", p_conn_saved->name);
      
      connection_copy (p_conn, p_conn_saved);
      strcpy (p_conn->user, user);
      strcpy (p_conn->password, password);

      /* ignore protocol for now, can be in conflict with port */
      /*
      if (protocol[0])
        {
          if (get_protocol (&g_prot_list, protocol))
            strcpy (p_conn->protocol, protocol);
          else
            return 1;
        }
      */

      log_debug ("%s/%s@%s[%s]\n", p_conn->user, p_conn->password, p_conn->name, p_conn->protocol);

      return 0;
    }
  else
    return 2;
}
/*
void init_bookmarks (struct Bookmarks *bookmarks)
{
  bookmarks->head = 0;
  bookmarks->tail = 0;
}
*/
/**
 * count_bookmarks()
 */
/*
int
count_bookmarks (struct Bookmarks *bookmarks)
{
  struct Bookmark *b;
  int n;

  b = bookmarks->head;
  n = 0;

  while (b)
    {
      n ++;
      b = b->next;
    }

  return (n);
}
*/
int
count_directories (SConnection *pConn)
{
  return (pConn->directories ? pConn->directories->len : 0);
}

/**
 * search_bookmarks() - searches for a bookmark
 */
/*
struct Bookmark*
search_bookmark (struct Bookmarks *bookmarks, char *item)
{
  struct Bookmark *b;

  b = bookmarks->head;

  while (b)
    {
      if (!strcmp (b->item, item))
        return (b);
        
      b = b->next;
    }

  return (NULL);
}
*/
int
search_directory (SConnection *pConn, gchar *item)
{
  int i;
  gchar *dir = 0;

  if (pConn->directories == NULL)
    return -1;

  for (i=0; i<pConn->directories->len; i++) {
    dir = (gchar *) g_ptr_array_index (pConn->directories, i);

    if (dir == 0)
      continue;

    //log_debug ("%s vs %s\n", dir, item);

    if (!g_strcmp0 (dir, item))
      return i;
  }

  return -1;
} 

/**
 * add_bookmark() - adds a bookmark to a connection
 */
/*
void
add_bookmark (struct Bookmarks *bookmarks, char *item)
{
  struct Bookmark *p_new_bookmark, *p_del;

  // If bookmark is present move it at the end of the list
  
  if (p_new_bookmark = search_bookmark (bookmarks, item))
    {
      if (p_new_bookmark == bookmarks->tail)
        return;
      
      if (p_new_bookmark == bookmarks->head)
        {
        //printf ("HEAD: %s\n", p_new_bookmark->item);
          p_new_bookmark->next->prev = NULL;
          bookmarks->head = p_new_bookmark->next;
        }
      else
        {
          p_new_bookmark->prev->next = p_new_bookmark->next;
          p_new_bookmark->next->prev = p_new_bookmark->prev;
        }
      
      p_new_bookmark->prev = bookmarks->tail;
      p_new_bookmark->next = NULL;
      bookmarks->tail->next = p_new_bookmark;
      bookmarks->tail = p_new_bookmark;
      
      return;
    }
    
  p_new_bookmark = (struct Bookmark *) malloc (sizeof (struct Bookmark));

  memset (p_new_bookmark, 0, sizeof (struct Bookmark));
  strcpy (p_new_bookmark->item, item);
  p_new_bookmark->next = 0;

  if (bookmarks->head == 0)
    {
      bookmarks->head = p_new_bookmark;
      bookmarks->tail = p_new_bookmark;
    }
  else
    {
      bookmarks->tail->next = p_new_bookmark;
      p_new_bookmark->prev = bookmarks->tail;
      bookmarks->tail = p_new_bookmark;
      
      if (count_bookmarks (bookmarks) > MAX_BOOKMARKS)
        {
          p_del = bookmarks->head;
          bookmarks->head = bookmarks->head->next;
          free (p_del);
        }
    }
    
  //log_debug ("added %s (prev: %s)\n", p_new_bookmark->item, p_new_bookmark->prev != NULL ? p_new_bookmark->prev->item : "none");
}
*/
void
add_directory (SConnection *pConn, char *item)
{
  int i;
  gchar *pDir;
  gpointer ptr;

  //log_debug ("%s\n", item);

  if (item == 0)
    return;

  if (pConn->directories == 0)
    pConn->directories = g_ptr_array_new ();

  // If bookmark is present, remove it so it will go at the end of the list
  i = search_directory (pConn, item);

  if (i >= 0) {
    //log_debug ("Removing %d %s\n", i, item);
    ptr = g_ptr_array_remove_index (pConn->directories, i);
    g_free (ptr);
  }

  //log_debug ("Adding %s\n", item);

  //pDir = (gchar *) malloc (strlen (item) + 1);
  pDir = g_strdup (item);
  
  g_ptr_array_add (pConn->directories, (gpointer) pDir);

  if (count_directories (pConn) > MAX_BOOKMARKS)
    g_ptr_array_remove_index (pConn->directories, 0);
} 


