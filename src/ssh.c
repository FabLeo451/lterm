
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
 * @file ssh.c
 * @brief SSH module
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>
#include <errno.h>
#include "main.h"
#include "utils.h"
#include "ssh.h"
#include "sftp-panel.h"
#include "connection_list.h"

extern Globals globals;
extern Prefs prefs;

/* SSH List functions */

void
ssh_list_init (struct SSH_List *p_ssh_list)
{
  p_ssh_list->head = NULL;
  p_ssh_list->tail = NULL;
}

void
ssh_list_release_chain (struct SSH_Node *p_head)
{
  if (p_head)
    {
      ssh_list_release_chain (p_head->next);
      free (p_head);
    }
}

void
ssh_list_release (struct SSH_List *p_ssh_list)
{
  if (!p_ssh_list->head)
    return;

  ssh_list_release_chain (p_ssh_list->head);

  p_ssh_list->head = 0;
  p_ssh_list->tail = 0;
}

struct SSH_Node *
ssh_list_append (struct SSH_List *p_ssh_list, struct SSH_Node *p_new)
{
  struct SSH_Node *p_new_decl;

  p_new_decl = (struct SSH_Node *) malloc (sizeof (struct SSH_Node));

  memset (p_new_decl, 0, sizeof (struct SSH_Node));
  memcpy (p_new_decl, p_new, sizeof (struct SSH_Node));

  p_new_decl->next = 0;

  if (p_ssh_list->head == 0)
    {
      p_ssh_list->head = p_new_decl;
      p_ssh_list->tail = p_new_decl;
    }
  else
    {
      p_ssh_list->tail->next = p_new_decl;
      p_ssh_list->tail = p_new_decl;
    }
    
  return (p_new_decl);
}

void
ssh_list_remove (struct SSH_List *p_ssh_list, struct SSH_Node *p_node)
{
  struct SSH_Node *p_del, *p_prec;

  p_prec = NULL;
  p_del = p_ssh_list->head;

  while (p_del)
    {
      if (p_node == p_del)
        {
          if (p_prec)
            p_prec->next = p_del->next;
          else
            p_ssh_list->head = p_del->next;

          if (p_ssh_list->tail == p_del)
            p_ssh_list->tail = p_prec;

          free (p_del);
          break;
        }
 
      p_prec = p_del;
      p_del = p_del->next;
    }
}

struct SSH_Node *
ssh_list_search (struct SSH_List *p_ssh_list, char *host, char *user)
{
  struct SSH_Node *node;

  node = p_ssh_list->head;

  while (node)
    {
      if (!strcmp (node->host, host) && !strcmp (node->user, user))
        return (node);

      node = node->next;
    }

  return (NULL);
}

void
ssh_list_dump (struct SSH_List *p_ssh_list)
{
  struct SSH_Node *node;
  int valid;

  node = p_ssh_list->head;

  while (node)
    {
      valid = ssh_is_connected (node->session) && node->valid;
      
      if (valid)
        {
          ssh_channel c;
          
          if (c = ssh_node_open_channel (node))
            {
              ssh_channel_close (c);
              ssh_channel_free (c);
            }
          else
            valid = 0;
        }
        
      printf ("%s@%s (%d) %s\n", 
              node->user, node->host, node->refcount, 
              valid ? "valid" : "invalid");
      
      node = node->next;
    }
}

void
ssh_list_keepalive (struct SSH_List *p_ssh_list)
{
  struct SSH_Node *node;
  int rc;

  node = p_ssh_list->head;

  while (node)
    {
      if ((rc = ssh_node_keepalive (node)) != 0)
        {
          log_debug ("can't ping %s rc=%d\n", node->host, rc);
          //node->session = NULL;
          //node->sftp = NULL;
        }
        
      node = node->next;
    }
}

/**
 * ssh_node_connect() - connect to server and add node to list
 */
