
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
 * @file connection.c
 * @brief Connection management
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <sys/utsname.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "profile.h"
#include "gui.h"
#include "protocol.h"
#include "connection.h"
#include "config.h"
#include "connection_list.h"
#include "grouptree.h"
#include "main.h"
#include "utils.h"
#include "xml.h"

#define SEARCH_BY_NAME "Search by name"
#define SEARCH_BY_HOST "Search by host"

extern Globals globals;
extern Prefs prefs;
extern struct Protocol_List g_prot_list;
extern GtkWidget *main_window;
extern struct ConnectionTab *p_current_connection_tab;
extern struct QuickLaunchWindow g_quick_launch_window;
//extern int g_refresh_qlw_tree_view;

struct GroupTree g_groups;
struct Connection_List conn_list;
//struct GroupTree g_groups;
struct GroupNode *g_selected_node;
GdkPixbuf *postit_img;
int g_xml_state;
char g_xml_path[1024];
char g_xml_path_bak[1024];
struct Connection *g_xml_connection;

int g_dialog_connections_running;
int g_dialog_connections_connect;

GtkWidget *connections_dialog;


GtkWidget *port_spin_button;
GtkWidget *check_x11, *check_agentForwarding;
GtkWidget *check_disable_key_checking, *check_keepAliveInterval, *spin_keepAliveInterval;

struct _AuthWidgets {
  GtkWidget *user_entry, *password_entry;
  GtkWidget *radio_auth_key;
  GtkWidget *entry_private_key;
  GtkWidget *radio_auth_prompt;
  GtkWidget *radio_auth_save;
  GtkWidget *button_select_private_key, *button_clear_private_key;
} authWidgets;

GtkTreeStore *model;

enum {
  NAME_COLUMN,
  ADDRESS_COLUMN,
  PROTOCOL_COLUMN,
  PORT_COLUMN,
  PIXMAP_COLUMN,
  N_COLUMNS
};

/* semaphore to manage folders drag and drop */
int rows_signals_enabled = 1;
int g_rebuilding_tree_store = 0;

GtkWidget * create_connections_tree_view ();

void
connection_init_stuff ()
{
  cl_init (&conn_list);
  group_tree_init (&g_groups);

  model = gtk_tree_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);

  postit_img = gdk_pixbuf_new_from_file (g_strconcat (globals.img_dir, "/post-it.png", NULL), NULL);

  if (postit_img == NULL)
    {
      log_debug("can't load image %s\n", g_strconcat (globals.img_dir, "/post-it.png", NULL));
    }

  load_connections ();

  rebuild_tree_store ();
  //expand_connection_tree_view_groups (GTK_TREE_VIEW (tree_view), &g_groups.root);
}

/**
 * detect_serverlist_file_version() - returns version of file containing server hosts
 * @return version of file
 */
int
detect_serverlist_file_version ()
{
  FILE *fp;
  char line[1024];
  int version;

  char v2_s[] = ";mtsl-2";
  
  version = profile_load_int (globals.serverlist, "CFG", "version", 2);
  
  return (version);
}

int
is_xml_file (char *filename)
{
  int is_xml = 0;
  FILE *fp;
  char line[1024];

  fp = fopen (filename, "r");

  if (fp == 0) 
    return 0;

  while (fgets (line, 1024, fp) != 0)
    {
      if (strchr (";\n\r ", line[0]))
        continue;
      
      if (!memcmp (line, "<?xml", 5))
        {
          is_xml = 1;
          break;
        }
    }

  fclose (fp);

  return (is_xml);
}
/*
int
load_connections_from_file_v1 (char *filename, struct Connection_List *p_cl)
{
  FILE *fp;
  char line[1024];
  char name[64], host[128], protocol[64], port_tmp[16];
  int port;
  struct Connection conn;

  log_debug ("\n");

  fp = fopen (filename, "r");

  if (fp == 0) 
    return 1;

  while (fgets (line, 1024, fp) != 0)
    {
      if (strchr (";\n\r ", line[0]))
        continue;

      cut_newline (line);

      list_get_nth (line, 1, ':', conn.name);
      list_get_nth (line, 2, ':', conn.host);
      list_get_nth (line, 3, ':', conn.protocol);
      list_get_nth (line, 4, ':', port_tmp);
      list_get_nth (line, 5, ':', conn.emulation);

      if (is_numeric (port_tmp))
        conn.port = atoi (port_tmp);
      else
        conn.port = -1;

      //cl_append (p_cl, &conn);
      cl_insert_sorted (p_cl, &conn);
    }

  fclose (fp);

  return 0;
}
*/

/**
 * load_connections_from_file_version() - loads a liear-type file. used in sessions loading
 */
/*
int
load_connections_from_file_version (char *filename, struct Connection_List *p_cl, int version)
{
  FILE *fp;
  char line[1024], *pc;
  struct Connection conn, *c;
  char *p_enc;
  char *p_enc_b64;
  int len;

  log_debug ("%s\n", filename);
  
  fp = fopen (filename, "r");

  if (fp == 0) 
    return 1;

  // find list of connections, only names
  
  while (fgets (line, 1024, fp) != 0)
    {
      if (line[0] != '[')
        continue;
      
      pc = (char *) strchr (line, ']');
      
      if (!pc)
        continue;

      memset (&conn, 0x00, sizeof (struct Connection));
      
      *pc = 0;

      pc = &line[1];
      
      if (!strcmp (pc, "CFG"))
        continue;

      strcpy (conn.name, pc);
      
      //log_debug ("%s\n", conn.name);

      cl_insert_sorted (p_cl, &conn);
    }

  fclose (fp);
  
  // load parameters for each connection
  
  c = p_cl->head;

  while (c)
    {
      profile_load_string (filename, c->name, "host", c->host, "localhost");
      profile_load_string (filename, c->name, "protocol", c->protocol, "telnet");
      c->port = profile_load_int (filename, c->name, "port", 23);
      //profile_load_string (filename, c->name, "emulation", c->emulation, "xterm");
      profile_load_string (filename, c->name, "last_user", c->last_user, "");
      
      c->flags = profile_load_int (filename, c->name, "flags", 0);

      //c->auth = profile_load_int (filename, c->name, "auth", 0);
      c->auth_mode = profile_load_int (filename, c->name, "auth_mode", 0);
      profile_load_string (filename, c->name, "auth_user", c->auth_user, "");
      profile_load_string (filename, c->name, "auth_password", c->auth_password_encrypted, "");
      
      // In 2 then "user" and "password" are used as "auth_user" and "auth_password"
      
      if (version == 2)
        {
          profile_load_string (filename, c->name, "user", c->auth_user, "");
          profile_load_string (filename, c->name, "password", c->auth_password_encrypted, "");
        }

      profile_load_string (filename, c->name, "user", c->user, "");
      profile_load_string (filename, c->name, "password", c->password_encrypted, "");

      //log_debug ("host=%s user=%s last_user=%s\n", c->host, c->user, c->last_user);

      if (strlen (c->password_encrypted) > 2)
        {
          // decrypt password
          strcpy (c->password, des_decrypt_b64 (c->password_encrypted));
        }
      else
        {
          strcpy (c->password, "");
          strcpy (c->password_encrypted, "");
        }

      if (strlen (c->auth_password_encrypted) > 2)
        {
          // decrypt password
          strcpy (c->auth_password, (char *) des_decrypt_b64 (c->auth_password_encrypted));
        }
      else
        {
          strcpy (c->auth_password, "");
          strcpy (c->auth_password_encrypted, "");
        }

      profile_load_string (filename, c->name, "user_options", c->user_options, "");
      profile_load_string (filename, c->name, "directory", c->directory, "");

      c->x11Forwarding = profile_load_int (filename, c->name, "x11Forwarding", 0);
      log_debug ("reading identityFile\n");
      profile_load_string (filename, c->name, "identityFile", c->identityFile, "");

      // change name to original

      pc = (char *) strrchr (c->name, '~');

      if (pc)
        *pc = 0;
      
      c = c->next;
    }  

  log_debug ("end\n");

  return 0;
}


int
load_connections_from_file (char *filename, struct Connection_List *p_cl)
{
  int ret, version;

  version = detect_serverlist_file_version ();

  switch (version) 
    {
    case 1:
      //ret = load_connections_from_file_v1 (filename, p_cl);
      break;
    default:
      //ret = load_connections_from_file_v2 (filename, p_cl);
      ret = load_connections_from_file_version (filename, p_cl, version);
      break;
    }

  return (ret);
}
*/

int
conn_update_last_user (char *cname, char *last_user)
{
  struct Connection *p_conn;

  log_debug ("updating last_user = %s for conn. %s\n", last_user, cname);

  /*
  if (last_user[0] != 0)
    return (profile_modify_string (PROFILE_SAVE, globals.serverlist, name, "last_user", last_user));
  else
    return 1;
  */

  p_conn = cl_get_by_name (&conn_list, cname);

  if (p_conn)
    strcpy (p_conn->last_user, last_user);

  return 0;
}

/**
 * save_connections_to_file() - saves a linear-type file. used in sessions saving
 */
/*
int
save_connections_to_file (char *filename, struct Connection_List *p_cl, int flags)
{
  struct Connection *c;
  FILE *fp;

  log_debug ("\n");

  fp = fopen (filename, "w");

  if (fp == 0) 
    return 1;
  
  // fprintf (fp, ";mtsl-2\n\n"); // Version 2 header 

  fprintf (fp, "[CFG]\n");
  fprintf (fp, "version = %d\n\n", CFG_VERSION);
  
  c = p_cl->head;

  while (c)
    {
      fprintf (fp, "[%s]\n", c->name);
      fprintf (fp, "host = %s\n", c->host);
      fprintf (fp, "protocol = %s\n", c->protocol);
      fprintf (fp, "port = %d\n", c->port);
      //fprintf (fp, "emulation = %s\n", c->emulation);
      
      if (c->last_user[0])
        fprintf (fp, "last_user = %s\n", c->last_user);
      
      if (flags & CONN_SAVE_USERPASS)
        {
          fprintf (fp, "user = %s\n", c->user);
          fprintf (fp, "password = %s\n", c->password_encrypted);
        }
      
      //fprintf (fp, "auth = %d\n", c->auth);
      fprintf (fp, "auth_mode = %d\n", c->auth_mode);
      fprintf (fp, "auth_user = %s\n", c->auth_user);
      fprintf (fp, "identityFile = %s\n", c->identityFile);

      //fprintf (fp, "password = %s\n", c->password); // clear password
      //fprintf (fp, "password = %s\n", c->password_encrypted, strlen (c->password_encrypted));
      //fprintf (fp, "password = %s\n", g_base64_encode (c->password_encrypted, strlen (c->password_encrypted)));
      fprintf (fp, "auth_password = %s\n", c->auth_password_encrypted);

      fprintf (fp, "user_options = %s\n", c->user_options);
      fprintf (fp, "directory = %s\n", c->directory);
      fprintf (fp, "flags = %d\n", c->flags);
      fprintf (fp, "x11Forwarding = %d\n", c->x11Forwarding);
      
      fprintf (fp, "\n"); 
      
      c = c->next;
    }

  fclose (fp);
  
  return (0);
}
*/

void
write_connection_node (FILE *fp, struct Connection *p_conn, int indent)
{
  int i;

              fprintf (fp, "%*s<connection name='%s' host='%s' protocol='%s' port='%d' flags='%d'>\n"
                           //"%*s  <emulation>%s</emulation>\n"
                           //"%*s  <authentication enabled='%d'>\n"
                           "%*s  <authentication>\n"
                           "%*s    <mode>%d</mode>\n"
                           "%*s    <auth_user>%s</auth_user>\n"
                           "%*s    <auth_password>%s</auth_password>\n"
                           "%*s    <identityFile>%s</identityFile>\n"
                           "%*s  </authentication>\n"
                           "%*s  <last_user>%s</last_user>\n"
                           "%*s  <user>%s</user>\n"
                           "%*s  <password>%s</password>\n"
                           "%*s  <directory>%s</directory>\n"
                           "%*s  <user_options>%s</user_options>\n"
                           "%*s  <note>%s</note>\n"
                           "%*s  <sftp_dir>%s</sftp_dir>\n"
                           "%*s  <upload_dir>%s</upload_dir>\n"
                           "%*s  <download_dir>%s</download_dir>\n"
                           //"%*s  <x11Forwarding>%d</x11Forwarding>\n",
                           "%*s  <options>\n"
                           "%*s    <property name='x11Forwarding'>%d</property>\n"
                           "%*s    <property name='agentForwarding'>%d</property>\n"
                           "%*s    <property name='disableStrictKeyChecking'>%d</property>\n"
                           "%*s    <property name='keepAliveInterval' enabled='%d'>%d</property>\n"
                           "%*s  </options>\n",
                        indent, " ", p_conn->name, NVL(p_conn->host, ""), NVL(p_conn->protocol, ""), p_conn->port, p_conn->flags,
                        //indent, " ", p_conn->emulation,
                        indent, " ", 
                        indent, " ", p_conn->auth_mode,
                        indent, " ", NVL(p_conn->auth_user, ""),
                        indent, " ", NVL(p_conn->auth_password_encrypted, ""),
                        indent, " ", p_conn->identityFile[0] ? g_markup_escape_text (p_conn->identityFile, strlen (p_conn->identityFile)) : "",
                        indent, " ",
                        indent, " ", NVL(p_conn->last_user, ""),
                        indent, " ", NVL(p_conn->user, ""),
                        indent, " ", NVL(p_conn->password_encrypted, ""),
                        indent, " ", p_conn->directory[0] ? g_markup_escape_text (p_conn->directory, strlen (p_conn->directory)) : "",
                        indent, " ", p_conn->user_options,
                        indent, " ", p_conn->note[0] ? g_markup_escape_text (p_conn->note, strlen (p_conn->note)) : "",
                        indent, " ", p_conn->sftp_dir[0] ? g_markup_escape_text (p_conn->sftp_dir, strlen (p_conn->sftp_dir)) : "",
                        indent, " ", p_conn->upload_dir[0] ? g_markup_escape_text (p_conn->upload_dir, strlen (p_conn->upload_dir)) : "",
                        indent, " ", p_conn->download_dir[0] ? g_markup_escape_text (p_conn->download_dir, strlen (p_conn->download_dir)) : "",
                        indent, " ",
                        indent, " ", p_conn->sshOptions.x11Forwarding,
                        indent, " ", p_conn->sshOptions.agentForwarding,
                        indent, " ", p_conn->sshOptions.disableStrictKeyChecking,
                        indent, " ", p_conn->sshOptions.flagKeepAlive, p_conn->sshOptions.keepAliveInterval,
                        indent, " "
                      );
                        
              //if (p_conn->history.head)
              if (p_conn->directories)
                {
                  //log_debug ("  Writing history...\n");
                  
                  //struct Bookmark *b = p_conn->history.head;
                  
                  fprintf (fp, "%*s  <history>\n", indent, " ");
                  
                  /*while (b)
                    {
                      if (strlen (b->item) > 0)
                        fprintf (fp, "%*s    <item>%s</item>\n", indent, " ", g_markup_escape_text (b->item, strlen (b->item)));
                      
                      b = b->next;
                    }*/
                  for (i=0; i<p_conn->directories->len; i++)
                    {
                      char *dir;
                      dir = (gchar *) g_ptr_array_index (p_conn->directories, i);

                      fprintf (fp, "%*s    <item>%s</item>\n", indent, " ", g_markup_escape_text (dir, strlen (dir)));
                    }
                    
                  fprintf (fp, "%*s  </history>\n", indent, " ");
                  
                  //log_debug ("  ...done\n");
                }
              /*else
                {
                  log_debug ("no bookmark for %s\n", p_conn->name);
                }*/
                        
              fprintf (fp, "%*s</connection>\n", indent, " ");
}