struct SSH_Node *
ssh_node_connect (struct SSH_List *p_ssh_list, struct SSH_Auth_Data *p_auth)
{
  struct SSH_Node node, *p_node = NULL;
  GError *error = NULL;
  int rc, valid = 0;

  ////////////////////////////////
  //lockSSH (__func__, TRUE);
  
  memset (&node, 0, sizeof (struct SSH_Node));
  
  if (p_node = ssh_list_search (p_ssh_list, p_auth->host, p_auth->user))
    {
      log_write ("Found ssh node for %s@%s\n", p_auth->user, p_auth->host);

      if (p_auth->mode == CONN_AUTH_MODE_PROMPT && strcmp (p_auth->password, p_node->password) != 0)
        {
          strcpy (p_auth->error_s, "Wrong password");
          p_auth->error_code = SSH_ERR_AUTH;
          
          return (NULL);
        }
        
      /* check node validity */
      
      //if (valid)
      //  {
          ssh_channel c;
          
          log_write ("Tryng to open a channel on %s@%s\n", p_auth->user, p_auth->host);
          
          sftp_spinner_refresh ();
          
          if (c = ssh_node_open_channel (p_node))
            {
              log_write ("Channel successfully opened, close it and return\n");
              
              ssh_channel_close (c);
              ssh_channel_free (c);
              ssh_node_ref (p_node);
              return (p_node);
            }
          else
            valid = 0;
      //  }
    
      if (!valid) {
        log_write ("Not a valid node for to %s@%s, recreate it\n", p_auth->user, p_auth->host);
        node.refcount = p_node->refcount;
        ssh_node_free (p_node);
      }
    }

  log_write ("Creating a new ssh node for %s@%s\n", p_auth->user, p_auth->host);
  
  node.session = ssh_new ();
  
  if (node.session == NULL)
    return (NULL);
  
  ssh_options_set (node.session, SSH_OPTIONS_HOST, p_auth->host);
  ssh_options_set (node.session, SSH_OPTIONS_USER, p_auth->user);
  ssh_options_set (node.session, SSH_OPTIONS_PORT, &p_auth->port);
  ssh_options_set (node.session, SSH_OPTIONS_TIMEOUT, &prefs.ssh_timeout);

  sftp_set_status ("Connecting to %s@%s...", p_auth->user, p_auth->host);
  
  rc = ssh_connect (node.session);
  
  if (rc != SSH_OK)
    {
      sprintf (p_auth->error_s, "%s", ssh_get_error (node.session));
      p_auth->error_code = SSH_ERR_CONNECT;
      ssh_free (node.session);
      return (NULL);
    }
    
  // Verify the server's identity
  /*
  if (verify_knownhost (my_ssh_session) < 0)
    {
      ssh_disconnect (my_ssh_session);
      ssh_free (my_ssh_session);
      exit(-1);
    }
  */

l_auth:    
  sftp_set_status ("Authenticating %s@%s...", p_auth->user, p_auth->host);
  
  /* get authentication methods */
  while (ssh_userauth_none (node.session, NULL) == SSH_AUTH_AGAIN)
    sftp_spinner_refresh ();
  
  if (p_auth->mode == CONN_AUTH_MODE_KEY)
    {
      log_write ("Authentication by key\n");
      sftp_spinner_refresh ();

      if (p_auth->identityFile[0])
        ssh_options_set (node.session, SSH_OPTIONS_IDENTITY, p_auth->identityFile);
      
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 6, 0)
      rc = ssh_userauth_publickey_auto (node.session, NULL, NULL);
#else
      rc = ssh_userauth_autopubkey (node.session, NULL);
#endif
    }
  else
    {
      sftp_spinner_refresh ();
      node.auth_methods = ssh_userauth_list (node.session, NULL);
      
      log_write ("auth methods for %s@%s: %d\n", p_auth->user, p_auth->host, node.auth_methods);
      
      if (node.auth_methods & SSH_AUTH_METHOD_PASSWORD)
        {
          gsize bytes_read, bytes_written;
          
          //rc = ssh_userauth_password (node.session, NULL, p_auth->password);
          sftp_spinner_refresh ();
          rc = ssh_userauth_password (node.session, NULL, 
                                      g_convert (p_auth->password, strlen (p_auth->password), 
                                                 "UTF8", "ISO-8859-1", &bytes_read, &bytes_written, &error)
                                     );
          
          log_write ("auth method password returns %d\n", rc);
        }
      else if (node.auth_methods & SSH_AUTH_METHOD_INTERACTIVE)
        {
          sftp_spinner_refresh ();
          rc = ssh_userauth_kbdint (node.session, NULL, NULL);
          
          log_write ("auth method interactive: ssh_userauth_kbdint() returns %d\n", rc);
          
          if (rc == SSH_AUTH_INFO)
            {
              sftp_spinner_refresh ();
              ssh_userauth_kbdint_setanswer (node.session, 0, p_auth->password);

              sftp_spinner_refresh ();
              rc = ssh_userauth_kbdint (node.session, NULL, NULL); 
              
              log_write ("auth method interactive: ssh_userauth_kbdint() returns %d\n", rc);
            }
          else
            rc = SSH_AUTH_ERROR;

          log_write ("auth method interactive returns %d\n", rc);
        }
      else
        {
          sprintf (p_auth->error_s, "Unknown authentication method for server %s\n", p_auth->host);
          p_auth->error_code = SSH_ERR_UNKNOWN_AUTH_METHOD;
          ssh_disconnect (node.session);
          ssh_free (node.session);
          return (NULL);
        }
    }
    
  if (rc != SSH_AUTH_SUCCESS)
    {
      sprintf (p_auth->error_s, "Authentication error %d: %s", rc, ssh_get_error (node.session));
      
      if (rc == SSH_AUTH_AGAIN)
        {
          log_write ("%s: SSH_AUTH_AGAIN\n", p_auth->host);
          sftp_spinner_refresh ();
          goto l_auth;
        }
      
      p_auth->error_code = SSH_ERR_AUTH;
      ssh_disconnect (node.session);
      ssh_free (node.session);
      return (NULL);
    }

  /* create an sftp session */

  sftp_set_status ("Creating sftp session on %s@%s...", p_auth->user, p_auth->host);

  node.sftp = sftp_new (node.session);
  
  if (node.sftp == NULL)
    {
      sprintf (p_auth->error_s, "%s", ssh_get_error (node.session));
      node.sftp = NULL;
      //return (1);
    }
  else
    {
      sftp_set_status ("Initializing SFTP session on %s@%s...", p_auth->user, p_auth->host);
      
      rc = sftp_init (node.sftp);
      
      if (rc != SSH_OK)
        {
          sprintf (p_auth->error_s, "%d", sftp_get_error (node.sftp));
          sftp_free (node.sftp);
          node.sftp = NULL;
          //return (2);
        }
        
      sftp_set_status ("%s", rc == 0 ? "sftp connected" : p_auth->error_s);
    }

  if (p_node)
    memcpy (p_node, &node, sizeof (struct SSH_Node)); /* recreated */
  else
    p_node = ssh_list_append (p_ssh_list, &node); /* new node */
  
  strcpy (p_node->user, p_auth->user);
  strcpy (p_node->password, p_auth->password);
  strcpy (p_node->host, p_auth->host);
  p_node->port = p_auth->port;
  
  p_node->refcount = node.refcount + 1;
  p_node->valid = 1;
  
  ssh_node_update_time (p_node);

  //lockSSH (__func__, FALSE);
  ////////////////////////////////

  return (p_node);
}