void
append_node_to_file_xml (FILE *fp, struct GroupNode *p_node, int indent)
{
  int i;
  struct Connection *p_conn;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->child[i])
        {
          if (p_node->child[i]->type == GN_TYPE_CONNECTION)
            {
              p_conn = cl_get_by_name (&conn_list, p_node->child[i]->name);
              //log_debug ("Writing %s\n", p_conn->name);
              write_connection_node (fp, p_conn, indent);
            }
          else
            {
              fprintf (fp, "%*s<folder name='%s' expanded='%d'>\n", indent, " ", p_node->child[i]->name, p_node->child[i]->expanded);
              append_node_to_file_xml (fp, p_node->child[i], indent + 2);
              fprintf (fp, "%*s</folder>\n", indent, " ");
            }
        }
    }
}

int
save_connections_to_file_xml (char *filename)
{
  int i;
  FILE *fp;

  log_debug ("\n");

  fp = fopen (filename, "w");

  if (fp == 0) 
    return 1;

  fprintf (fp, 
           "<?xml version = '1.0'?>\n"
           "<!DOCTYPE connectionset>\n"
           "<connectionset version=\"%d\">\n", 
           CFG_XML_VERSION);
  
  append_node_to_file_xml (fp, &g_groups.root, 2);
  
  fprintf (fp, "</connectionset>\n"); 

  fclose (fp);

  return (0);
}
/*
int
save_connections_to_file_xml_from_list (struct Connection_List *pList, char *filename)
{
  int i;
  FILE *fp;
  struct Connection *c;

  log_debug ("%s\n", filename);

  fp = fopen (filename, "w");

  if (fp == 0) 
    return 1;

  fprintf (fp, 
           "<?xml version = '1.0'?>\n"
           "<!DOCTYPE connectionset>\n"
           "<connectionset version=\"%d\">\n", 
           CFG_XML_VERSION);
  
  //append_node_to_file_xml (fp, &g_groups.root, 2);
  c = pList->head;

  while (c)
    {
      log_debug ("Writing %s\n", c->name);
      write_connection_node (fp, c, 2);
      c = c->next;
    }
  
  fprintf (fp, "</connectionset>\n"); 

  fclose (fp);

  

  return (0);
}
*/

int
save_connections_to_file_xml_from_glist (GList *pList, char *filename)
{
  int i;
  FILE *fp;
  struct Connection *c;

  log_debug ("%s\n", filename);

  fp = fopen (filename, "w");

  if (fp == 0) 
    return 1;

  fprintf (fp, 
           "<?xml version = '1.0'?>\n"
           "<!DOCTYPE connectionset>\n"
           "<connectionset version=\"%d\">\n", 
           CFG_XML_VERSION);
  
  struct Connection *p_conn;
  GList *item;

  item = g_list_first (pList);

  while (item)
    {
      p_conn = (struct Connection *) item->data;

      //log_debug ("Writing %s\n", p_conn->name);
      write_connection_node (fp, p_conn, 2);

      item = g_list_next (item);
    }

  fprintf (fp, "</connectionset>\n"); 

  fclose (fp);

  return (0);
}

struct Connection *
get_connection (struct Connection_List *p_cl, char *name)
{
  struct Connection *c;

  c = p_cl->head;

  while (c)
    {
      if (!strcmp (c->name, name))
        return (c);
 
      c = c->next;
    }

  return 0;
}

int g_connectionset_version;

void
read_connection_node (XMLNode *node, struct Connection *pConn)
{
  char name[256], *pc;
  char tmp_s[32];
  char propertyName[128], propertyValue[1024];
  XMLNode *child, *node_auth, *node_hist;

  memset (pConn, 0, sizeof (struct Connection));

  strcpy (pConn->name, xml_node_get_attribute (node, "name"));
  //log_debug ("%s\n", pConn->name);
  
  strcpy (pConn->host, xml_node_get_attribute (node, "host"));
  strcpy (pConn->protocol, xml_node_get_attribute (node, "protocol"));

  strcpy (tmp_s, xml_node_get_attribute (node, "port"));
  if (tmp_s[0])
    pConn->port = atoi (tmp_s);

  strcpy (tmp_s, NVL(xml_node_get_attribute (node, "flags"), ""));
  if (tmp_s[0])
    pConn->flags = atoi (tmp_s);

  if (child = xml_node_get_child (node, "last_user"))
    strcpy (pConn->last_user, NVL(xml_node_get_value (child), ""));

  if (child = xml_node_get_child (node, "user"))
    strcpy (pConn->user, NVL(xml_node_get_value (child), ""));

  //log_debug ("User: %s\n", pConn->user);

  if (child = xml_node_get_child (node, "password"))
    {
      strcpy (pConn->password_encrypted, NVL(xml_node_get_value (child), ""));

      if (strlen (pConn->password_encrypted) > 5)
        {
          //pc = des_decrypt_b64 (pConn->auth_password_encrypted);
          memcpy (pConn->password, des_decrypt_b64 (pConn->password_encrypted), 32);
        }
      else
        {
          strcpy (pConn->password_encrypted, "");
        }
      //log_debug ("Password: %s\n", pConn->password);
    }

  /*if (child = xml_node_get_child (node, "emulation"))
    strcpy (pConn->emulation, NVL(xml_node_get_value (child), "xterm"));*/

  if (child = xml_node_get_child (node, "directory"))
    strcpy (pConn->directory, NVL(xml_node_get_value (child), ""));

  if (child = xml_node_get_child (node, "note"))
    strcpy (pConn->note, NVL(xml_node_get_value (child), ""));

  if (node_auth = xml_node_get_child (node, "authentication"))
    {
      if (g_connectionset_version == 4)
        {
          strcpy (tmp_s, xml_node_get_attribute (node_auth, "enabled"));
          if (tmp_s[0])
            //pConn->auth = atoi (tmp_s);
            pConn->auth_mode = atoi (tmp_s);
        }

      if (child = xml_node_get_child (node_auth, "mode"))
        {
          strcpy (tmp_s, NVL(xml_node_get_value (child), "0"));
          if (tmp_s[0])
            pConn->auth_mode = atoi (tmp_s);
        }

      if (child = xml_node_get_child (node_auth, "auth_user"))
        strcpy (pConn->auth_user, NVL(xml_node_get_value (child), ""));

      if (child = xml_node_get_child (node_auth, "auth_password"))
        {
          strcpy (pConn->auth_password_encrypted, NVL(xml_node_get_value (child), ""));

          if (strlen (pConn->auth_password_encrypted) > 5)
            {
              //pc = des_decrypt_b64 (pConn->auth_password_encrypted);
              memcpy (pConn->auth_password, des_decrypt_b64 (pConn->auth_password_encrypted), 32);
            }
          else
            {
              strcpy (pConn->auth_password, "");
              strcpy (pConn->auth_password_encrypted, "");
            }
        }

      if (child = xml_node_get_child (node_auth, "identityFile"))
        strcpy (pConn->identityFile, NVL(xml_node_get_value (child), ""));

    }

  if (node_hist = xml_node_get_child (node, "history"))
    {
      //log_debug ("Reading history...\n");
      
      child = node_hist->children;
      
      while (child)
        {
          char *d = (char *) xml_node_get_value (child);
          if (d) {
            //log_debug ("Adding %s\n", d);
            //add_bookmark (&(pConn->history), d); // deprecated
            add_directory (pConn, d);
          }
          child = child->next;
        }
    }

  if (child = xml_node_get_child (node, "upload_dir"))
    strcpy (pConn->upload_dir, NVL(xml_node_get_value (child), ""));

  if (child = xml_node_get_child (node, "download_dir"))
    strcpy (pConn->download_dir, NVL(xml_node_get_value (child), ""));

  // Deprecated: use options node
  if (child = xml_node_get_child (node, "x11Forwarding"))
    {
      //log_debug ("x11Forwarding\n");
      strcpy (tmp_s, NVL(xml_node_get_value (child), "0"));
      if (tmp_s[0])
        pConn->sshOptions.x11Forwarding = atoi (tmp_s);
    }

  if (child = xml_node_get_child (node, "options"))
    {
      XMLNode *propNode = child->children;

      while (propNode) {
        strcpy (propertyName, xml_node_get_attribute (propNode, "name"));
        strcpy (propertyValue, NVL(xml_node_get_value (propNode), "0"));

        //log_debug ("%s = %s\n", propertyName, propertyValue);

        if (!strcmp(propertyName, "x11Forwarding"))
          pConn->sshOptions.x11Forwarding = atoi (propertyValue);
        else if (!strcmp(propertyName, "agentForwarding"))
          pConn->sshOptions.agentForwarding = atoi (propertyValue);
        else if (!strcmp(propertyName, "disableStrictKeyChecking"))
          pConn->sshOptions.disableStrictKeyChecking = atoi (propertyValue);
        else if (!strcmp(propertyName, "keepAliveInterval")) {
          pConn->sshOptions.flagKeepAlive = atoi (NVL(xml_node_get_attribute (propNode, "enabled"), "0"));
          pConn->sshOptions.keepAliveInterval = atoi (propertyValue);
        }
  
        propNode = propNode->next;
      }
    }

    //log_debug ("Node read\n");
}

void
read_xml_connection_item (XMLNode *node)
{
  struct GroupNode *p_node;
  struct Connection conn;
  char name[256], *pc;
  char tmp_s[32];
  XMLNode *child, *node_auth, *node_hist;

  while (node)
    {
      if (!strcmp (node->name, "folder"))
        {
          if (g_xml_path[0] != 0)
            strcat (g_xml_path, "/");
              
          strcpy (name, xml_node_get_attribute (node, "name"));   
          strcat (g_xml_path, name);
          
          p_node = group_tree_create_path (&g_groups, g_xml_path);

          strcpy (tmp_s, xml_node_get_attribute (node, "expanded"));
          if (tmp_s[0])
            p_node->expanded = atoi (tmp_s);

          read_xml_connection_item (node->children);

          pc = (char *) strrchr (g_xml_path, '/');
          
          if (pc)
            *pc = 0;
          else
            strcpy (g_xml_path, "");

          node = node->next;
        }
      else if (!strcmp (node->name, "connection"))
        {
          read_connection_node (node, &conn);

          /* add to the linear and the detailed list */

          g_xml_connection = cl_insert_sorted (&conn_list, &conn);
          
          /* add to the tree */
          
          if (g_xml_path[0])
            p_node = group_tree_create_path (&g_groups, g_xml_path);
          else
            p_node = group_tree_get_root (&g_groups);

          group_node_add_child (p_node, GN_TYPE_CONNECTION, conn.name);
          node = node->next;
        }
    }
}

int
get_xml_doc (char *filename, XML *xmldoc)
{
  char line[2048], tmp_s[32];
  char *xml;
  FILE *fp;

  fp = fopen (filename, "r");
  
  if (fp == NULL)
    return (1);
  
  xml = (char *) malloc (2048);
  strcpy (xml, "");
  strcpy (g_xml_path, "");
  
  while (fgets (line, 1024, fp) != 0)
    {
      if (strlen (xml) + strlen (line) > sizeof (xml))
        xml = (char *) realloc (xml, strlen (xml) + strlen (line) + 1);
      
      strcat (xml, line);
    }
    
  fclose (fp);
  
  //log_debug ("\n%s\n", xml);

  
  /* Parse xml and create the connections tree */

  //XML xmldoc;

  xml_parse (xml, xmldoc);

  //log_debug ("xmldoc.error.code=%d\n", xmldoc.error.code);

  if (xmldoc->error.code)
    {
      log_write ("%s\n", xmldoc->error.message);
      return 1;
    } 

  return 0;
}

/**
 * load_connections_from_file_xml() - loads user connection tree
 */
int
load_connections_from_file_xml (char *filename)
{
  int rc = 0;
  struct Connection *p_conn;
  //char *xml;
  char line[2048], tmp_s[32];
  
  log_debug ("%s\n", filename);

  cl_release (&conn_list);
  cl_init (&conn_list);

  group_tree_release (&g_groups);
  group_tree_init (&g_groups);

  XML xmldoc;
  rc = get_xml_doc (filename, &xmldoc);
  
  if (rc != 0)
    return (rc);

  if (xmldoc.cur_root)
    {
      if (xmldoc.cur_root && strcmp (xmldoc.cur_root->name, "connectionset"))
        {
          log_write ("[%s] can't find root node: connectionset\n", __func__);
          return 2;
        }
        
      g_connectionset_version = 1;
      
      strcpy (tmp_s, xml_node_get_attribute (xmldoc.cur_root, "version"));
      if (tmp_s[0])
        g_connectionset_version = atoi (tmp_s);  
        
      log_debug ("connectionset version: %d\n", g_connectionset_version);      
      
      read_xml_connection_item (xmldoc.cur_root->children);
      
      xml_free (&xmldoc);
    }


  //free (xml);

#ifdef DEBUG
  //group_tree_dump (&g_groups);
#endif
  
  return (rc);
}


/**
 * load_connection_list_from_file_xml()
 * loads connections into a list
 */
GList *
load_connection_list_from_file_xml (char *filename)
{
  int rc = 0;
  struct Connection *pConn;
  //char *xml;
  XMLNode *node;
  GList *list = NULL;
  
  log_debug ("%s\n", filename);

  XML xmldoc;
  rc = get_xml_doc (filename, &xmldoc);
  
  if (rc != 0)
    return (NULL);

  if (xmldoc.cur_root)
    {
      if (xmldoc.cur_root && strcmp (xmldoc.cur_root->name, "connectionset"))
        {
          log_write ("[%s] can't find root node: connectionset\n", __func__);
          return NULL;
        }
 
      node = xmldoc.cur_root->children;

      while (node)
        {
          //log_debug ("%s\n", node->name);

          if (!strcmp (node->name, "connection"))
            {
              pConn = (struct Connection *) malloc (sizeof (struct Connection));
              read_connection_node (node, pConn);
              list = g_list_append (list, pConn);

              //struct Connection *c1 = (struct Connection *) g_list_last (list);
              //log_debug ("%s@%s\n", pConn->user, pConn->host);
            }

          node = node->next;
        }
      
      xml_free (&xmldoc);
    }


  //free (xml);

#ifdef DEBUG
  //group_tree_dump (&g_groups);
#endif
  
  return (list);
}
/**
 * load_connections() - loads user connection tree
 */
int
load_connections ()
{
  int rc;
  struct Connection *p_conn;

  cl_init (&conn_list);
  group_tree_init (&g_groups);

  /* try connections.xml file first */
  
  rc = load_connections_from_file_xml (globals.connections_xml);
/*
  // if connections.xml has not been found try loading old file format
  
  if (rc != 0)
    {
      rc = load_connections_from_file (globals.serverlist, &conn_list);

      // add connections to tree, will be all children of root node
      
      p_conn = conn_list.head;
      
      while (p_conn)
        {
          group_node_add_child (&g_groups.root, GN_TYPE_CONNECTION, p_conn->name);

          p_conn = p_conn->next;
        }
    }
*/
#ifdef DEBUG
  //group_tree_dump (&g_groups);
#endif

  

  return (rc);
}

int
count_current_connections ()
{
  return (cl_count (&conn_list));
}

struct Connection *
get_connection_by_index (int index)
{
  return (cl_get_by_index (&conn_list, index));
}

struct Connection *
get_connection_by_name (char *name)
{
  return (cl_get_by_name (&conn_list, name));
}

struct Connection *
get_connection_by_host (char *host)
{
  return (cl_host_search (&conn_list, host, NULL));
}

/* ---[ Graphic User Interface section ]--- */

void
set_private_key_controls (gboolean status)
{
#if (GTK_MAJOR_VERSION == 2)
  gtk_widget_set_sensitive (authWidgets.entry_private_key, status);
  gtk_widget_set_sensitive (authWidgets.button_select_private_key, status);
  gtk_widget_set_sensitive (authWidgets.password_entry, status);
#else
  gtk_widget_set_state_flags (authWidgets.entry_private_key, 
                              status ? GTK_STATE_FLAG_NORMAL : GTK_STATE_FLAG_INSENSITIVE, 
                              TRUE);
  gtk_widget_set_state_flags (authWidgets.button_select_private_key, 
                              status ? GTK_STATE_FLAG_NORMAL : GTK_STATE_FLAG_INSENSITIVE, 
                              TRUE);
  gtk_widget_set_state_flags (authWidgets.button_clear_private_key, 
                              status ? GTK_STATE_FLAG_NORMAL : GTK_STATE_FLAG_INSENSITIVE, 
                              TRUE);
#endif
}

static void
change_protocol_cb (GtkWidget *entry, gpointer user_data)
{
  struct Protocol *p_prot;
  //SConnectionTab *connTab = (SConnectionTab *)user_data;

  log_debug ("[start]\n");

#if (GTK_MAJOR_VERSION == 2)
  p_prot = get_protocol (&g_prot_list, (char *) gtk_combo_box_get_active_text (GTK_COMBO_BOX (entry)));
#else
  p_prot = get_protocol (&g_prot_list, (char *) gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (entry)));
#endif

  if (p_prot)
    {
      gtk_spin_button_set_value (GTK_SPIN_BUTTON (port_spin_button), p_prot->port);
/*
#if (GTK_MAJOR_VERSION == 2)
      gtk_widget_set_sensitive (check_x11, p_prot->type == PROT_TYPE_SSH);
      gtk_widget_set_sensitive (check_disable_key_checking, p_prot->type == PROT_TYPE_SSH);
      gtk_widget_set_sensitive (authWidgets.radio_auth_key, p_prot->type == PROT_TYPE_SSH);
#else

      GtkStateFlags flags = p_prot->type == PROT_TYPE_SSH ? GTK_STATE_FLAG_NORMAL : GTK_STATE_FLAG_INSENSITIVE;

      gtk_widget_set_state_flags (check_x11, flags, FALSE);
      gtk_widget_set_state_flags (check_disable_key_checking, flags, FALSE);
      gtk_widget_set_state_flags (authWidgets.radio_auth_key, flags, FALSE);
*/
      gboolean sensitive = p_prot->type == PROT_TYPE_SSH;

      gtk_widget_set_sensitive (check_x11, sensitive);
      gtk_widget_set_sensitive (check_agentForwarding, sensitive);
      gtk_widget_set_sensitive (check_disable_key_checking, sensitive);
      gtk_widget_set_sensitive (check_keepAliveInterval, sensitive);
      gtk_widget_set_sensitive (spin_keepAliveInterval, sensitive);
      gtk_widget_set_sensitive (authWidgets.radio_auth_key, sensitive);
//#endif

      //set_private_key_controls (p_prot->type == PROT_TYPE_SSH);

      // If protocol has been changed from ssh to other, and the key authentication is selected
      // then change to prompted authentication
      if (p_prot->type != PROT_TYPE_SSH 
          && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key))) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_prompt), TRUE);
      }
    }

  log_debug ("[end]\n");
}

static void
change_search_by_cb (GtkWidget *entry, gpointer user_data)
{
  GtkWidget *tree_view;

  tree_view = GTK_WIDGET (user_data); 

#if (GTK_MAJOR_VERSION == 2)
  if (!strcmp (gtk_combo_box_get_active_text (GTK_COMBO_BOX (entry)), SEARCH_BY_NAME))
#else
  if (!strcmp (gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (entry)), SEARCH_BY_NAME))
#endif
    {
      prefs.search_by = 0;
    }
  else
    {
      prefs.search_by = 1;
    }

  gtk_tree_view_set_search_column (GTK_TREE_VIEW (tree_view), prefs.search_by);
  gtk_widget_grab_focus (tree_view);
}

void
copy_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  GtkClipboard *clipboard;
  GtkTreeSelection *select;
  struct Connection conn;
  int found;

  select = (GtkTreeSelection *) user_data;
  found = get_selected_connection (select, &conn);

  if (found)
    {
#ifdef DEBUG
      printf ("copy_button_clicked_cb() : copy %s (%s)\n", conn.host, conn.name);
#endif
      /*
      clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);                                                            
      gtk_clipboard_clear(clipboard);                                                                                  
      gtk_clipboard_set_text(clipboard, conn.host, strlen (conn.host));                                                                        
      */
      clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);                                                          
      gtk_clipboard_clear(clipboard);                                                                                
      gtk_clipboard_set_text(clipboard, conn.host, strlen (conn.host));
    }                                                                     
}

GtkWidget *
create_entry_control (char *label, GtkWidget *entry)
{
  GtkWidget *hbox;

#if (GTK_MAJOR_VERSION == 2)
  hbox = gtk_hbox_new (FALSE, 10);
#else
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
#endif

  gtk_box_pack_start (GTK_BOX (hbox), gtk_widget_new (GTK_TYPE_LABEL, "label", label, "xalign", 0.0, NULL), FALSE, TRUE, 0);

  //entry = gtk_entry_new_with_max_length (255);
  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);

  return (hbox);
}

void
radio_auth_save_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
  log_debug("\n");

#if (GTK_MAJOR_VERSION == 2)
  gtk_widget_set_sensitive (authWidgets.user_entry, gtk_toggle_button_get_active (togglebutton));
  gtk_widget_set_sensitive (authWidgets.password_entry, gtk_toggle_button_get_active (togglebutton));
#else
  gtk_widget_set_state_flags (authWidgets.user_entry, 
                              gtk_toggle_button_get_active (togglebutton) ? GTK_STATE_FLAG_NORMAL : GTK_STATE_FLAG_INSENSITIVE, 
                              TRUE);

  gtk_widget_set_state_flags (authWidgets.password_entry, 
                              gtk_toggle_button_get_active (togglebutton) ? GTK_STATE_FLAG_NORMAL : GTK_STATE_FLAG_INSENSITIVE, 
                              TRUE);
#endif
}

void
radio_auth_key_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
  log_debug("\n");

  set_private_key_controls (gtk_toggle_button_get_active (togglebutton));
}

/**
 * get_parent_node_for_insert() - return the node to be used as parent for a new node creation
 * @return a pointer to the parent node
 */
struct GroupNode *
get_parent_node_for_insert ()
{
  struct GroupNode *p_parent = NULL;

  if (g_selected_node == NULL)
    p_parent = group_tree_get_root (&g_groups); /* top level */
  else
    {
      log_debug ("g_selected_node = %s\n", g_selected_node->name);

      if (g_selected_node->type == GN_TYPE_FOLDER)
        {
          p_parent = g_selected_node; /* will be child of current folder */
          g_selected_node->expanded = 1;
        }
      else
        p_parent = g_selected_node->parent; /* will be at the same level of current connection */
    }

  log_debug ("p_parent = %s\n", p_parent->name);

  return (p_parent);
}               

void
clear_note_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  GtkTextBuffer *buffer = (GtkTextBuffer *) user_data;

  gtk_text_buffer_set_text (buffer, "", -1);
}

void
select_private_key_cb (GtkButton *button, gpointer user_data)
{
  GtkWidget *dialog;
  gint result;

  dialog = gtk_file_chooser_dialog_new ("Select private key file", GTK_WINDOW (main_window), 
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                        NULL);
                                         
  /*if (p_conn->upload_dir[0])
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), p_conn->upload_dir);*/
  
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), FALSE);

  result = gtk_dialog_run (GTK_DIALOG (dialog));
  
  if (result == GTK_RESPONSE_ACCEPT)
    {
      gtk_entry_set_text (authWidgets.entry_private_key, gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog)));
    }
    
  gtk_widget_destroy (dialog);
}

void
clear_private_key_cb (GtkButton *button, gpointer user_data)
{
  gtk_entry_set_text (authWidgets.entry_private_key, "");
}

void
note_buffer_changed_cb (GtkTextBuffer *buffer, gpointer user_data)
{
  GtkTextIter start;
  GtkTextIter end;
  gchar *text;
   
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  
   if (strlen (text) > MAX_NOTE_LEN)
     {
       gtk_text_buffer_get_iter_at_offset (buffer, &start, MAX_NOTE_LEN - 1); /* leave room for NULL */
       gtk_text_buffer_get_iter_at_offset (buffer, &end, MAX_NOTE_LEN );
       gtk_text_buffer_delete (buffer, &start, &end);
     }
}

char *
get_validation_error_string (int error_code)
{
  switch (error_code) {
    case 0:
      return (_("Validation OK"));
      break;
    case ERR_VALIDATE_MISSING_VALUES:
      return (_("Missing values"));
      break;
    case ERR_VALIDATE_EXISTING_CONNECTION:
      return (_("An existing connection has the same name"));
      break;
    case ERR_VALIDATE_EXISTING_ITEM_LEVEL:
      return (_("Existing item with the same name at the same level"));
      break;
    default:
      return (_("Name is not allowed"));
      break;
  }
}

/**
 * validate_name() - checks if a name is allowed for adding or updating
 * @param[in] p_parent parent node
 * @param[in] p_node_upd node being updated
 * @param[in] p_conn connection being updated
 * @param[in] item_name new name for connection or folder
 * @return 0 if name is allowed, an error value if not
 */
int
validate_name (struct GroupNode *p_parent, struct GroupNode *p_node_upd, struct Connection *p_conn, char *item_name)
{
  int i;
  struct Connection *p_conn_ctrl;

  

  if (item_name[0] == 0)
    return (ERR_VALIDATE_MISSING_VALUES);

  if (p_conn) /* adding or updating a connection */
    {
      if (p_conn->protocol[0] == 0 || p_conn->host[0] == 0)
        return (ERR_VALIDATE_MISSING_VALUES);

      log_debug ("check existing connections\n");

      p_conn_ctrl = get_connection (&conn_list, item_name);

      if (p_conn_ctrl && p_conn_ctrl != p_conn)
        {
          return (ERR_VALIDATE_EXISTING_CONNECTION);
        }
    }

  /* check if there is an item in the same level with the same name */

  log_debug ("check folders\n");

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i] == NULL)
        continue;

      if (!strcmp (p_parent->child[i]->name, item_name) && p_parent->child[i] != p_node_upd)
        {
          return (ERR_VALIDATE_EXISTING_ITEM_LEVEL);
        }
    }

  

  return (0);
}

/**
 * add_update_connection() - add and update connections
 * @param[in] p_node node of the connection being updated, NULL if adding new connection
 * @return a pointer to added/modified node
 */