void
ssh_node_free (struct SSH_Node *p_ssh_node)
{
  ////////////////////////////////
  //lockSSH (__func__, TRUE);

  if (p_ssh_node->sftp)
    {
      sftp_free (p_ssh_node->sftp);
      p_ssh_node->sftp = NULL;
    }
    
  if (p_ssh_node->session)
    {    
      if (ssh_is_connected (p_ssh_node->session))
        {
          log_write ("disconnecting node %s@%s\n", p_ssh_node->user, p_ssh_node->host);
          ssh_disconnect (p_ssh_node->session);
        }

      log_debug ("releasing node memory\n");
      
      ssh_free (p_ssh_node->session);
      p_ssh_node->session = NULL;
    }
    
  p_ssh_node->refcount = 0;
  ssh_node_set_validity (p_ssh_node, 0);
  
  //lockSSH (__func__, FALSE);
  ////////////////////////////////
}

void 
ssh_node_ref (struct SSH_Node *p_ssh_node)
{
  p_ssh_node->refcount ++;
}

void 
ssh_node_unref (struct SSH_Node *p_ssh_node)
{
  p_ssh_node->refcount --;
  
  if (p_ssh_node->refcount == 0)
    {
      log_write ("Removing watch file descriptors for %s@%s if any\n", p_ssh_node->user, p_ssh_node->host);

      int nDel = sftp_panel_mirror_file_clear (p_ssh_node, 0);

      log_write ("Removed %d\n", nDel);

      log_debug ("Removing node %s@%s\n", p_ssh_node->user, p_ssh_node->host);
      
      ssh_node_free (p_ssh_node);
      ssh_list_remove (&globals.ssh_list, p_ssh_node);
    }
}

void
ssh_node_set_validity (struct SSH_Node *p_ssh_node, int valid)
{
  p_ssh_node->valid = valid;
}

int
ssh_node_get_validity (struct SSH_Node *p_ssh_node)
{
  return (p_ssh_node->valid);
}
/*
int sTimeout = 0;
void 
AlarmHandler (int sig) 
{ 
  sTimeout = 1; 
} 
*/
ssh_channel
ssh_node_open_channel (struct SSH_Node *p_node)
{
  ssh_channel channel;
  int rc;

  ////////////////////////////////
  //lockSSH (__func__, TRUE);

  log_write ("Opening channel on %s@%s\n", p_node->user, p_node->host);

  if ((channel = ssh_channel_new (p_node->session)) == NULL)
    {
      ssh_node_set_validity (p_node, 0);
      return (NULL);
    }
/*
  signal (SIGALRM, AlarmHandler); 
  sTimeout = 0; 
  alarm (2); 
*/
  timerStart (2);

  rc = ssh_channel_open_session (channel);
  
  if (timedOut ())
    {
      log_write ("Timeout!\n");
      rc = SSH_ERROR;
    }
/* 
  signal (SIGALRM, SIG_DFL);
  sTimeout = 0; 
  alarm (0); 
*/
  timerStop ();

  if (rc == SSH_ERROR)
    { 
      log_write ("error: can't open channel on %s@%s\n", p_node->user, p_node->host);
      ssh_channel_free (channel);
      ssh_node_set_validity (p_node, 0);
      return (NULL);
    }

  //lockSSH (__func__, FALSE);
  ////////////////////////////////

  return (channel);
}