struct GroupNode *
add_update_connection (struct GroupNode *p_node, struct Connection *p_conn_model)
{
  GtkBuilder *builder;
  GError *error=NULL;
  GtkWidget *notebook;
  char title[64], temp[256];
  char *emu_tmp, connection_name[1024], s_tmp[1024];
  int i, err_name_validation;
  int x_pad = 10, y_pad = 5;
  GtkWidget *dialog;
  GtkWidget *cancel_button, *ok_button;
  GtkWidget *table;
  GtkWidget *name_entry, *host_entry, *user_options_entry;
  GtkWidget *protocol_combo;
  //GList *emulation_glist = NULL;
  gint result;
  struct Protocol *p;
  struct Connection *p_conn = NULL;
  struct Connection *p_conn_tmp;

  char *p_enc;
  char *p_enc_b64;
  int len;

  struct Connection conn_new, *p_conn_ctrl = NULL;
  struct GroupNode *p_parent;
  struct GroupNode *p_node_return = NULL;

  char ui[256];
  
  log_debug ("Loading gui\n");

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/edit-connection.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }
    
  log_debug ("Loaded %s\n", ui);

  if (p_node == NULL)
    {
      strcpy (title, _("Add connection"));
      p_conn = p_conn_model; 
    }
  else
    {
      strcpy (title, _("Edit connection"));
      p_conn = cl_get_by_name (&conn_list, p_node->name);

      if (p_conn == NULL)
        return (NULL);
      
      log_debug ("Editing %s\n", p_conn->name);
    }

  log_debug ("%s\n", title);

  notebook = GTK_WIDGET (gtk_builder_get_object (builder, "notebook1"));

  /* basic */
  
  name_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_name"));

  if (p_conn)
    {
      gtk_entry_set_text (GTK_ENTRY (name_entry), p_conn->name);
    }

  host_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_host"));

  if (p_conn)
    gtk_entry_set_text (GTK_ENTRY (host_entry), p_conn->host);

  protocol_combo = GTK_WIDGET (gtk_builder_get_object (builder, "combo_protocol"));

  port_spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "spin_port"));

  // X11 Forwarding
  check_x11 = GTK_WIDGET (gtk_builder_get_object (builder, "check_x11forwarding"));

  if (p_conn)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_x11), p_conn->sshOptions.x11Forwarding);

  // Agent forwarding
  check_agentForwarding = GTK_WIDGET (gtk_builder_get_object (builder, "check_agentForwarding"));

  if (p_conn)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_agentForwarding), p_conn->sshOptions.agentForwarding);

  // Disable strict host key checking
  check_disable_key_checking = GTK_WIDGET (gtk_builder_get_object (builder, "check_disableStrictKeyChecking"));

  if (p_conn)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_disable_key_checking), p_conn->sshOptions.disableStrictKeyChecking);

  // Keep alive interval
  check_keepAliveInterval = GTK_WIDGET (gtk_builder_get_object (builder, "check_keepAliveInterval"));
  spin_keepAliveInterval = GTK_WIDGET (gtk_builder_get_object (builder, "spin_keepAliveInterval"));

  if (p_conn) {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_keepAliveInterval), p_conn->sshOptions.flagKeepAlive);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_keepAliveInterval), p_conn->sshOptions.keepAliveInterval);
  }

  // Key authentication (need to be created before sig_handler_prot)
  authWidgets.radio_auth_key = GTK_WIDGET (gtk_builder_get_object (builder, "radio_auth_key"));
  authWidgets.user_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_user"));
  authWidgets.password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_password"));

  if (p_conn)
    {
      gtk_entry_set_text (GTK_ENTRY (authWidgets.user_entry), p_conn->auth_user);
      gtk_entry_set_text (GTK_ENTRY (authWidgets.password_entry), p_conn->auth_password);
    }

  authWidgets.entry_private_key = GTK_WIDGET (gtk_builder_get_object (builder, "entry_private_key"));

  if (p_conn)
    gtk_entry_set_text (GTK_ENTRY (authWidgets.entry_private_key), p_conn->identityFile);

  authWidgets.button_select_private_key = GTK_WIDGET (gtk_builder_get_object (builder, "button_select_private_key"));
  g_signal_connect (G_OBJECT (authWidgets.button_select_private_key), "clicked", G_CALLBACK (select_private_key_cb), NULL);

  authWidgets.button_clear_private_key = GTK_WIDGET (gtk_builder_get_object (builder, "button_clear_private_key"));
  g_signal_connect (G_OBJECT (authWidgets.button_clear_private_key), "clicked", G_CALLBACK (clear_private_key_cb), NULL);

  /* Connect signal now, after port has been created */

  int sig_handler_prot = g_signal_connect (G_OBJECT (GTK_COMBO_BOX (protocol_combo)), "changed", G_CALLBACK (change_protocol_cb), NULL);

  i = 0;
  p = g_prot_list.head;

  while (p)
    {
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(protocol_combo), p->name);

      if (p_conn)
        {
          if (!strcmp (p_conn->protocol, p->name))
            gtk_combo_box_set_active (GTK_COMBO_BOX (protocol_combo), i);
        }

      i ++;
      p = p->next;
    }

  if (!p_conn)
    gtk_combo_box_set_active (GTK_COMBO_BOX (protocol_combo), 0);

  if (p_conn)
    {
      gtk_spin_button_set_value (GTK_SPIN_BUTTON (port_spin_button), p_conn->port);
    }
/*
  GtkWidget *emulation_combo = GTK_WIDGET (gtk_builder_get_object (builder, "combo_emulation"));

  for (i=1; i<=list_count (prefs.emulation_list, ':'); i++)
    {
      emu_tmp = (char *) malloc (64);

      list_get_nth (prefs.emulation_list, i, ':', emu_tmp);
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(emulation_combo), emu_tmp);

      if (p_conn)
        {
          if (!strcmp (p_conn->emulation, emu_tmp))
            {
              gtk_combo_box_set_active (GTK_COMBO_BOX (emulation_combo), i-1);
            }
        }
      else
        gtk_combo_box_set_active (GTK_COMBO_BOX (emulation_combo), 0);
    }
*/
  /* warnings */
  
  char warnings[1024];
  GtkWidget *label_warnings = GTK_WIDGET (gtk_builder_get_object (builder, "label_warnings"));

  strcpy (warnings, "<i>");

  if (p_conn)
    {
      if (p_conn->warnings == CONN_WARNING_NONE)
        strcat (warnings, "No warnings");
      else
        {
          if (p_conn->warnings & CONN_WARNING_HOST_DUPLICATED)
            {
              p_conn_tmp = cl_host_search (&conn_list, p_conn->host, p_conn->name);
              
              if (p_conn_tmp)
                {
                  sprintf (s_tmp, _("Connection <b>%s</b> has the same host\n"), p_conn_tmp->name);
                  strcat (warnings, s_tmp);
                }
            }

          if (p_conn->warnings & CONN_WARNING_PROTOCOL_COMMAND_NOT_FOUND)
            {
              char protocol_command[128];
              struct Protocol *p = get_protocol (&g_prot_list, p_conn->protocol);
  
              if (p)
                strcpy (protocol_command, p->command);
              else
                strcpy (protocol_command, "");

              sprintf (s_tmp, _("Protocol command <b>%s</b> not found\n"), protocol_command);
              strcat (warnings, s_tmp);
            }

          if (p_conn->warnings & CONN_WARNING_PROTOCOL_NOT_FOUND)
            {
              sprintf (s_tmp, _("Protocol <b>%s</b> not found\n"), p_conn->protocol);
              strcat (warnings, s_tmp);
            }
        }
    }

  strcat (warnings, "</i>");

  gtk_label_set_markup (GTK_LABEL (label_warnings), warnings);

  GtkWidget *ignore_warnings_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_hide_warnings"));
  
  if (p_conn)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ignore_warnings_check), p_conn->flags & CONN_FLAG_IGNORE_WARNINGS);


  /* Authentication */

  authWidgets.radio_auth_prompt = GTK_WIDGET (gtk_builder_get_object (builder, "radio_auth_prompt"));

  authWidgets.radio_auth_save = GTK_WIDGET (gtk_builder_get_object (builder, "radio_auth_save"));

  // Private key

  int authMode = p_conn ? p_conn->auth_mode : CONN_AUTH_MODE_PROMPT;

  log_debug("authMode = %d\n", authMode);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_prompt), authMode == CONN_AUTH_MODE_PROMPT);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save), authMode == CONN_AUTH_MODE_SAVE);
  radio_auth_save_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save), NULL); // Force signal

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key), authMode == CONN_AUTH_MODE_KEY);
  radio_auth_key_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key), NULL); // Force signal

  //if (p_conn)
  //  {
/*
      switch (authMode) {
        case CONN_AUTH_MODE_SAVE:
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save), TRUE);
          radio_auth_key_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key), NULL); // Force update for the other
          break;
        case CONN_AUTH_MODE_KEY:
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key), TRUE);
          radio_auth_save_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save), NULL); // Force update for the other
          break;
        default:
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_prompt), TRUE);
          radio_auth_key_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key), NULL); // Force update for the other
          radio_auth_save_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save), NULL); // Force update for the other
          break;    
        }
*/
  /*  }
  else
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_prompt), TRUE);
      radio_auth_key_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key), NULL); // Force update for the other
      radio_auth_save_cb (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save), NULL); // Force update for the other
    }*/

  g_signal_connect (authWidgets.radio_auth_save, "toggled", G_CALLBACK (radio_auth_save_cb), NULL);
  g_signal_connect (authWidgets.radio_auth_key, "toggled", G_CALLBACK (radio_auth_key_cb), NULL);

  /* Extra options */
  
  user_options_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_extra_options"));

  GtkWidget *directory_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_shared_dir"));

  if (p_conn)
    {
      gtk_entry_set_text (GTK_ENTRY (user_options_entry), p_conn->user_options);
      gtk_entry_set_text (GTK_ENTRY (directory_entry), p_conn->directory);
    }

  /* notes */
  
  GtkTextBuffer *note_buffer;
  GtkWidget *note_scrolwin, *note_view;

  note_view = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (note_view), TRUE);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (note_view), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin (GTK_TEXT_VIEW (note_view), 2);

  note_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (note_view));

  note_scrolwin = GTK_WIDGET (gtk_builder_get_object (builder, "scrolled_notes"));
  gtk_container_add (GTK_CONTAINER (note_scrolwin), note_view);

  if (p_conn)
    {
      if (p_conn->note[0])
        gtk_text_buffer_set_text (note_buffer, p_conn->note, -1);
    }

  GtkWidget *clear_note_button = GTK_WIDGET (gtk_builder_get_object (builder, "button_clear"));
  g_signal_connect (G_OBJECT (clear_note_button), "clicked", G_CALLBACK (clear_note_button_clicked_cb), note_buffer);
  g_signal_connect (G_OBJECT (note_buffer), "changed", G_CALLBACK (note_buffer_changed_cb), NULL);

  /* create dialog */

  dialog = gtk_dialog_new ();

  gtk_window_set_title (GTK_WINDOW (dialog), title);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (connections_dialog));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  //gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 10);
  //gtk_box_set_spacing (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), 10);
  //gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  //gtk_box_pack_end (GTK_BOX (GTK_DIALOG (dialog)->vbox), notebook, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), notebook, TRUE, TRUE, 0);

  cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  gtk_widget_grab_focus (name_entry);

  /* run dialog */
  
  while (1)
    {
      result = gtk_dialog_run (GTK_DIALOG (dialog));

      if (result == GTK_RESPONSE_OK)
        {
          strcpy (connection_name, gtk_entry_get_text (GTK_ENTRY (name_entry)));
          trim (connection_name);

          /* initialize a new connection structure */

          memset (&conn_new, 0x00, sizeof (struct Connection));

          /* 
           * if we are updating an existing connection, make a copy before updating
           * to keep some values
           */

          if (p_conn)
            connection_copy (&conn_new, p_conn);

          /* update values */

          strcpy (conn_new.name, connection_name);
          strcpy (conn_new.host, gtk_entry_get_text (GTK_ENTRY (host_entry)));
          trim (conn_new.host);

#if (GTK_MAJOR_VERSION == 2)
          strcpy (conn_new.protocol, gtk_combo_box_get_active_text (GTK_COMBO_BOX (protocol_combo)));
#else
          strcpy (conn_new.protocol, gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(protocol_combo)));
#endif
          trim (conn_new.protocol);
        
          conn_new.port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (port_spin_button));

          //strcpy (conn_new.emulation, gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (emulation_combo)));

          strcpy (conn_new.user_options, gtk_entry_get_text (GTK_ENTRY (user_options_entry)));
          strcpy (conn_new.directory, gtk_entry_get_text (GTK_ENTRY (directory_entry)));

          conn_new.sshOptions.x11Forwarding = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_x11)) ? 1 : 0;
          conn_new.sshOptions.agentForwarding = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_agentForwarding)) ? 1 : 0;
          conn_new.sshOptions.disableStrictKeyChecking = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_disable_key_checking)) ? 1 : 0;
          conn_new.sshOptions.flagKeepAlive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_keepAliveInterval)) ? 1 : 0;
          conn_new.sshOptions.keepAliveInterval = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin_keepAliveInterval));

          //conn_new.flags |= (CONN_FLAG_MASK & gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ignore_warnings_check)) ? 1 : 0);
          conn_new.flags = (conn_new.flags & ~(CONN_FLAG_IGNORE_WARNINGS)) | ((gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ignore_warnings_check)) ? 1 : 0) << 0);
          //p_prot->flags = (p_prot->flags & ~(PROT_FLAG_ASKUSER)) | ((gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (askuser_check)) ? 1 : 0) << 0);

          //conn_new.auth = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (auth_check)) ? 1 : 0;
          //conn_new.auth = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (authWidgets.radio_auth_save)) ? 1 : 0;
          if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_prompt)))
            conn_new.auth_mode = CONN_AUTH_MODE_PROMPT;
          else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_save)))
            conn_new.auth_mode = CONN_AUTH_MODE_SAVE;
          else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(authWidgets.radio_auth_key)))
            conn_new.auth_mode = CONN_AUTH_MODE_KEY;
            
          strcpy (conn_new.auth_user, gtk_entry_get_text (GTK_ENTRY (authWidgets.user_entry)));
          strcpy (conn_new.auth_password, gtk_entry_get_text (GTK_ENTRY (authWidgets.password_entry)));

          /* encrypt password */
/*
          p_enc = Encrypt (KEY, conn_new.auth_password, sizeof (conn_new.auth_password));
          p_enc_b64 = g_base64_encode (p_enc, strlen (p_enc));
          memcpy (conn_new.auth_password_encrypted, p_enc_b64, strlen (p_enc_b64));
*/
          
          log_debug("encryption\n");
          
          if (conn_new.auth_password[0] != 0)
            strcpy (conn_new.auth_password_encrypted, des_encrypt_b64 (conn_new.auth_password));

          // Private key
          strcpy (conn_new.identityFile, gtk_entry_get_text (GTK_ENTRY (authWidgets.entry_private_key)));

          /* Notes */
          GtkTextIter start_iter, end_iter;
          gtk_text_buffer_get_bounds (note_buffer, &start_iter, &end_iter);
          strcpy (conn_new.note, gtk_text_buffer_get_text (note_buffer, &start_iter, &end_iter, FALSE));

          if (p_node) /* edit */
            {
              log_debug("Edit\n");
              log_debug ("Validating %s ...\n", p_conn->name);
              
              //err_name_validation = validate_name (g_selected_node->parent, g_selected_node, p_conn, connection_name);
              err_name_validation = validate_name (p_node->parent, p_node, p_conn, connection_name);

              if (!err_name_validation)
                {
                  log_debug("Name validated\n");
                  
                  connection_copy (p_conn, &conn_new);

                  /* update name in the tree */
                  //strcpy (g_selected_node->name, p_conn->name); 
                  //p_node_return = g_selected_node;
                  strcpy (p_node->name, p_conn->name); 
                  p_node_return = p_node;
                  break;
                }
              else
                msgbox_error (get_validation_error_string (err_name_validation));
                
              log_debug("Edit end\n");
            }
          else        /* add */
            {
              log_debug("add\n");
              
              p_parent = get_parent_node_for_insert ();

              err_name_validation = validate_name (p_parent, NULL, &conn_new, connection_name);

              if (!err_name_validation)
                {
                  p_node_return = group_node_add_child (p_parent, GN_TYPE_CONNECTION, conn_new.name);
                  p_conn_ctrl = cl_insert_sorted (&conn_list, &conn_new);

                  /* refresh list for search entry completion */
                  refresh_search_completion ();

                  break;
                }
              else
                msgbox_error (get_validation_error_string (err_name_validation));
            }
        }
      else
        {
          log_debug ("user clicked CANCEL\n");
          break;
        }
    }

  gtk_widget_destroy (dialog);

  /* cl_check (&conn_list); */

  

  return (p_node_return);
}


/**
 * add_update_folder() - add or update a folder
 * @param[in] p_node tree node being updated, NULL if adding new folder
 * @return a pointer to added/modified node
 */
struct GroupNode *
add_update_folder (struct GroupNode *p_node)
{
  char title[64], temp[256];
  char folder_name[1024];
  int i, proceed, err_name_validation, result;
  GtkWidget *dialog;
  GtkWidget *cancel_button, *ok_button;
  GtkWidget *name_entry;
  struct GroupNode *p_node_return = NULL;
  struct GroupNode *p_parent;
 
  

  if (p_node == 0)
    strcpy (title, _("Create folder"));
  else
    strcpy (title, _("Rename folder"));

  name_entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (name_entry), TRUE);

  GtkWidget *name_hbox = create_entry_control (_("Folder name"), name_entry);

  if (p_node)
    {
      gtk_entry_set_text (GTK_ENTRY (name_entry), p_node->name);
    }

  //strcpy (original_name, p_node->name);

  /* create dialog */

  dialog = gtk_dialog_new ();

  gtk_window_set_title (GTK_WINDOW (dialog), title);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (connections_dialog));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  //gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 10);
  gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  gtk_box_pack_end (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), name_hbox, TRUE, TRUE, 0);

  cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  gtk_widget_grab_focus (name_entry);

  /* run dialog */
  
  while (1)
    {
      result = gtk_dialog_run (GTK_DIALOG (dialog));

      if (result == GTK_RESPONSE_OK)
        {
          strcpy (folder_name, gtk_entry_get_text (GTK_ENTRY (name_entry)));
          trim (folder_name);

          if (p_node) /* edit */
            {
              err_name_validation = validate_name (g_selected_node->parent, g_selected_node, NULL, folder_name);

              if (!err_name_validation)
                {
                  /* update name in the tree */
                  strcpy (g_selected_node->name, folder_name);  
                  p_node_return = g_selected_node;
                  break;
                }
              else
                msgbox_error (get_validation_error_string (err_name_validation));
            }
          else /* add */
            {
              p_parent = get_parent_node_for_insert ();

              log_debug ("p_parent = %s\n", p_parent->name);
/*
              if (group_node_find_child (p_parent, folder_name))
                msgbox_error (_("Item with the same name already existing"));
              else
                {
                  p_node_return = group_node_add_child (p_parent, GN_TYPE_FOLDER, folder_name);
                  break;
                }
*/
              err_name_validation = validate_name (p_parent, NULL, NULL, folder_name);

              if (!err_name_validation)
                {
                  p_node_return = group_node_add_child (p_parent, GN_TYPE_FOLDER, folder_name);
                  break;
                }
              else
                msgbox_error (get_validation_error_string (err_name_validation));

            }
        }
      else
        break;
    } /* while */

  gtk_widget_destroy (dialog);

  

  return (p_node_return);
}


GtkTreeStore *
connection_get_tree_store ()
{
  return (model);
}

void
append_node_gtk_tree (struct GroupNode *p_node, GtkTreeIter *parentIter)
{
  int i;
  struct Connection *p_conn;
  GtkTreeIter child;
  char port_s[32];

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->child[i])
        {
          gtk_tree_store_append (model, &child, parentIter);
          //log_debug ("added %s %s\n", p_node->child[i]->type == GN_TYPE_CONNECTION ? "connection" : "folder", p_node->child[i]->name);

          if (p_node->child[i]->type == GN_TYPE_CONNECTION)
            {
              p_conn = cl_get_by_name (&conn_list, p_node->child[i]->name);

              if (p_conn->port > 0)
                sprintf (port_s, "%d", p_conn->port);
              else
                strcpy (port_s, "");

              gtk_tree_store_set (model, &child,
                                  NAME_COLUMN, p_conn->name,
                                  ADDRESS_COLUMN, p_conn->host,
                                  PROTOCOL_COLUMN, p_conn->protocol,
                                  PORT_COLUMN, port_s, 
                                  PIXMAP_COLUMN, p_conn->note[0] != 0 ? postit_img : NULL, 
                                  -1);
            }
          else
            {
              gtk_tree_store_set (model, &child, NAME_COLUMN, p_node->child[i]->name, PIXMAP_COLUMN, NULL, -1);

              append_node_gtk_tree (p_node->child[i], &child);
            }
        }
    }
}

void
expand_connection_tree_view_groups (GtkTreeView *tree_view, struct GroupNode *p_parent)
{
  int i;
  char path_s[256];
  GtkTreePath *path;
  
  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i])
        {
          if (p_parent->child[i]->type == GN_TYPE_FOLDER && p_parent->child[i]->expanded)
            {
              strcpy (path_s, "");
              group_tree_get_node_path (&g_groups, p_parent->child[i], path_s);
              path = gtk_tree_path_new_from_string (path_s);
              
              //log_debug ("expanding %s (%s)\n", p_parent->child[i]->name, path_s);

              if (gtk_tree_view_expand_row (tree_view, path, FALSE) == FALSE)
                {
                  log_debug ("can't expand %s\n", p_parent->child[i]->name); 
                }
                  
              expand_connection_tree_view_groups (tree_view, p_parent->child[i]);
            }
        }
    }
}

void
rebuild_tree_store ()
{
  g_rebuilding_tree_store = 1;

  /* disable signals */
  //rows_signals_enabled = 0;

  gtk_tree_store_clear (model);

  /* set warnings */
  cl_check (&conn_list);

  log_debug ("sorting tree...\n");
  group_tree_sort (&g_groups, 0);

  log_debug ("refreshing ...\n");
  append_node_gtk_tree (&g_groups.root, NULL);

  g_rebuilding_tree_store = 0;
}

void 
refresh_connection_tree_view (GtkTreeView *tree_view)
{
  

  //rebuild_tree_store ();

  if (tree_view == NULL)
    return;
  
  log_debug ("expanding ...\n");
  expand_connection_tree_view_groups (tree_view, &g_groups.root);

  //gtk_tree_view_set_show_expanders (tree_view, TRUE);
  //gtk_tree_view_set_expander_column (tree_view, NULL);

  
}

gboolean 
conn_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  int keyReturn, keyEnter;

#if (GTK_MAJOR_VERSION == 2)
  keyReturn = GDK_Return;
  keyEnter = GDK_KP_Enter;
#else
  keyReturn = GDK_KEY_Return;
  keyEnter = GDK_KEY_KP_Enter;
#endif

  if (event->keyval == keyReturn || event->keyval == keyEnter)
    {
#ifdef DEBUG
      printf ("conn_key_pressed() : pressed enter key\n");
#endif
      gtk_dialog_response (GTK_DIALOG (widget), GTK_RESPONSE_OK);
    }

  return FALSE;
}

/*
gboolean 
treeview_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{ 
  GtkTreePath *path;
  GtkTreeIter iter;
#ifdef DEBUG
  gchar *name;
#endif

#ifdef DEBUG
      printf ("treeview_key_press_cb()\n");
#endif

  if (event->keyval != GDK_Return && event->keyval != GDK_KP_Enter)
    {
#ifdef DEBUG
      printf ("treeview_key_press_cb() : not enter/return\n");
#endif
      gtk_tree_selection_get_selected (GTK_TREE_SELECTION (user_data), NULL, &iter);
#ifdef DEBUG
      gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, NAME_COLUMN, &name, -1);
      printf ("treeview_key_press_cb() : selected %s\n", name);
#endif
      path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
      //gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tree_view), path, NULL, TRUE, 0, 0);
      gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (tree_view), 0, 11);
      gtk_tree_path_free (path);
    }

  return FALSE;
}
*/

void
cursor_changed_cb (GtkTreeView *tree_view, gpointer user_data)
{
  GtkTreeModel *l_model;
  GtkTreePath *path;
  GtkTreeIter iter;
  GtkTreeSelection *selection;

  //

  //selection = gtk_tree_view_get_selection (tree_view);

  //gtk_tree_selection_get_selected (selection, &l_model, &iter);
  gtk_tree_selection_get_selected (user_data, &l_model, &iter);

  if (!gtk_tree_selection_get_selected (/*selection*/ user_data, &l_model, &iter))
    {
      g_selected_node = NULL;
      return;
    }

  gtk_tree_view_get_cursor (tree_view, &path, NULL);
  
  //log_debug ("path = %s\n", gtk_tree_path_to_string (path));
  
  gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tree_view), path, NULL, FALSE, 0.5, 0);

  struct GroupNode *p_node;
  p_node = group_node_find_by_numeric_path (group_tree_get_root (&g_groups), gtk_tree_path_to_string (path), 1);

  if (p_node)
    {
      g_selected_node = p_node;
      //log_debug ("%s %s\n", p_node->type == GN_TYPE_FOLDER ? "folder" : "connection", g_selected_node->name);  
    }
  else
    {
      g_selected_node = NULL;
      log_debug ("node not found\n");
    }

  //
}

gboolean
expand_row_cb (GtkTreeView *tree_view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data)
{
  struct GroupNode *p_node;

  p_node = group_node_find_by_numeric_path (group_tree_get_root (&g_groups), gtk_tree_path_to_string (path), 1);

  if (p_node)
    {
      p_node->expanded = 1;
      //log_debug ("expanded %s\n", p_node->name);
    }

  return (FALSE);
}

gboolean
collapse_row_cb (GtkTreeView *tree_view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data)
{
  struct GroupNode *p_node;

  p_node = group_node_find_by_numeric_path (group_tree_get_root (&g_groups), gtk_tree_path_to_string (path), 1);

  if (p_node)
    {
      p_node->expanded = 0;
      log_debug ("collapsed %s\n", p_node->name);
    }

  return (FALSE);
}

/* row_activated_cb() - callback function when a row has been double-clicked in the Log In dialog */
void
row_activated_cb (GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
  GtkTreeSelection *selection;
  struct Connection conn;

  

  selection = gtk_tree_view_get_selection (tree_view);

  if (get_selected_connection (selection, &conn))
    g_dialog_connections_connect = 1;
    //gtk_dialog_response (GTK_DIALOG (user_data), GTK_RESPONSE_OK);

  
}

/* row_activated_quick_cb() - callback function when a row has been double-clicked in the quick launch window */
void
row_activated_quick_cb (GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  gboolean have_iter;
  struct Connection *p_conn_selected, conn;
  struct Connection *p_conn_new, conn1;
  struct Protocol *p_prot;
  gint sel_port;
  gchar *sel_name;
  struct ConnectionTab *p_connection_tab;
  int connect_it = 0, retcode;

  

  selection = gtk_tree_view_get_selection (tree_view);

  connect_it = get_selected_connection (selection, &conn);
    
  if (connect_it)
    {
      gtk_tree_selection_unselect_all (selection);

      p_connection_tab = connection_tab_new ();
      
      log_debug ("selected '%s'\n", conn.name);

      memset (&(p_connection_tab->connection), 0, sizeof (struct Connection));

      connection_copy (&p_connection_tab->connection, &conn);

      log_debug ("connecting to '%s' ...\n", p_connection_tab->connection.name);

      retcode = log_on (p_connection_tab);

      if (retcode == 0)
        {
          connection_tab_add (p_connection_tab);
          p_current_connection_tab = p_connection_tab;
          gtk_widget_grab_focus (p_current_connection_tab->hbox_terminal);
        }
        
      update_screen_info ();
    }

  
}

void
connection_name_cell_data_func (GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model,
                                GtkTreeIter *iter, gpointer user_data)
{
  gchar *name;
  char markup_string[256], color[64];
  struct Connection *p_conn;

  gtk_tree_model_get (model, iter, NAME_COLUMN, &name, -1);

  p_conn = (struct Connection *) get_connection (&conn_list, name);

  if (p_conn)
    {
      //log_debug("%s is a connection\n", name);
/*
      if (p_conn->warnings != CONN_WARNING_NONE)
        g_object_set (renderer, "foreground", prefs.warnings_color, "foreground-set", TRUE, NULL);
      else
        g_object_set (renderer, "foreground", "Black", "foreground-set", TRUE, NULL);
*/
      if (p_conn->warnings != CONN_WARNING_NONE)
        {
          if ((p_conn->warnings & CONN_WARNING_PROTOCOL_NOT_FOUND) || (p_conn->warnings & CONN_WARNING_PROTOCOL_COMMAND_NOT_FOUND))
            strcpy (color, prefs.warnings_error_color);
          else
            strcpy (color, prefs.warnings_color);

          sprintf (markup_string, "<span color=\"%s\">%s</span>", color, name);
        }
      else
        sprintf (markup_string, "%s", name);
    }
  else
    {
      sprintf (markup_string, "<b>%s</b>", name);
    }

  //g_object_set (renderer, "text", name, NULL);
  g_object_set (renderer, "markup", markup_string, NULL);
}

/**
 * cell_tooltip_callback_config_default() - Shows a tooltip whenever the user puts the mouse over a row
 */
gboolean
cell_tooltip_callback (GtkWidget *widget, gint x, gint y,
                       gboolean keyboard_tip, GtkTooltip *tooltip, gpointer user_data)
{
  struct Connection *p_conn;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeViewColumn *column;
  GtkTreePath *path;

  if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget), &x, &y, keyboard_tip, &model, 0, &iter))
    {
      return FALSE;
    }

  if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), x, y, 0, &column, 0, 0))
    {
      return FALSE;
    }

  /* get connection from iter */

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

  struct GroupNode *p_node;
  p_node = group_node_find_by_numeric_path (group_tree_get_root (&g_groups), gtk_tree_path_to_string (path), 1);

  if (p_node)
    {
      if (p_node->type == GN_TYPE_CONNECTION)
        {
          p_conn = cl_get_by_name (&conn_list, p_node->name);
    
          if (p_conn)
            {
              if (p_conn->note[0])
                {
                  gtk_tooltip_set_text (tooltip, p_conn->note);
                  return TRUE;
                }
            }
        }
    }

  return FALSE;
}

void
on_drag_data_inserted (GtkTreeModel *tree_model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data)
{
  struct GtkTreeView *tree_view = (struct GtkTreeView *) user_data;
  struct GroupNode *p_parent;
  char *user_path, *pc;
  char parent_path[256];

  if (!rows_signals_enabled) return;
  if (g_rebuilding_tree_store) return;
    

  if (g_selected_node == NULL)
    return;

  /* find the path where user moved the row */
  
  user_path = gtk_tree_path_to_string (path);
  
  /* find path to parent node (delete last number in x:y:z... sequence) */
  
  strcpy (parent_path, user_path);
  pc = (char *) strrchr (parent_path, ':');
  
  if (pc)
    {
      *pc = 0;
      p_parent = group_node_find_by_numeric_path (group_tree_get_root (&g_groups), parent_path, 1);
    }
  else
    p_parent = group_tree_get_root (&g_groups);

  log_debug ("user parent_path=%s\n", p_parent->name);

  if (p_parent->type == GN_TYPE_FOLDER)
    {
      p_parent->expanded = 1;
    }
  else
    p_parent = p_parent->parent;
  
  log_debug ("%s user_path=%s parent_path=%s (%s)\n", g_selected_node->name, user_path, parent_path, p_parent->name);

  if (!group_node_find_child (p_parent, g_selected_node->name))
    {
      group_node_move (p_parent, g_selected_node);
    }
    
   /* disable signals callback functions */
   
  rows_signals_enabled = 0;

  
}