int
ssh_node_keepalive (struct SSH_Node *p_ssh_node)
{
  ssh_channel channel;
  
  if (p_ssh_node->session == NULL)
    return (1);
    
  log_write ("[%s] %s\n", __func__, p_ssh_node->host);

  channel = ssh_channel_new (p_ssh_node->session);
  
  if (channel == NULL)
    return (2);
    
  if (ssh_channel_open_session (channel) == SSH_ERROR)
    {
      ssh_channel_free (channel);
      return (3);
    }
/*
  if (ssh_channel_request_env (p_ssh_node->channel, "__TEST", "test") == SSH_ERROR 
      && ssh_get_error_code (p_ssh_node->session) == SSH_FATAL)
    {
      //ssh_channel_close (channel);
      //ssh_channel_free (channel);
      return (4);
    }
*/ 
  ssh_channel_close (channel);
  ssh_channel_free (channel);
    
  return (0);
}

void
ssh_node_update_time (struct SSH_Node *p_ssh_node)
{
  if (p_ssh_node)
    p_ssh_node->last = time (NULL);
  
  log_write ("%s: timestamp updated\n", p_ssh_node->host);
}

/* Directory list functions */

void
dl_init (struct Directory_List *p_dl)
{
  p_dl->head = NULL;
  p_dl->tail = NULL;
  
  p_dl->count = 0;
}

void
dl_release_chain (struct Directory_Entry *p_head)
{
  if (p_head)
    {
      dl_release_chain (p_head->next);
      free (p_head);
    }
}

void
dl_release (struct Directory_List *p_dl)
{
  if (!p_dl->head)
    return;

  dl_release_chain (p_dl->head);

  p_dl->head = 0;
  p_dl->tail = 0;
}

void
dl_append (struct Directory_List *p_dl, struct Directory_Entry *p_new)
{
  struct Directory_Entry *p_new_decl;

  p_new_decl = (struct Directory_Entry *) malloc (sizeof (struct Directory_Entry));

  memset (p_new_decl, 0, sizeof (struct Directory_Entry));
  memcpy (p_new_decl, p_new, sizeof (struct Directory_Entry));

  p_new_decl->next = 0;

  if (p_dl->head == 0)
    {
      p_dl->head = p_new_decl;
      p_dl->tail = p_new_decl;
    }
  else
    {
      p_dl->tail->next = p_new_decl;
      p_dl->tail = p_new_decl;
    }
    
  if (!is_hidden_file (p_new))
    p_dl->count ++;
}

int 
is_hidden_file (struct Directory_Entry *entry)
{
  return (entry->name[0] == '.');
}

int 
is_directory (struct Directory_Entry *entry)
{
  return (entry->type == SSH_FILEXFER_TYPE_DIRECTORY);
}

struct Directory_Entry *
dl_search_by_name (struct Directory_List *p_dl, char *name)
{
  struct Directory_Entry *e;

  e = p_dl->head;

  while (e)
    {
      if (!strcmp (e->name, name))
        return (e);

      e = e->next;
    }

  return (NULL);
}

void
dl_dump (struct Directory_List *p_dl)
{
  struct Directory_Entry *e;

  e = p_dl->head;

  while (e)
    {
      printf ("%s\t%s\t%llu\t%lu\n", e->name, e->type == SSH_FILEXFER_TYPE_DIRECTORY ? "[dir]" : "", e->size, e->mtime);
      e = e->next;
    }
}

/* SSH management functions */