/* called when dropping in dialog, doesn't use functions in gtk main loop */
/*
void
on_drag_data_deleted_dialog (GtkTreeModel *tree_model, GtkTreePath *path, gpointer user_data)
{
  struct GtkTreeView *tree_view = (struct GtkTreeView *) user_data;

  if (g_rebuilding_tree_store) return;

  

  g_signal_emit_by_name (tree_view, "lterm-refresh-dialog-tree-view");
  
  rows_signals_enabled = 1;
  
  
}
*/

void
on_drag_data_deleted (GtkTreeModel *tree_model, GtkTreePath *path, gpointer user_data)
{
  struct GtkTreeView *tree_view = (struct GtkTreeView *) user_data;

  if (g_rebuilding_tree_store) return;

  

  ifr_add (ITERATION_REBUILD_TREE_STORE, NULL);

  if (tree_view != NULL)
    {
      //log_debug ("pointer = %ld\n", GTK_TREE_VIEW (tree_view));
      ifr_add (ITERATION_REFRESH_TREE_VIEW, GTK_TREE_VIEW (tree_view));
    }

  /* enable signals callback functions again */
  
  rows_signals_enabled = 1;
  
  
}

/*
void
on_drag_data_changed (GtkTreeModel *tree_model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data)
{
  if (!rows_signals_enabled) return;

  GValue value={0,};
  char *cptr, *spath;

  
  
  if (g_selected_node == NULL)
    return;
  
  log_debug ("moved %s\n", g_selected_node->name);

  gtk_tree_model_get_value (tree_model, iter, NAME_COLUMN, &value);
  cptr = (char*) g_value_get_string(&value);
  
  spath = gtk_tree_path_to_string(path);
  
  log_debug("cell=%s, path=%s\n", cptr, spath);
  
  g_value_unset (&value);
}
*/

static GtkTargetEntry row_targets[] = {
  { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_APP, 0}
};

/**
 * create_connections_tree_view() - creates a widget for list of connections
 * @return a pointer to the created widget
 */
GtkWidget *
create_connections_tree_view ()
{
  GtkTreeModel *tree_model;
  GtkTreeIter iter;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *column;

  FILE *fp;
  char line[1024], s_path[16];
  char name[64], host[128], protocol[64], port_tmp[16];
  int port;
  int retcode = 0;

  struct Connection *p_conn_selected;
  struct Connection *p_conn_new;
  struct Protocol *p_prot;
  gint sel_port;
  gchar *sel_name;

  //GtkTreeView *tree_view = gtk_tree_view_new ();
  GtkWidget *tree_view = gtk_tree_view_new ();
  gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), GTK_TREE_MODEL (model));

  g_object_set (tree_view, "has-tooltip", TRUE, (char *) 0);

  g_signal_connect (tree_view, "query-tooltip", (GCallback) cell_tooltip_callback, 0);
  g_signal_connect (tree_view, "test-expand-row", G_CALLBACK (expand_row_cb), NULL);
  g_signal_connect (tree_view, "test-collapse-row", G_CALLBACK (collapse_row_cb), NULL);

  /* draw lines interconnecting the expanders */ 
  gtk_tree_view_set_enable_tree_lines (GTK_TREE_VIEW (tree_view), TRUE);

  /* sets which grid lines to draw */
  //gtk_tree_view_set_grid_lines (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_GRID_LINES_NONE);

  gtk_widget_show (GTK_WIDGET(tree_view));


  /* Name */

  GtkCellRenderer *name_cell = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn *name_column = gtk_tree_view_column_new_with_attributes (_("Name"), name_cell, "text", NAME_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (name_column));

  //g_object_set (name_cell, "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, NULL);
  gtk_tree_view_column_set_cell_data_func (name_column, name_cell, connection_name_cell_data_func, NULL, NULL);
  //gtk_tree_view_column_pack_start (column, cell, TRUE);

  /* Address */

  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Host"), cell, "text", ADDRESS_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  /* Protocol */

  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Protocol"), cell, "text", PROTOCOL_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  /* Port */

  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Port"), cell, "text", PORT_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  cell = gtk_cell_renderer_pixbuf_new ();
  column = gtk_tree_view_column_new_with_attributes ("", cell, "pixbuf", PIXMAP_COLUMN, NULL);
  //gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (column), GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_max_width (GTK_TREE_VIEW_COLUMN (column), 30);
  //gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  //g_signal_connect (G_OBJECT (tree_view), "row-activated", G_CALLBACK (row_activated_cb), dialog);

  /* sets "Name" as the column where the interactive search code should search */

  gtk_tree_view_set_search_column (GTK_TREE_VIEW (tree_view), prefs.search_by);


  /* Enable drag and drop */

  //tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));
  
  /* row-inserted callback is common for quick launch window and dialog */
  //g_signal_connect (tree_model, "row-inserted", G_CALLBACK (on_drag_data_inserted), GTK_TREE_VIEW (tree_view));
  //g_signal_connect (tree_model, "row-changed", G_CALLBACK (on_drag_data_changed), NULL);
  
  gtk_tree_view_set_reorderable (GTK_TREE_VIEW (tree_view), TRUE); /* for drag and drop */
/*
  gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view), GDK_BUTTON1_MASK, row_targets,
                                          G_N_ELEMENTS (row_targets), GDK_ACTION_MOVE);
  gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (tree_view), row_targets, 
                                        G_N_ELEMENTS (row_targets), GDK_ACTION_MOVE);
*/

  return (tree_view);
}

GtkWidget *
create_search_by_combo ()
{
#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *search_by_combo = gtk_combo_box_new_text ();
  gtk_combo_box_append_text (GTK_COMBO_BOX (search_by_combo), SEARCH_BY_NAME);
  gtk_combo_box_append_text (GTK_COMBO_BOX (search_by_combo), SEARCH_BY_HOST);
#else
  GtkWidget *search_by_combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(search_by_combo), SEARCH_BY_NAME);
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(search_by_combo), SEARCH_BY_HOST);
#endif

  gtk_combo_box_set_active (GTK_COMBO_BOX (search_by_combo), prefs.search_by);
  gtk_widget_show (search_by_combo);

  return (search_by_combo);
}

int
get_selected_connection (GtkTreeSelection *select, struct Connection *p_conn)
{
  GtkTreeIter iter;
  gboolean have_iter;
  gchar *sel_name;
  struct Connection *p_conn_selected;
  int rc = 0;

  

  if (gtk_tree_selection_get_selected (select, NULL, &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, NAME_COLUMN, &sel_name, -1);

      p_conn_selected = get_connection (&conn_list, sel_name);

      log_debug ("%s\n", sel_name);

      if (p_conn_selected)
        {
          memset (p_conn, 0x00, sizeof (struct Connection));
          connection_copy (p_conn, p_conn_selected);
          rc = 1;
        }

      g_free (sel_name);
  }

  

  return (rc);
}

void
move_cursor_to_node (GtkTreeView *tree_view, struct GroupNode *p_node)
{
  char path_s[256];
  GtkTreePath *path;

  

  group_tree_sort (&g_groups, 0);

  //refresh_connection_tree_view (GTK_TREE_VIEW (tree_view));
  rebuild_tree_store ();
  expand_connection_tree_view_groups (tree_view, &g_groups.root);

  gtk_widget_grab_focus (GTK_WIDGET(tree_view));

  if (p_node)
    {
      strcpy (path_s, "");
      group_tree_get_node_path (&g_groups, p_node, path_s);
    }
  else
    strcpy (path_s, "0");

  log_debug ("path = %s\n", path_s);

  /* move to right connection */

  path = gtk_tree_path_new_from_string (path_s);

  if (path)
    gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree_view), path, NULL, FALSE);

  
}

void
new_connection_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  struct GroupNode *p_node;
  GtkTreeView *tree_view = user_data;

  p_node = add_update_connection (NULL, NULL);

  if (p_node)
    move_cursor_to_node (tree_view, p_node);
}

void
edit_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  GtkTreeView *tree_view = user_data;
  //struct Connection *p_conn;
  struct GroupNode *p_node = NULL;
  int move_cursor = 0;

  if (g_selected_node == NULL)
    return;

  if (g_selected_node->type == GN_TYPE_CONNECTION)
    {
      /*
      p_conn = cl_get_by_name (&conn_list, g_selected_node->name);

      if (p_conn)
        {
          if (p_node = add_update_connection (p_conn))
            move_cursor = 1;
        }
      */

      if (p_node = add_update_connection (g_selected_node, NULL))
        move_cursor = 1;
    }
  else
    {
      if (p_node = add_update_folder (g_selected_node))
        move_cursor = 1;
    }

  if (move_cursor)
    move_cursor_to_node (tree_view, p_node);
}

void
duplicate_connection_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  GtkTreeView *tree_view = user_data;
  struct Connection *p_conn, conn_new;
  struct GroupNode *p_node_copy = NULL;
  char new_name[256];
  int i, err;

  if (g_selected_node == NULL)
    return;

  if (g_selected_node->type != GN_TYPE_CONNECTION)
    return;
  
  p_conn = cl_get_by_name (&conn_list, g_selected_node->name);
  
  if (p_conn == NULL)
    return;
  
  i = 1;
  
  while (1)
    {
      sprintf (new_name, "%s (%d)", p_conn->name, i);
      
      log_debug ("trying %s\n", new_name);
      
      err = validate_name (g_selected_node->parent, g_selected_node, p_conn, new_name);
      
      log_debug ("%s\n", get_validation_error_string (err));
      
      if (err == 0)
        {
          memset (&conn_new, 0, sizeof (struct Connection));
          connection_copy (&conn_new, p_conn);
          strcpy (conn_new.name, new_name);
          
          p_node_copy = group_node_add_child (g_selected_node->parent, GN_TYPE_CONNECTION, conn_new.name);
          
          if (p_node_copy)
            cl_insert_sorted (&conn_list, &conn_new);
          else
            {
              log_debug ("can't create node %s\n", new_name);
            }
            
          break;
        }
      
      i ++;
    }
    
  if (p_node_copy)
    move_cursor_to_node (tree_view, p_node_copy);
}

void
delete_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  int rc;
  GtkTreeView *tree_view = user_data;
  GtkTreePath *path;
  char confirm_remove_message[256];

  if (g_selected_node == NULL)
    return;

  if (g_selected_node->type == GN_TYPE_CONNECTION)
    sprintf (confirm_remove_message, _("Remove connection '%s'?"), g_selected_node->name);
  else  
    sprintf (confirm_remove_message, _("Remove folder '%s' and all his connections?"), g_selected_node->name);

  rc = msgbox_yes_no (confirm_remove_message);

  if (rc == GTK_RESPONSE_YES)
    {
      group_node_delete_child (g_selected_node->parent, g_selected_node);
/*
      path = gtk_tree_path_new_from_string ("0");

      if (path)
        gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree_view), path, NULL, FALSE);
*/
      move_cursor_to_node (tree_view, NULL);
    }
}

void
new_folder_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  GtkTreeView *tree_view = user_data;
  struct GroupNode *p_node;

  p_node = add_update_folder (NULL);

  if (p_node)
    {
      move_cursor_to_node (tree_view, p_node);
    }
}

/*
void
refresh_dialog_tree_view_cb (gpointer user_data)
{
  struct GtkTreeView *tree_view = (struct GtkTreeView *) user_data;

  

  rebuild_tree_store ();
  refresh_connection_tree_view (GTK_TREE_VIEW (tree_view));

  
}
*/

gint
dialog_delete_event_cb (GtkWidget *window, GdkEventAny *e, gpointer data)
{
  g_dialog_connections_running = 0;

  return TRUE;
}

void
ok_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  GtkTextBuffer *buffer = (GtkTextBuffer *) user_data;

  g_dialog_connections_connect = 1;
}

/**
 * choose_manage_connection() - choose and manage connections
 * @param[out] p_conn new or updated connection
 * @return 0 on success
 */
int
choose_manage_connection (struct Connection *p_conn)
{
  //GtkWidget *dialog;
  GtkWidget *vpaned;

  GtkWidget *scrolled_window;

  GtkTreeIter iter;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  GtkTreeModel *tree_model;
  //GtkHBox *buttons_hbox;
  GtkWidget *buttons_hbox;
  GtkAccelGroup *gtk_accel = gtk_accel_group_new ();
  GClosure *closure;
  GdkScreen *screen;

  //GtkTreePath *path;

  gboolean have_iter;

  FILE *fp;
  char line[1024], s_path[16];
  char name[64], host[128], protocol[64], port_tmp[16];
  //char path_s[256];
  int port;
  int is_selection_selected;
  int retcode = 1;

  struct Connection *p_conn_selected;
  struct Connection *p_conn_new;
  struct Protocol *p_prot;
  struct GroupNode *p_node;
  gint sel_port;
  gchar *sel_name;

  

  /* disable DND signals for QLW */
/*
  g_signal_handler_disconnect (g_quick_launch_window.tree_model, g_quick_launch_window.row_inserted_handler);
  g_signal_handler_disconnect (g_quick_launch_window.tree_model, g_quick_launch_window.row_deleted_handler);
*/
  g_signal_handler_block (g_quick_launch_window.tree_model, g_quick_launch_window.row_inserted_handler);
  g_signal_handler_block (g_quick_launch_window.tree_model, g_quick_launch_window.row_deleted_handler);

  /* create the window */

  GtkWidget *dialog_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  connections_dialog = dialog_window;
    
  gtk_window_set_modal (GTK_WINDOW (dialog_window), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog_window), GTK_WINDOW (main_window));
  gtk_window_set_position (GTK_WINDOW (dialog_window), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_title (GTK_WINDOW (dialog_window), _("Log on"));

  g_signal_connect (dialog_window, "delete_event", G_CALLBACK (dialog_delete_event_cb), NULL);

#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *dialog_vbox = gtk_vbox_new (FALSE, 0);
#else
  GtkWidget *dialog_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif

  /* create a tree view for connections before buttons so we can pass it to callback functions */

  GtkWidget *tree_view = create_connections_tree_view ();
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

/*
  guint sig_refresh_tree_view = g_signal_new ("lterm-refresh-dialog-tree-view",
                                              G_TYPE_OBJECT, G_SIGNAL_RUN_CLEANUP,
                                              0, NULL, NULL,
                                              g_cclosure_marshal_VOID__POINTER,
                                              G_TYPE_NONE, 1, G_TYPE_POINTER);

  g_signal_connect (tree_view, "lterm-refresh-dialog-tree-view", G_CALLBACK (refresh_dialog_tree_view_cb), tree_view);
*/

  refresh_connection_tree_view (GTK_TREE_VIEW (tree_view));
  //expand_connection_tree_view_groups (GTK_TREE_VIEW (tree_view), &g_groups.root);

  /* refresh quick launch window expansders */
  //expand_connection_tree_view_groups (GTK_TREE_VIEW (g_quick_launch_window.tree_view), &g_groups.root);
  //ifr_add (ITERATION_REFRESH_QUICK_LAUCH_TREE_VIEW, NULL);

/*
#if (GTK_MAJOR_VERSION == 2)
  g_signal_connect (GTK_OBJECT (tree_view), "cursor-changed", GTK_SIGNAL_FUNC (cursor_changed_cb), select);
#else
  g_signal_connect (tree_view, "cursor-changed", G_CALLBACK (cursor_changed_cb), select);
#endif
*/
  g_signal_connect (tree_view, "cursor-changed", G_CALLBACK (cursor_changed_cb), select);

  tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));
  g_signal_connect (tree_model, "row-inserted", G_CALLBACK (on_drag_data_inserted), GTK_TREE_VIEW (tree_view));
  g_signal_connect (tree_model, "row-deleted", G_CALLBACK (on_drag_data_deleted), GTK_TREE_VIEW (tree_view));

  /* create a new scrolled window, with scrollbars only if needed, and put the tree view inside */

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_ETCHED_IN);
  //gtk_container_set_resize_mode (GTK_SCROLLED_WINDOW (scrolled_window), GTK_RESIZE_QUEUE);
  gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);
  gtk_widget_show (scrolled_window);


  /* command buttons */

#if (GTK_MAJOR_VERSION == 2)
  buttons_hbox = gtk_hbox_new (FALSE, 5);
#else
  buttons_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
#endif

  gtk_container_set_border_width (GTK_CONTAINER (buttons_hbox), 5);

  GtkWidget *new_connection_button = gtk_button_new ();
  gtk_button_set_image (GTK_BUTTON (new_connection_button), gtk_image_new_from_stock (MY_STOCK_PLUS, GTK_ICON_SIZE_LARGE_TOOLBAR));
  gtk_button_set_image_position (GTK_BUTTON (new_connection_button), GTK_POS_TOP);
  gtk_button_set_label (GTK_BUTTON (new_connection_button), _("Add"));
  gtk_button_set_relief (GTK_BUTTON (new_connection_button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text (new_connection_button, _("Add a new connection"));
  gtk_box_pack_start (GTK_BOX (buttons_hbox), new_connection_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (new_connection_button), "clicked", G_CALLBACK (new_connection_button_clicked_cb), tree_view);

  GtkWidget *delete_button = gtk_button_new ();
  gtk_button_set_image (GTK_BUTTON (delete_button), gtk_image_new_from_stock (MY_STOCK_LESS, GTK_ICON_SIZE_LARGE_TOOLBAR));
  gtk_button_set_image_position (GTK_BUTTON (delete_button), GTK_POS_TOP);
  gtk_button_set_label (GTK_BUTTON (delete_button), _("Remove"));
  gtk_widget_set_tooltip_text (delete_button, _("Remove selected connection or group"));
  gtk_button_set_relief (GTK_BUTTON (delete_button), GTK_RELIEF_NONE);
  gtk_box_pack_start (GTK_BOX (buttons_hbox), delete_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (delete_button), "clicked", G_CALLBACK (delete_button_clicked_cb), tree_view);

  GtkWidget *edit_button = gtk_button_new ();
  gtk_button_set_image (GTK_BUTTON (edit_button), gtk_image_new_from_stock (MY_STOCK_PENCIL, GTK_ICON_SIZE_LARGE_TOOLBAR));
  gtk_button_set_image_position (GTK_BUTTON (edit_button), GTK_POS_TOP);
  gtk_button_set_label (GTK_BUTTON (edit_button), "Edit");
  gtk_button_set_relief (GTK_BUTTON (edit_button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text (edit_button, _("Edit selected connection or group"));
  gtk_box_pack_start (GTK_BOX (buttons_hbox), edit_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (edit_button), "clicked", G_CALLBACK (edit_button_clicked_cb), tree_view);

  GtkWidget *duplicate_connection_button = gtk_button_new ();
  gtk_button_set_image (GTK_BUTTON (duplicate_connection_button), gtk_image_new_from_stock (MY_STOCK_COPY, GTK_ICON_SIZE_LARGE_TOOLBAR));
  gtk_button_set_image_position (GTK_BUTTON (duplicate_connection_button), GTK_POS_TOP);
  gtk_button_set_label (GTK_BUTTON (duplicate_connection_button), "Duplicate");
  gtk_button_set_relief (GTK_BUTTON (duplicate_connection_button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text (duplicate_connection_button, _("Duplicate selected connection"));
  gtk_box_pack_start (GTK_BOX (buttons_hbox), duplicate_connection_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (duplicate_connection_button), "clicked", G_CALLBACK (duplicate_connection_button_clicked_cb), tree_view);

  GtkWidget *new_folder_button = gtk_button_new ();
  gtk_button_set_image (GTK_BUTTON (new_folder_button), gtk_image_new_from_stock (MY_STOCK_FOLDER, GTK_ICON_SIZE_LARGE_TOOLBAR));
  gtk_button_set_image_position (GTK_BUTTON (new_folder_button), GTK_POS_TOP);
  gtk_button_set_label (GTK_BUTTON (new_folder_button), "Folder");
  gtk_button_set_relief (GTK_BUTTON (new_folder_button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text (new_folder_button, _("Create a new folder"));
  gtk_box_pack_start (GTK_BOX (buttons_hbox), new_folder_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (new_folder_button), "clicked", G_CALLBACK (new_folder_button_clicked_cb), tree_view);


  //gtk_box_pack_start (gtk_dialog_get_content_area (GTK_DIALOG (connections_dialog)), buttons_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX(dialog_vbox), buttons_hbox, FALSE, FALSE, 0);

/*
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (connections_dialog)), GTK_WINDOW (main_window));
  gtk_box_set_spacing (gtk_dialog_get_content_area (GTK_DIALOG (connections_dialog)), 10);
  gtk_container_set_border_width (GTK_CONTAINER (connections_dialog), 5);
*/
  g_signal_connect (tree_view, "cursor-changed", G_CALLBACK (cursor_changed_cb), select);
  g_signal_connect (tree_view, "row-activated", G_CALLBACK (row_activated_cb), NULL /*connections_dialog*/);
  //g_signal_connect (connections_dialog, "key-press-event", G_CALLBACK (conn_key_press_cb), NULL);
  //gtk_dialog_set_default_response (GTK_DIALOG (connections_dialog), GTK_RESPONSE_OK);

  //gtk_box_pack_start (gtk_dialog_get_content_area (GTK_DIALOG (connections_dialog)), buttons_hbox, TRUE, TRUE, 0);
  //gtk_box_pack_start (gtk_dialog_get_content_area (GTK_DIALOG (connections_dialog)), scrolled_window, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX(dialog_vbox), scrolled_window, TRUE, TRUE, 0);

  /* combo for selecting column in interactive search */

  GtkWidget *search_by_combo = create_search_by_combo ();
  //gtk_box_pack_end (gtk_dialog_get_content_area (GTK_DIALOG (connections_dialog)), search_by_combo, TRUE, FALSE, 0);

/*
#if (GTK_MAJOR_VERSION == 2)
  g_signal_connect (G_OBJECT (GTK_COMBO_BOX (search_by_combo)), "changed", G_CALLBACK (change_search_by_cb), tree_view);
#else
  g_signal_connect (GTK_COMBO_BOX (search_by_combo), "changed", G_CALLBACK (change_search_by_cb), tree_view);
#endif
*/

  g_signal_connect (GTK_COMBO_BOX (search_by_combo), "changed", G_CALLBACK (change_search_by_cb), tree_view);


  /* standard buttons */

#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *std_buttons_hbox = gtk_hbox_new (TRUE, 5); 
#else
  GtkWidget *std_buttons_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
#endif

  //gtk_box_set_homogeneous (GTK_BOX (std_buttons_hbox), TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (std_buttons_hbox), 10);

/*
  GtkWidget *std_table = gtk_table_new (1, 2, TRUE);
  gtk_table_set_homogeneous (GTK_TABLE (std_table), TRUE);
  gtk_table_set_row_spacings (GTK_TABLE (std_table), 20);
  gtk_table_set_col_spacings (GTK_TABLE (std_table), 10);
*/

  GtkWidget *cancel_button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
  gtk_box_pack_end (GTK_BOX (std_buttons_hbox), cancel_button, TRUE, TRUE, 0);
  //gtk_table_attach (GTK_TABLE (std_table), cancel_button, 1, 2, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
  g_signal_connect (G_OBJECT (cancel_button), "clicked", G_CALLBACK (dialog_delete_event_cb), NULL);

  GtkWidget *ok_button = gtk_button_new_from_stock (GTK_STOCK_CONNECT);
  gtk_box_pack_end (GTK_BOX (std_buttons_hbox), ok_button, TRUE, TRUE, 0);
  //gtk_table_attach (GTK_TABLE (std_table), ok_button, 0, 1, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
  g_signal_connect (G_OBJECT (ok_button), "clicked", G_CALLBACK (ok_button_clicked_cb), NULL);

  GtkWidget *b_align = gtk_alignment_new (1, 0, 0, 0);
  gtk_container_add (GTK_CONTAINER (b_align), std_buttons_hbox);
  //gtk_widget_show (l_align);

  //gtk_box_pack_end (dialog_vbox, std_buttons_hbox, FALSE, FALSE, 0);
#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *sep = gtk_hseparator_new ();
#else
  GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
#endif

  gtk_box_pack_end (GTK_BOX(dialog_vbox), b_align, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX(dialog_vbox), sep, FALSE, FALSE, 10);
  gtk_box_pack_end (GTK_BOX(dialog_vbox), search_by_combo, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (dialog_window), dialog_vbox);
  gtk_widget_show_all (dialog_vbox);

  gint w_width, w_height;
  screen = gtk_window_get_screen (GTK_WINDOW (main_window));
  gtk_window_get_size (GTK_WINDOW (dialog_window), &w_width, &w_height);
  gtk_widget_set_size_request (GTK_WIDGET (dialog_window), w_width+100, gdk_screen_get_height (screen)/2);
  
  gtk_window_set_position (GTK_WINDOW (dialog_window), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_widget_show_all (dialog_window);

  //gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (connections_dialog)));
  //gtk_widget_grab_focus (tree_view);

  /* select the first row */

  if (cl_count (&conn_list))
    move_cursor_to_node (GTK_TREE_VIEW (tree_view), group_node_find_by_numeric_path (&g_groups.root, "0", 1));

  /* create a new accelerator to close window pressing ESC key */

  int keyEsc;

#if (GTK_MAJOR_VERSION == 2)
  keyEsc = GDK_Escape;
#else
  keyEsc = GDK_KEY_Escape;
#endif

  closure = g_cclosure_new (G_CALLBACK (dialog_delete_event_cb), NULL, NULL);
  gtk_accel_group_connect (gtk_accel, keyEsc, 0, GTK_ACCEL_VISIBLE, closure);
  g_closure_unref (closure);
  gtk_window_add_accel_group (GTK_WINDOW (dialog_window), gtk_accel);

  /* init the pointer to current selected node */

  g_selected_node = NULL;
  g_dialog_connections_running = 1;
  g_dialog_connections_connect = 0;

  while (g_dialog_connections_running)
    {
      lterm_iteration ();

      if (g_dialog_connections_connect)
        {
          memset (p_conn, 0x00, sizeof (struct Connection));
          is_selection_selected = get_selected_connection (select, p_conn);

          if (is_selection_selected)
            { 
              retcode = 0;
              break;
            }
          else
            g_dialog_connections_connect = 0;
        }

    } /* while */

  gtk_widget_destroy (dialog_window);
  connections_dialog = NULL;

  /* reenable DND signals for QLW */

  g_signal_handler_unblock (g_quick_launch_window.tree_model, g_quick_launch_window.row_inserted_handler);
  g_signal_handler_unblock (g_quick_launch_window.tree_model, g_quick_launch_window.row_deleted_handler);

  /* refresh list for search entry completion */
  refresh_search_completion ();

  /* refresh quick launch window expansders */
  ifr_add (ITERATION_REFRESH_QUICK_LAUCH_TREE_VIEW, NULL);
   
  return (retcode);
}


/**
 * create_quick_launch_window()
 * @param[out] p_conn new or updated connection
 * @return pointer to the window widget
 */
void
create_quick_launch_window (struct QuickLaunchWindow *p_qlv)
{
  GtkTreeSelection *select;

  /* create a tree view for connections */

  p_qlv->tree_view = create_connections_tree_view ();
  p_qlv->tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (p_qlv->tree_view));
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (p_qlv->tree_view));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (p_qlv->tree_view), FALSE);

  /* remove protocol and port column */

  gtk_tree_view_remove_column (GTK_TREE_VIEW (p_qlv->tree_view), gtk_tree_view_get_column (GTK_TREE_VIEW (p_qlv->tree_view), 2));
  gtk_tree_view_remove_column (GTK_TREE_VIEW (p_qlv->tree_view), gtk_tree_view_get_column (GTK_TREE_VIEW (p_qlv->tree_view), 2));
/*
#if (GTK_MAJOR_VERSION == 2)
  g_signal_connect (GTK_OBJECT (p_qlv->tree_view), "cursor-changed", GTK_SIGNAL_FUNC (cursor_changed_cb), select);
#else
  g_signal_connect (p_qlv->tree_view, "cursor-changed", G_CALLBACK (cursor_changed_cb), select);
#endif
*/

  g_signal_connect (p_qlv->tree_view, "cursor-changed", G_CALLBACK (cursor_changed_cb), select);
  p_qlv->row_inserted_handler = g_signal_connect (p_qlv->tree_model, "row-inserted", G_CALLBACK (on_drag_data_inserted), GTK_TREE_VIEW (p_qlv->tree_view));
  p_qlv->row_deleted_handler = g_signal_connect (p_qlv->tree_model, "row-deleted", G_CALLBACK (on_drag_data_deleted), GTK_TREE_VIEW (p_qlv->tree_view));

  gtk_widget_show_all (p_qlv->tree_view);

  PangoFontDescription *font_desc;

  font_desc = pango_font_description_from_string (prefs.font_quick_launch_window);
  gtk_widget_modify_font (p_qlv->tree_view, font_desc);
  pango_font_description_free (font_desc);

/*
  font_desc = pango_font_description_copy (vte_terminal_get_font (terminal));
  newsize = (pango_font_description_get_size (font_desc) / PANGO_SCALE) - 1;
  pango_font_description_set_size (font_desc, CLAMP (newsize, 4, 144) * PANGO_SCALE);
*/

  g_signal_connect (G_OBJECT (p_qlv->tree_view), "row-activated", G_CALLBACK (row_activated_quick_cb), select);
  //g_signal_connect (GTK_OBJECT (dialog), "key-press-event", GTK_SIGNAL_FUNC (conn_key_press_cb), NULL);

  //refresh_connection_tree_view (GTK_TREE_VIEW (p_qlv->tree_view));
  //rebuild_tree_store ();
  expand_connection_tree_view_groups (GTK_TREE_VIEW (p_qlv->tree_view), &g_groups.root);

  /* create a new scrolled window, with scrollbars only if needed, and put the tree view inside */

  p_qlv->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (p_qlv->scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (p_qlv->scrolled_window), GTK_SHADOW_ETCHED_IN);
  //gtk_container_set_resize_mode (GTK_SCROLLED_WINDOW (scrolled_window), GTK_RESIZE_IMMEDIATE);
  gtk_container_add (GTK_CONTAINER (p_qlv->scrolled_window), p_qlv->tree_view);
  gtk_widget_show_all (p_qlv->scrolled_window);

  /* combo for selecting column in interactive search */

  p_qlv->search_by_combo = create_search_by_combo ();
  g_signal_connect (G_OBJECT (GTK_COMBO_BOX (p_qlv->search_by_combo)), "changed", G_CALLBACK (change_search_by_cb), p_qlv->tree_view);

  /* button for copying host into clipboard */

  p_qlv->copy_button = gtk_button_new_with_label ("Copy host to clipboard");
  g_signal_connect (G_OBJECT (p_qlv->copy_button), "clicked", G_CALLBACK (copy_button_clicked_cb), select);

  /* main vertical box */
  
#if (GTK_MAJOR_VERSION == 2)
  p_qlv->vbox = gtk_vbox_new (FALSE, 5);
#else
  p_qlv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif

  gtk_box_pack_start (GTK_BOX (p_qlv->vbox), p_qlv->scrolled_window, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (p_qlv->vbox), p_qlv->copy_button, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (p_qlv->vbox), p_qlv->search_by_combo, FALSE, FALSE, 0);

  //return (vbox);
}

int
connection_import_file_chooser (char *title, char *outFilename)
{
  GtkWidget *dialog;
  gint result;
  char *filename;
  int err = 0;
  struct Connection *p_conn;
  
  dialog = gtk_file_chooser_dialog_new (title, GTK_WINDOW (main_window), GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                        NULL);
                                         
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), globals.home_dir);
  
  result = gtk_dialog_run (GTK_DIALOG (dialog));
  
  if (result == GTK_RESPONSE_ACCEPT)
    {
      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      
      log_debug ("filename : %s\n", filename);
      
      strcpy (outFilename, filename);

      g_free (filename);
    } 
  
  gtk_widget_destroy (dialog);
    
  return (result);
}

void
connection_import_lterm ()
{
  int result, err = 0;
  char filename[512];

  result = connection_import_file_chooser (_("Import from lterm"), filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      if (is_xml_file (filename))
        {
          log_debug ("XML file\n");
          
          err = load_connections_from_file_xml (filename);
        }
      else
        {
          msgbox_error (_("Unable to import %s\nWrong file format"), filename);
          err = 1;
        }
    }
        
  if (result == GTK_RESPONSE_ACCEPT)
    {
      if (err)
        msgbox_error ("Unable to load %s", filename);
      else
        {
          rebuild_tree_store ();
          expand_connection_tree_view_groups (GTK_TREE_VIEW (g_quick_launch_window.tree_view), &g_groups.root);

          msgbox_info ("Import completed");
        }
    }
}

void
connection_import_MobaXterm ()
{
  int result, err = 0;
  char filename[512], line[4096], SubRep[2048];
  FILE *fp;
  struct GroupNode *pFolderNode, *pNode;
  
  result = connection_import_file_chooser (_("Import from MobaXterm"), filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      fp = fopen (filename, "r");
      
      if (fp == NULL) {
        msgbox_error ("Unable to load %s", filename);
        return;
      }

      cl_release (&conn_list);
      cl_init (&conn_list);

      group_tree_release (&g_groups);
      group_tree_init (&g_groups);

      while (fgets (line, 4096, fp) != 0) {
        rtrim (line);
        
        if (line[0] == 0)
          continue;
        
        if (!memcmp (line, "[Bookmarks", 10)) {
          strcpy (SubRep, "");
          pFolderNode = &g_groups.root;
          continue;
        }
        
        if (!memcmp (line, "ImgNum=", 7))
          continue;
        
        if (!memcmp (line, "SubRep=", 7)) {
          sscanf (line, "SubRep=%s", SubRep);
          log_debug ("SubRep = %s\n", SubRep);
          
          if (SubRep[0] != 0) {
            int nFolders, i;
            
            // Go to right folder or create all path
            char **folders =
              splitString (SubRep, "\\", 1, 0, 0, &nFolders);
            
            for (i=0; i<nFolders; i++) {
              //log_debug ("  %s\n", folders[i]);
              pNode = group_node_find_child (pFolderNode, folders[i]);
              
              if (pNode == NULL)
                pFolderNode = group_node_add_child (pFolderNode, GN_TYPE_FOLDER, folders[i]);
              else
                pFolderNode = pNode;
            }
          }
            
          continue;
        }
          
        // If none of the above, then it's a bookmark and pFolderNode is the right folder
        
        struct Connection conn, *pConn;
        char protocolId[8], portStr[8];
        const int sshId = '0';
        const int telnetId = '1';
        
        memset (&conn, 0, sizeof (struct Connection));
        
        //Datalayer=#109#0%10.104.101.126%22%% %-1%-1% %%22%%0%-1%Interactive shell%
        
        list_get_nth (line, 1, '=', conn.name);
        list_get_nth (line, 3, '#', protocolId);
        list_get_nth (line, 2, '%', conn.host);
        list_get_nth (line, 3, '%', portStr);
        
        log_debug ("%s %c %s %s\n", conn.name, protocolId[0], conn.host, portStr);
        
        conn.port = atoi (portStr);
        
        if (protocolId[0] == telnetId)
          strcpy (conn.protocol, "telnet");
        else
          strcpy (conn.protocol, "ssh");   
          
        pNode = group_node_add_child (pFolderNode, GN_TYPE_CONNECTION, conn.name);
        pConn = cl_insert_sorted (&conn_list, &conn);
      } // While
    
    fclose (fp);
  } // Accept
      
  rebuild_tree_store ();
}

void
connection_import_Putty ()
{
  int result, err = 0, n = 0;
  char filename[512], line[4096];
  char *header = "[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\";
  struct Connection conn, *pConn;
  FILE *fp;

  result = connection_import_file_chooser (_("Import from Putty"), filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      fp = fopen (filename, "r");
      
      if (fp == NULL) {
        msgbox_error ("Unable to load %s", filename);
        return;
      }

      cl_release (&conn_list);
      cl_init (&conn_list);

      group_tree_release (&g_groups);
      group_tree_init (&g_groups);

      while (fgets (line, 4096, fp) != 0) {
        
        //log_debug ("%d %s\n", strlen(line), line);
        rtrim (line);
        
        if (line[0] == 0)
          continue;
        
        if (strstr (line, header)) {
          // A new connection begins, save the previous (if any)
          if (n > 0) {
            log_debug ("Adding %s %s %s %d\n", conn.name, conn.protocol, conn.host, conn.port);
      
            group_node_add_child (&g_groups.root, GN_TYPE_CONNECTION, conn.name);
            cl_insert_sorted (&conn_list, &conn);
          }
          
          n ++;
          memset (&conn, 0, sizeof (struct Connection));
          
          // Get connection name
          char *pc, *name = &line[strlen(header)];
          pc = (char *) strchr (name, ']');
          
          if (pc)
            *pc = 0;
            
          strcpy (conn.name, name);
          
          log_debug ("Found %s\n", conn.name);
          continue;
        }
        
        if (!memcmp (line, "\"HostName\"=", 11)) {
          sscanf (line, "\"HostName\"=\"%s\"", conn.host);
          conn.host[strlen(conn.host)-1] = 0; // Delete trailing "
          continue;
        }
        
        if (!memcmp (line, "\"Protocol\"=", 11)) {
          sscanf (line, "\"Protocol\"=\"%s\"", conn.protocol);
          conn.protocol[strlen(conn.protocol)-1] = 0; // Delete trailing "
          continue;
        }
        
        if (!memcmp (line, "\"PortNumber\"=dword:", 19)) {
          char portStr[32];//, const char *hexstring = "abcdef0";
          sscanf (line, "\"PortNumber\"=dword:%s", portStr);
          conn.port = (int) strtol (portStr, NULL, 16);
          continue;
        }
      } // While
    
      fclose (fp);
    } // Accept
    
  rebuild_tree_store ();
}

gint
connection_export_file_chooser (char *title, 
                                char *currentName, 
                                char *outFilename)
{
  GtkWidget *dialog;
  gint result;
  char *filename;
  
  dialog = gtk_file_chooser_dialog_new (title, GTK_WINDOW (main_window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                        NULL);
                                        
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);  
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), globals.home_dir);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), currentName);
  
  result = gtk_dialog_run (GTK_DIALOG (dialog));
  
  if (result == GTK_RESPONSE_ACCEPT)
    {
      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      
      log_debug ("%s\n", filename);

      strcpy (outFilename, filename);
      
      g_free (filename);
    } 
  
  gtk_widget_destroy (dialog);

  return (result);
}