void
lt_ssh_init (struct SSH_Info *p_ssh)
{
  memset (p_ssh, 0, sizeof (struct SSH_Info));
}
/*
int
lt_ssh_connect_old (struct SSH_Info *p_ssh)
{
  int rc;
  
  // Open session and set options
  p_ssh->ssh_node->session = ssh_new ();
  
  if (p_ssh->ssh_node->session == NULL)
    return (1);
  
  ssh_options_set (p_ssh->ssh_node->session, SSH_OPTIONS_HOST, p_ssh->host);
  ssh_options_set (p_ssh->ssh_node->session, SSH_OPTIONS_USER, p_ssh->user);
  ssh_options_set (p_ssh->ssh_node->session, SSH_OPTIONS_PORT, &(p_ssh->port));
  
  // Connect to server
  rc = ssh_connect (p_ssh->ssh_node->session);
  
  if (rc != SSH_OK)
    {
      sprintf(p_ssh->error_s, "%s", ssh_get_error (p_ssh->ssh_node->session));
      ssh_free (p_ssh->ssh_node->session);
      p_ssh->ssh_node->session = NULL;
      return (SSH_ERR_CONNECT);
    }
    
  // Verify the server's identity
  \*
  if (verify_knownhost (my_ssh_session) < 0)
    {
      ssh_disconnect (my_ssh_session);
      ssh_free (my_ssh_session);
      exit(-1);
    }
  *\
    
  // Authenticate ourselves
  
  \* get authentication methods *\
  p_ssh->auth_methods = ssh_userauth_list (p_ssh->ssh_node->session, NULL);
  
  if (p_ssh->auth_methods & SSH_AUTH_METHOD_PASSWORD)
    {
      rc = ssh_userauth_password (p_ssh->ssh_node->session, NULL, p_ssh->password);
      
      log_debug ("auth method password: rc=%d\n", rc);
    }
  else if (p_ssh->auth_methods & SSH_AUTH_METHOD_INTERACTIVE)
    {
      rc = ssh_userauth_kbdint (p_ssh->ssh_node->session, NULL, NULL);
      
      log_debug ("auth method interactive: ssh_userauth_kbdint() returns %d\n", rc);
      
      if (rc == SSH_AUTH_INFO)
        {
          ssh_userauth_kbdint_setanswer (p_ssh->ssh_node->session, 0, p_ssh->password);

          rc = ssh_userauth_kbdint (p_ssh->ssh_node->session, NULL, NULL); 
          
          log_debug ("auth method interactive: ssh_userauth_kbdint() returns %d\n", rc);
        }
      else
        rc = SSH_AUTH_ERROR;

      log_debug ("auth method interactive: rc=%d\n", rc);
    }
  else
    {
      sprintf (p_ssh->error_s, "unknown authentication method for server %s\n", p_ssh->host);
      ssh_disconnect (p_ssh->ssh_node->session);
      ssh_free (p_ssh->ssh_node->session);
      p_ssh->ssh_node->session = NULL;
      return (SSH_ERR_AUTH);
    }
  
  if (rc != SSH_AUTH_SUCCESS)
    {
      sprintf (p_ssh->error_s, "%s", ssh_get_error (p_ssh->ssh_node->session));
      ssh_disconnect (p_ssh->ssh_node->session);
      ssh_free (p_ssh->ssh_node->session);
      p_ssh->ssh_node->session = NULL;
      return (SSH_ERR_AUTH);
    }
    
  lt_ssh_getenv (p_ssh, "HOME", p_ssh->home);
    
  return (0);
}
*/
int
lt_ssh_connect (struct SSH_Info *p_ssh, struct SSH_List *p_ssh_list, struct SSH_Auth_Data *p_auth)
{
  struct SSH_Node *p_node;
  int rc = 0;

  ////////////////////////////////
  lockSSH (__func__, TRUE);
  
  sftp_spinner_start ();
  
  if ((p_node = ssh_node_connect (p_ssh_list, p_auth)) == NULL)
    {
      //sprintf (p_ssh->error_s, "%s", ssh_get_error (p_ssh->ssh_node->session));
      sprintf (p_ssh->error_s, "%s", p_auth->error_s);
      rc = p_auth->error_code;
    }
  else
    {
      p_ssh->ssh_node = p_node;
      lt_ssh_getenv (p_ssh, "HOME", p_ssh->home);
    }
    
  sftp_clear_status ();
  sftp_spinner_stop ();

  lockSSH (__func__, FALSE);
  ////////////////////////////////

  return (rc);
}

void
lt_ssh_disconnect (struct SSH_Info *p_ssh)
{
  log_debug ("\n");

  if (p_ssh->ssh_node == NULL)
    return;

  ////////////////////////////////
  lockSSH (__func__, TRUE);

  log_debug ("%s\n", p_ssh->ssh_node->host);
  
  ssh_node_unref (p_ssh->ssh_node);
  
  p_ssh->ssh_node = NULL;

  lockSSH (__func__, FALSE);
  ////////////////////////////////
}

int
lt_ssh_is_connected (struct SSH_Info *p_ssh)
{
  int connected = 1;
  
  log_debug ("\n");
  
  if (p_ssh)
    {
      if (p_ssh->ssh_node)
        {
          if (p_ssh->ssh_node->session == NULL || p_ssh->ssh_node->sftp == NULL)
            connected = 0;
        }
      else
        connected = 0;
    }
  else
    connected = 0;
  
  if (connected && (!ssh_is_connected (p_ssh->ssh_node->session) || !ssh_node_get_validity (p_ssh->ssh_node)))
    connected = 0;

  //log_debug ("connected=%d\n", connected);
  
  return (connected);
}