void
connection_export_lterm ()
{
  int result;
  char filename[512];

  result = connection_export_file_chooser (_("Export to lterm"), "lterm-bookmarks.xml", filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      if (save_connections_to_file_xml (filename))
        msgbox_error ("Unable to save %s", filename);
    } 
}

int mxtBookmarksCount;
char *mainFolder = "lterm-bookmarks";

void
mobaXterm_write_node (FILE *fp, struct GroupNode *pNode, char *SubRep)
{
  int i, protocolId, imgId;
  struct Connection *p_conn;
  struct Protocol *p_prot;
  char header[32], line[4096];
  const int sshId = 0;
  const int telnetId = 1;

  if (pNode == &g_groups.root) {
    sprintf (header, "[Bookmarks]");
  }
  else {
    sprintf (header, "[Bookmarks_%d]", ++mxtBookmarksCount);
  }

  fprintf (fp, "%s\n", header);
  fprintf (fp, "SubRep=%s\n", SubRep);
  fprintf (fp, "ImgNum=41\n");

  // Connections
  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (pNode->child[i])
        {
          if (pNode->child[i]->type == GN_TYPE_CONNECTION)
            {
              p_conn = cl_get_by_name (&conn_list, pNode->child[i]->name);
  
              p_prot = get_protocol (&g_prot_list, p_conn->protocol);
              
              if (p_prot)
                protocolId = p_prot->type == PROT_TYPE_TELNET ? telnetId : sshId;
              else
                protocolId = sshId;
                
              imgId = protocolId == telnetId ? 98 : 109;
              
              sprintf (line, "%s=#%d#%d%%%s%%%d%%%% %%-1%%-1%% %%%%%d%%%%0%%-1%%Interactive shell%%", 
                       p_conn->name, 
                       imgId,
                       protocolId,
                       p_conn->host,
                       p_conn->port,
                       p_conn->port);
              fprintf (fp, "%s\n", line);
                       
            }
        }
    }
    
  fprintf (fp, "\n");
    
  // Subfolders
  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (pNode->child[i])
        {
          if (pNode->child[i]->type == GN_TYPE_FOLDER)
            {
              char newSubRep[4096];
              sprintf (newSubRep, "%s\\%s", SubRep, pNode->child[i]->name);
              mobaXterm_write_node (fp, pNode->child[i], newSubRep);
            }
        }
    }
}

void
connection_export_MobaXterm ()
{
  int result;
  char filename[512];
  FILE *fp;

  result = connection_export_file_chooser (_("Export to MobaXterm"), "lterm-bookmarks.mxtsessions", filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      fp = fopen (filename, "w");

      if (fp == NULL) {
        msgbox_error ("Unable to save %s", filename);
	      return;
      }

      mxtBookmarksCount = 0;
      mobaXterm_write_node (fp, &g_groups.root, mainFolder);

      fclose (fp);
    } 
}

void
connection_export_Putty ()
{
  int result;
  char filename[512], templateFilename[512], *template, *connectionEntry;
  FILE *fp;

  result = connection_export_file_chooser (_("Export to Putty"), "lterm-bookmarks.reg", filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      // Load template for a single connection
      sprintf (templateFilename, "%s/PuttyExportTemplate.txt", globals.data_dir);
      template = readFile (templateFilename);
      
      if (template == NULL) {
        msgbox_error ("Unable to load Putty export template %s", templateFilename);
	      return;
      }
      
      // Allocate string for actual connetion
      connectionEntry = (char *) malloc (strlen (template) + 1024);

      // Open output file
      fp = fopen (filename, "w");

      if (fp == NULL) {
        msgbox_error ("Unable to save %s", filename);
	      return;
      }
      
      fprintf (fp, "Windows Registry Editor Version 5.00\n\n");
      
      struct Connection *p_conn = conn_list.head;

      while (p_conn)
        {
          strcpy (connectionEntry, template);
          
          char *connectionEntryTmp = replace_str (connectionEntry, "{name}", p_conn->name);
          strcpy (connectionEntry, connectionEntryTmp);
          free (connectionEntryTmp);

          connectionEntryTmp = replace_str (connectionEntry, "{ip}", p_conn->host);
          strcpy (connectionEntry, connectionEntryTmp);
          free (connectionEntryTmp);

          char portTmp[32];
          sprintf (portTmp, "%08x", p_conn->port);
          connectionEntryTmp = replace_str (connectionEntry, "{port}", portTmp);
          strcpy (connectionEntry, connectionEntryTmp);
          free (connectionEntryTmp);

          connectionEntryTmp = replace_str (connectionEntry, "{protocol}", p_conn->protocol);
          strcpy (connectionEntry, connectionEntryTmp);
          free (connectionEntryTmp);
          
          fprintf (fp, "%s\n\n", connectionEntry);
          
          p_conn = p_conn->next;
        }

      fclose (fp);
      free (connectionEntry);
    } 
}


void
connection_export_CSV ()
{
  int result;
  char filename[512], templateFilename[512], *template, *connectionEntry;
  FILE *fp;

  result = connection_export_file_chooser (_("Export to CSV"), "lterm-bookmarks.csv", filename);

  if (result == GTK_RESPONSE_ACCEPT)
    {
      // Open output file
      fp = fopen (filename, "w");

      if (fp == NULL) {
        msgbox_error ("Unable to save %s", filename);
	      return;
      }
      
      struct Connection *p_conn = conn_list.head;

      while (p_conn)
        {
          fprintf (fp, "%s,%s,%s,%d\n", p_conn->name, p_conn->host, p_conn->protocol, p_conn->port);
          
          p_conn = p_conn->next;
        }

      fclose (fp);
      free (connectionEntry);
    } 
}