int
lt_ssh_getenv (struct SSH_Info *p_ssh, char *variable, char *value)
{
  ssh_channel channel;
  char stmt[256];
  int rc;
  char buffer[256];
  unsigned int nbytes;

  ////////////////////////////////
  //lockSSH (__func__, TRUE);

  if ((channel = ssh_node_open_channel (p_ssh->ssh_node)) == NULL)
    return (1);
  
  sprintf (stmt, "echo ${%s}", variable);
  
  log_debug ("%s\n", stmt);
  
  rc = ssh_channel_request_exec (channel, stmt);

  if (rc != SSH_OK)
    {
      log_write ("Error: can't execute statement: %s\n", stmt);
      ssh_channel_close (channel);
      ssh_channel_free (channel);
      ssh_node_set_validity (p_ssh->ssh_node, 0);
      return (rc);
    }

  strcpy (value, "");
  
  log_debug ("Reading...\n");

  nbytes = ssh_channel_read (channel, buffer, sizeof (buffer), 0);

  while (nbytes > 0)
    {
      log_debug ("Read %d bytes\n", nbytes);
      
      buffer [nbytes] = 0;
      strcat (value, buffer);
      nbytes = ssh_channel_read (channel, buffer, sizeof (buffer), 0);
    }

  if (nbytes < 0)
    {
      log_write ("Error: can't execute statement: %s\n", stmt);
      ssh_channel_close (channel);
      ssh_channel_free (channel);
      ssh_node_set_validity (p_ssh->ssh_node, 0);
      return (SSH_ERROR);
    }
    
  log_debug ("Value successfully read\n");

  trim (value);

  log_debug ("%s=%s\n", variable, value);
  
  log_write ("Closing channel on %s@%s\n", p_ssh->ssh_node->user, p_ssh->ssh_node->host);
  
  ssh_channel_send_eof (channel);
  ssh_channel_close (channel);
  ssh_channel_free (channel);
  
  ssh_node_update_time (p_ssh->ssh_node);

  //lockSSH (__func__, FALSE);
  ////////////////////////////////

  return (0);
}
/*
int
lt_sftp_create (struct SSH_Info *p_ssh)
{
  //sftp_session sftp;
  int rc;
  
  p_ssh->ssh_node->sftp = sftp_new (p_ssh->ssh_node->session);
  
  if (p_ssh->ssh_node->sftp == NULL)
    {
      sprintf (p_ssh->error_s, "%s", ssh_get_error (p_ssh->ssh_node->session));
      p_ssh->ssh_node->sftp = NULL;
      return (1);
    }
    
  sftp_set_status ("initializing sftp");
  
  rc = sftp_init (p_ssh->ssh_node->sftp);
  
  if (rc != SSH_OK)
    {
      sprintf (p_ssh->error_s, "%d", sftp_get_error (p_ssh->ssh_node->sftp));
      sftp_free (p_ssh->ssh_node->sftp);
      p_ssh->ssh_node->sftp = NULL;
      return (2);
    }

  return (0);
}
*/
void
sftp_normalize_directory (struct SSH_Info *p_ssh, char *path)
{
  char *tmp;

  trim (path);

  if (strcmp (path, "/") != 0)
    {
      if (path[strlen (path)-1] == '/')
        path[strlen (path)-1] = 0;
    }
    
  tmp = replace_str (path, "//", "/");
  strcpy (path, tmp);

  if (lt_ssh_is_connected (p_ssh))
    {
      tmp = replace_str (path, "~", p_ssh->home);
      strcpy (path, tmp);
    }
}

int
sftp_refresh_directory_list (struct SSH_Info *p_ssh)
{
  int n, nh = 0, retCode=0;
  struct Directory_Entry entry;
  sftp_dir dir;
  sftp_attributes attributes;
  char /*home[256],*/ *tmp;

  ////////////////////////////////
  lockSSH (__func__, TRUE);

  if (!lt_ssh_is_connected (p_ssh))
    return (1);
  
  log_debug ("$HOME=%s\n", p_ssh->home);

  if (p_ssh->directory[0])
    {
      tmp = replace_str (p_ssh->directory, "~", p_ssh->home);
      strcpy (p_ssh->directory, tmp);
    }
  else
    strcpy (p_ssh->directory, p_ssh->home);

  sftp_set_status (_("Opening directory %s..."), p_ssh->directory);
  
  /* set timeout */
/*
  signal (SIGALRM, AlarmHandler); 
  sTimeout = 0; 
  alarm (2); 
*/
  timerStart (2);
  
  dir = sftp_opendir (p_ssh->ssh_node->sftp, p_ssh->directory[0] ? p_ssh->directory : ".");

  if (timedOut ())
    {
      log_write ("Timeout!\n");
      dir = NULL;
    }
/*    
  signal (SIGALRM, SIG_DFL);
  sTimeout = 0; 
  alarm (0); 
*/
  timerStop ();
  
  if (dir)
    {
      dl_release (&p_ssh->dirlist);

      sftp_set_status (_("Reading directory %s..."), p_ssh->directory);
      
      sftp_spinner_start ();
      sftp_begin ();
      
      n = 0;
      while ((attributes = sftp_readdir (p_ssh->ssh_node->sftp, dir)) != NULL)
        {
          if (sftp_stoped_by_user ())
            break;

          memset (&entry, 0, sizeof (struct Directory_Entry));

          strcpy (entry.name, attributes->name);
          entry.type = attributes->type;
          entry.size = attributes->size;
          entry.mtime = attributes->mtime;
          //log_debug ("appending %s\n", entry.name);
          strcpy (entry.owner, attributes->owner ? attributes->owner : "?");
          strcpy (entry.group, attributes->group ? attributes->group : "?");
          entry.permissions = attributes->permissions;

          dl_append (&p_ssh->dirlist, &entry);

          sftp_attributes_free (attributes);
          n ++;
          
          if (is_hidden_file (&entry))
            nh ++;
          
          if ((n >= 200) && (n % 100 == 0))
            sftp_set_status (_("Reading directory %s (%d files)..."), p_ssh->directory, n);
        }

      sftp_closedir (dir);
      
      ssh_node_update_time (p_ssh->ssh_node);
      
      //sftp_set_status (_("%d file%s in %s (%d hidden)"), n, n != 1 ? "s" : "", p_ssh->directory, nh);
      update_statusbar ();

      //dl_dump (&p_ssh->dirlist);

      sftp_spinner_stop ();
      sftp_end ();
  }
  else
    retCode = 2;

  lockSSH (__func__, FALSE);
  ////////////////////////////////
  
  return (retCode);
}

int
lt_ssh_exec (struct SSH_Info *p_ssh, char *command, char *output, int outlen, char *error, int errlen)
{
  ssh_channel channel;
  int rc;
  char buffer[32];
  unsigned int nbytes;

  ////////////////////////////////
  lockSSH (__func__, TRUE);

  if ((channel = ssh_node_open_channel (p_ssh->ssh_node)) == NULL)
    return (1);
  
  //ssh_channel_request_shell (channel);
  
  rc = ssh_channel_request_exec (channel, command);

  if (rc != SSH_OK)
    {
      log_debug ("error\n");
    }


  /* Read output */
  
  strcpy (output, "");
  
  nbytes = ssh_channel_read (channel, buffer, sizeof (buffer), 0);

  while (nbytes > 0)
    {
      buffer [nbytes] = 0;
      strcat (output, buffer);
      nbytes = ssh_channel_read (channel, buffer, sizeof (buffer), 0);
      
      if (strlen (output) + nbytes > outlen)
        break;
    }

  /* Read error */
  
  strcpy (error, "");
  
  nbytes = ssh_channel_read (channel, buffer, sizeof (buffer), 1);

  while (nbytes > 0)
    {
      buffer [nbytes] = 0;
      strcat (error, buffer);
      nbytes = ssh_channel_read (channel, buffer, sizeof (buffer), 1);
      
      //printf ("%d > %d\n", (int) strlen (error) + nbytes, errlen);
      
      if (strlen (error) + nbytes > errlen)
        break;
    }  

/*
  if (nbytes < 0)
    {
      
    }*/

  ssh_channel_send_eof (channel);
  ssh_channel_close (channel);
  ssh_channel_free (channel);
  
  if (rc == SSH_OK)
    {
      ssh_node_update_time (p_ssh->ssh_node);
      rc = 0;
    }

  ////////////////////////////////
  lockSSH (__func__, FALSE);

  return (rc);
}
/*
int
lt_mount (struct SSH_Info *p_ssh, char *source, char *target, char *error)
{
  int result;

  //const char* src  = "none";
  //const char* trgt = "/var/tmp";
  const char* type = "tmpfs";
  const unsigned long mntflags = 0;
  const char* opts = "mode=0700,uid=65534";   \* 65534 is the uid of nobody *\

  result = mount(source, target, type, mntflags, opts);

  if (result != 0 && error != NULL)
    strcpy (error, strerror(errno));

  return result;
}
*/
/*
typedef struct{
  gint std_in;
  gint std_out;
  gint std_err;
} child_fds;

GMainLoop * gml = NULL;

static gboolean 
cb_stdout (GIOChannel* channel, GIOCondition condition, gpointer data)
{
  if (condition & G_IO_ERR ) {
    log_debug ("G_IO_ERR\n");
    g_io_channel_unref(channel);
    g_main_loop_quit (gml);
    return TRUE;
  }
  if (condition & G_IO_HUP ) {
    log_debug ("G_IO_HUP\n");
    g_io_channel_unref(channel);
    g_main_loop_quit (gml);
    return FALSE;
  }

  gchar *string;
  gsize size;
  g_io_channel_read_line (channel, &string, &size, NULL, NULL);

  log_debug("> %s\n", string);
  g_free(string);

  return TRUE;
}

static gboolean 
cb_stderr (GIOChannel* channel, GIOCondition condition, gpointer data)
{
  if (condition & G_IO_ERR ) {
    log_debug ("G_IO_ERR\n");
    g_io_channel_unref(channel);
    g_main_loop_quit (gml);
    return TRUE;
  }
  if (condition & G_IO_HUP ) {
    log_debug ("G_IO_HUP\n");
    g_io_channel_unref(channel);
    g_main_loop_quit (gml);
    return FALSE;
  }

  gchar *string;
  gsize size;
  g_io_channel_read_line (channel, &string, &size, NULL, NULL);

  log_debug("> %s\n", string);
  g_free(string);

  return TRUE;
}

void 
child_handler(GPid pid, int status, gpointer data)
{
  log_debug ("Finished\n");
  g_spawn_close_pid (pid);
}

int
mount_sshfs (struct SSH_Info *p_ssh, char *source, char *target, char *error)
{
  int result = 0;
  char command[4096];

  //signal(SIGINT,&sigterm_handler ); //ctrl+c
      
  //sprintf (command, "xclock");
  //sprintf (command, "echo \"fermat\" | sshfs -o password_stdin fabio@127.0.0.1:/dati/Documenti ~/PROVA & ls ~/PROVA & pwd");
  sprintf (command, "bash -i -c \"sshfs -o password_stdin fabio@127.0.0.1:/dati/Documenti ~/PROVA\"");
  //sprintf (command, "sshfs fabio@127.0.0.1:/dati/Documenti ~/PROVA");
  //sprintf (command, "/dati/Source/helper");
           
  log_debug ("command=%s\n", command);
  GError *err = NULL;

  int argc;
  gchar** argv;
  g_shell_parse_argv (command, &argc, &argv, NULL);

  GIOChannel *std_in_ch;
  GIOChannel *std_out_ch;
  GIOChannel *std_err_ch;
  GPid ls_pid;
  child_fds fds_ls;

  gboolean success = g_spawn_async_with_pipes (NULL,
              argv,
              NULL,
              G_SPAWN_DO_NOT_REAP_CHILD|G_SPAWN_SEARCH_PATH,
              NULL,
              NULL,
              &ls_pid,
              &fds_ls.std_in,
              &fds_ls.std_out,
              &fds_ls.std_err,
              &err);

  g_strfreev(argv);

  if (success) {
    log_debug("Success\n");

    gml = g_main_loop_new(NULL, 0);

    // Watch child termination
    g_child_watch_add (ls_pid, (GChildWatchFunc)child_handler, NULL);

#ifdef G_OS_WIN32
    std_in_ch = g_io_channel_win32_new_fd (fds_ls.std_in);
    std_out_ch = g_io_channel_win32_new_fd (fds_ls.std_out);
    std_err_ch = g_io_channel_win32_new_fd (fds_ls.std_err);
#else
    std_in_ch = g_io_channel_unix_new (fds_ls.std_in);
    std_out_ch = g_io_channel_unix_new (fds_ls.std_out);
    std_err_ch = g_io_channel_unix_new (fds_ls.std_err);
#endif
    //log_debug("Channel %s\n", std_out_ch != NULL ? "created" : "NOT CREATED");

    g_io_add_watch(std_out_ch, G_IO_IN | G_IO_ERR | G_IO_HUP, (GIOFunc)cb_stdout , (gpointer)&fds_ls);
    g_io_add_watch(std_err_ch, G_IO_IN | G_IO_ERR | G_IO_HUP, (GIOFunc)cb_stderr , (gpointer)&fds_ls);
    //g_io_channel_unref(std_out_ch);
    //g_io_channel_unref(std_err_ch);


    gchar buffer[] = "fermat\n\r";
    gsize written;
    GIOStatus status = g_io_channel_write_chars (std_in_ch, buffer, -1, &written, &err);

    if (status != G_IO_STATUS_NORMAL) {
      log_write ("Can't send password\n");
    }

    log_write ("Sent: %d\n", written);

    // Make sync
    g_main_loop_run(gml);




  }
  else {
   log_debug("Error\n");

   result = 1;

    if (error != NULL && err != NULL) {
      strcpy (error, err->message);
      g_error_free (err);
    }
  }

  return result;
}
*/
