
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
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"
#include "protocol.h"
#include "connection.h"
#include "profile.h"
#include "utils.h"
#include "xml.h"
#include "main.h"

extern GtkWidget *main_window;

GtkWidget *protocol_combo = 0;
GtkWidget *command_entry;
GtkWidget *arguments_entry;
GtkWidget *port_entry;
GtkWidget *askuser_check;
GtkWidget *askpassword_check;
GtkWidget *disconnectclose_check;
GtkWidget *label_status;

int response_new = 10;

int modified = 0;

/* semaphores */
int refreshing = 0;     /* refreshing combo box */
int status_enabled = 1; /* change status */

void
pl_init (struct Protocol_List *p_pl)
{
  p_pl->head = 0;
  p_pl->tail = 0;
}

void
prot_release_chain (struct Protocol *p_head)
{
  if (p_head)
    {
      prot_release_chain (p_head->next);
      free (p_head);
    }
}

void
pl_release (struct Protocol_List *p_pl)
{
  prot_release_chain (p_pl->head);

  p_pl->head = 0;
  p_pl->tail = 0;
}

void
pl_append (struct Protocol_List *p_pl, struct Protocol *p_new)
{
  struct Protocol *p_new_decl;

  p_new_decl = (struct Protocol *) malloc (sizeof (struct Protocol));

  memset (p_new_decl, 0, sizeof (struct Protocol));
  memcpy (p_new_decl, p_new, sizeof (struct Protocol));

  if (!strcmp (p_new_decl->command, "telnet"))
    p_new_decl->type = PROT_TYPE_TELNET;
  else if (!strcmp (p_new_decl->command, "ssh"))
    p_new_decl->type = PROT_TYPE_SSH;
  else if (!strcmp (p_new_decl->command, "smbclient"))
    p_new_decl->type = PROT_TYPE_SAMBA;
  else
    p_new_decl->type = PROT_TYPE_OTHER;

  p_new_decl->next = 0;

  if (p_pl->head == 0)
    {
      p_pl->head = p_new_decl;
      p_pl->tail = p_new_decl;
    }
  else
    {
      p_pl->tail->next = p_new_decl;
      //p_new_decl->prev = p_sd->tail;
      p_pl->tail = p_new_decl;
    }
}

void
pl_prepend (struct Protocol_List *p_pl, struct Protocol *p_new)
{
  struct Protocol *p_new_decl;

  p_new_decl = (struct Protocol *) malloc (sizeof (struct Protocol));

  memset (p_new_decl, 0, sizeof (struct Protocol));
  memcpy (p_new_decl, p_new, sizeof (struct Protocol));

  p_new_decl->next = p_pl->head;
  p_pl->head = p_new_decl;
}

void
pl_remove (struct Protocol_List *p_pl, char *name)
{
  struct Protocol *p_del, *p_prec;

  p_prec = 0;
  p_del = p_pl->head;

#ifdef DEBUG
  printf ("pl_remove() : to be removed %s\n", name);
#endif

  while (p_del)
    {
      if (!strcmp (p_del->name, name))
        {
          if (p_prec)
            p_prec->next = p_del->next;
          else
            p_pl->head = p_del->next;

          if (p_pl->tail == p_del)
            p_pl->tail = p_prec;

#ifdef DEBUG
  printf ("pl_remove() : removing %s ...\n", p_del->name);
#endif
          free (p_del);

#ifdef DEBUG
  printf ("pl_remove() : removed %s\n", name);
#endif

          break;
        }
 
      p_prec = p_del;
      p_del = p_del->next;
    }
}

int
pl_count (struct Protocol_List *p_pl)
{
  struct Protocol *p;
  int n = 0;

  p = p_pl->head;

  while (p)
    {
      n ++;
      p = p->next;
    }
    
  return (n);
}

void
pl_dump (struct Protocol_List *p_pl)
{
  struct Protocol *p;

  p = p_pl->head;

  while (p)
    {
      printf ("%s : %s:%d %s\n", p->name, p->command, p->port, p->args);
      p = p->next;
    }
}
 
/**
 * check_standard_protocols() - add a standard protocol if not loaded
 */
void
check_standard_protocols (struct Protocol_List *p_pl)
{
  int n, i;

  struct Protocol std_protocosls[] =
    {
      { "telnet", PROT_TYPE_TELNET, "telnet", "%h %p", 23, PROT_FLAG_ASKUSER|PROT_FLAG_ASKPASSWORD, NULL }, 
      { "ssh", PROT_TYPE_SSH, "ssh", "-p %p -l %u %h", 22, PROT_FLAG_ASKPASSWORD, NULL },
      { "samba", PROT_TYPE_SAMBA, "smbclient", "//%h/%d -U %u%%%P", -1, PROT_FLAG_NO, NULL }
    };

  n = sizeof (std_protocosls) / sizeof (struct Protocol);

  for (i=0; i<n; i++)
    {
      if (!get_protocol (p_pl, std_protocosls[i].name))
        pl_append (p_pl, &std_protocosls[i]);
    }
}

int
load_protocols_from_file_xml (char *filename, struct Protocol_List *p_pl)
{
  int rc = 0;
  char *xml;
  char line[2048];
  char tmp_s[32], *pc;
  FILE *fp;
  struct Protocol protocol;
  int n_loaded = 0;

  /* put xml content into a string */
  
  fp = fopen (filename, "r");
  
  if (fp == NULL)
    return (0);
  
  xml = (char *) malloc (2048);
  strcpy (xml, "");
  
  while (fgets (line, 1024, fp) != 0)
    {
      if (strlen (xml) + strlen (line) > sizeof (xml))
        xml = (char *) realloc (xml, strlen (xml) + strlen (line) + 1);
      
      strcat (xml, line);
    }
    
  fclose (fp);

  /* parse the xml document */
  
  XML xmldoc;
  XMLNode *node, *child, *node_2;

  xml_parse (xml, &xmldoc);

  if (xmldoc.error.code)
    {
      log_write ("[%s] error: %s\n", __func__, xmldoc.error.message);
      return 0;
    } 

  if (strcmp (xmldoc.cur_root->name, "lterm-protocols"))
    {
      log_write ("[%s] can't find root node: lterm-protocols\n", __func__);
      return 0;
    }

  node = xmldoc.cur_root->children;
 
  while (node)
    {
      if (!strcmp (node->name, "protocol"))
        {
          memset (&protocol, 0, sizeof (struct Protocol));

          strcpy (protocol.name, xml_node_get_attribute (node, "name"));

          if (child = xml_node_get_child (node, "command"))
            strcpy (protocol.command, NVL(xml_node_get_value (child), ""));

          if (child = xml_node_get_child (node, "port"))
            {
              strcpy (tmp_s, NVL(xml_node_get_value (child), ""));
              if (tmp_s[0])
                protocol.port = atoi (tmp_s);
            }

          if (child = xml_node_get_child (node, "arguments"))
            strcpy (protocol.args, NVL(xml_node_get_value (child), ""));

          if (node_2 = xml_node_get_child (node, "flags"))
            {     
              if (child = xml_node_get_child (node_2, "askuser"))
                {
                  strcpy (tmp_s, NVL(xml_node_get_attribute (child, "value"), "0"));

                  if (tmp_s[0] != '0')
                    protocol.flags |= PROT_FLAG_ASKUSER;
                }

              if (child = xml_node_get_child (node_2, "askpassword"))
                {
                  strcpy (tmp_s, NVL(xml_node_get_attribute (child, "value"), "0"));

                  if (tmp_s[0] != '0')
                    protocol.flags |= PROT_FLAG_ASKPASSWORD;
                }

              if (child = xml_node_get_child (node_2, "disconnectclose"))
                {
                  strcpy (tmp_s, NVL(xml_node_get_attribute (child, "value"), "0"));

                  if (tmp_s[0] != '0')
                    protocol.flags |= PROT_FLAG_DISCONNECTCLOSE;
                }
            }
            
          pl_append (p_pl, &protocol);
          n_loaded ++;
        }
        
      node = node->next;
    }

  xml_free (&xmldoc);
  free (xml);

  return (n_loaded);
}

int
save_protocols_to_file_xml (char *filename, struct Protocol_List *p_pl)
{
  FILE *fp;
  struct Protocol *p;
  char line[1024];
  char s_flags[1024];
  int n_saved = 0;

  fp = fopen (filename, "w");

  if (fp == NULL) 
    return 0; 

  fprintf (fp, 
           "<?xml version = '1.0'?>\n"
           "<!DOCTYPE lterm-protocols>\n"
           "<lterm-protocols>\n");  

  p = p_pl->head;

  while (p)
    {
      fprintf (fp, "  <protocol name='%s'>\n"
                   "    <command>%s</command>\n"
                   "    <port>%d</port>\n"
                   "    <arguments>%s</arguments>\n"
                   "    <flags>\n"
                   "      <askuser value='%d'/>\n"
                   "      <askpassword value='%d'/>\n"
                   "      <disconnectclose value='%d'/>\n"
                   "    </flags>\n"
                   "  </protocol>\n",
                   p->name, p->command, p->port, p->args,
                   (p->flags & PROT_FLAG_ASKUSER) != 0,
                   (p->flags & PROT_FLAG_ASKPASSWORD) != 0,
                   (p->flags & PROT_FLAG_DISCONNECTCLOSE) != 0);

      n_saved ++;
      p = p->next;
    }

  fprintf (fp, "</lterm-protocols>\n");
  fclose (fp);
 
  return (n_saved);
}

struct Protocol *
get_protocol (struct Protocol_List *p_pl, char *name)
{
  struct Protocol *p;

  p = p_pl->head;

  while (p)
    {
      if (!strcmp (p->name, name))
        return (p);
 
      p = p->next;
    }

  return 0;
}

void
fill_protocol_entries (char *p_name, struct Protocol_List *p_pl)
{
  struct Protocol *p_prot;  
  char s_port[256];

  p_prot = get_protocol (p_pl, p_name);

  if (p_prot)
    {
      status_enabled = 0;
      
      gtk_entry_set_text (GTK_ENTRY (command_entry), p_prot->command);
      gtk_entry_set_text (GTK_ENTRY (arguments_entry), p_prot->args);

      sprintf (s_port, "%d", p_prot->port);
      gtk_entry_set_text (GTK_ENTRY (port_entry), s_port);
      
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (askuser_check), p_prot->flags & PROT_FLAG_ASKUSER);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (askpassword_check), p_prot->flags & PROT_FLAG_ASKPASSWORD);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (disconnectclose_check), p_prot->flags & PROT_FLAG_DISCONNECTCLOSE);
      
      status_enabled = 1;
    }
}

static void
change_edit_protocol_cb (GtkWidget *entry, gpointer user_data)
{
  struct Protocol_List *p_pl;

  if (refreshing) return;

  p_pl = (struct Protocol_List *) user_data;

#if (GTK_MAJOR_VERSION == 2)
  fill_protocol_entries ((char *) gtk_combo_box_get_active_text (GTK_COMBO_BOX (entry)), p_pl);
#else
  fill_protocol_entries ((char *) gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (entry)), p_pl);
#endif
}

int
query_name_new_protocol (char *buffer, struct Protocol_List *p_pl)
{
  int ret;
  GtkWidget *dialog;
  GtkWidget *p_label;
  GtkWidget *user_entry;
  GtkWidget *l_align;


  dialog = gtk_dialog_new_with_buttons ("New protocol", GTK_WINDOW(main_window), 0,
                                        GTK_STOCK_CANCEL,
                                        GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OK,
                                        GTK_RESPONSE_OK,
                                        NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  user_entry = gtk_entry_new ();
  gtk_entry_set_max_length (GTK_ENTRY(user_entry), 30);
  gtk_entry_set_activates_default (GTK_ENTRY (user_entry), TRUE);

  p_label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (p_label), "Enter a name for protocol");

  l_align = gtk_alignment_new (0, 0, 0, 0);
  gtk_container_add (GTK_CONTAINER (l_align), p_label);
  gtk_widget_show (l_align);

  gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), l_align);
  gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), user_entry);

  gtk_widget_show_all (dialog);

  gtk_widget_grab_focus (user_entry);

  /* Start the dialog window */
  
  while (1)
    {
      gint result = gtk_dialog_run (GTK_DIALOG (dialog));

      strcpy (buffer, "");

      if (result == GTK_RESPONSE_OK)
        {
          strcpy (buffer, gtk_entry_get_text (GTK_ENTRY (user_entry)));
          
          if (get_protocol (p_pl, buffer))
            {
              msgbox_error (_("Protocol %s already exists"), buffer);
            }
          else
            {
              ret = strlen (buffer);
              break;
            }
        }
      else
        {
          ret = -1;
          break;
        }
    }

  gtk_widget_destroy (dialog);

  return (ret);
}

void
prot_new_clicked_cb (GtkButton *button, gpointer user_data)
{
  struct Protocol_List *p_pl;
  struct Protocol prot_new;
  
#ifdef DEBUG
  printf ("prot_new_clicked_cb()\n");
#endif

  p_pl = (struct Protocol_List *) user_data;

  memset (&prot_new, 0, sizeof (struct Protocol));
  
  if (query_name_new_protocol (prot_new.name, p_pl) != -1)
    {
#ifdef DEBUG
      printf ("prot_new_clicked_cb(): adding '%s'\n", prot_new.name);
#endif
      pl_prepend (p_pl, &prot_new);
      refresh_protocols (p_pl);
      modified = 1;
      gtk_label_set_markup (GTK_LABEL (label_status), "<span color=\"red\">modified</span>");
    }
}

void
prot_revert_clicked_cb (GtkButton *button, gpointer user_data)
{
  struct Protocol_List *p_pl;

#ifdef DEBUG
  printf ("prot_revert_clicked_cb()\n");
#endif

  p_pl = (struct Protocol_List *) user_data;

#if (GTK_MAJOR_VERSION == 2)
  fill_protocol_entries ((char *) gtk_combo_box_get_active_text (GTK_COMBO_BOX (protocol_combo)), p_pl);
#else
  fill_protocol_entries ((char *) gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (protocol_combo)), p_pl);
#endif

  modified = 0;
  gtk_label_set_markup (GTK_LABEL (label_status), "Reverted to saved version");
}

void
prot_save_clicked_cb (GtkButton *button, gpointer user_data)
{
  struct Protocol_List *p_pl;
  struct Protocol *p_prot;  
  char s_port[256];

#ifdef DEBUG
  printf ("prot_save_clicked_cb()\n");
#endif

  p_pl = (struct Protocol_List *) user_data;

#if (GTK_MAJOR_VERSION == 2)
  p_prot = get_protocol (p_pl, (char *) gtk_combo_box_get_active_text (GTK_COMBO_BOX (protocol_combo)));
#else
  p_prot = get_protocol (p_pl, (char *) gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (protocol_combo)));
#endif

  if (p_prot)
    {
      strcpy (p_prot->command, gtk_entry_get_text (GTK_ENTRY (command_entry)));
      strcpy (p_prot->args, gtk_entry_get_text (GTK_ENTRY (arguments_entry)));
      p_prot->port = atoi (gtk_entry_get_text (GTK_ENTRY (port_entry)));
/*
flags = (flags & ~(PROT_FLAG_ASKPASSWORD)) | (x << 1)
      = 0011   & 1101                  | 0010     = 
      = 0001                           | 0010     = 0011
      = 0001                           | 0000     = 0001
*/
      p_prot->flags = (p_prot->flags & ~(PROT_FLAG_ASKUSER)) | ((gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (askuser_check)) ? 1 : 0) << 0);
      p_prot->flags = (p_prot->flags & ~(PROT_FLAG_ASKPASSWORD)) | ((gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (askpassword_check)) ? 1 : 0) << 1);
      p_prot->flags = (p_prot->flags & ~(PROT_FLAG_DISCONNECTCLOSE)) | ((gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (disconnectclose_check)) ? 1 : 0) << 2);

#ifdef DEBUG
      printf ("prot_save_clicked_cb() : flags = %d\n", p_prot->flags);
#endif      
      modified = 0;
      gtk_label_set_markup (GTK_LABEL (label_status), "Saved");
    }
}

void
prot_delete_clicked_cb (GtkButton *button, gpointer user_data)
{
  struct Protocol_List *p_pl;
  char name[1024];

#ifdef DEBUG
  printf ("prot_delete_clicked_cb()\n");
#endif

#if (GTK_MAJOR_VERSION == 2)
  strcpy (name, (char *) gtk_combo_box_get_active_text (GTK_COMBO_BOX (protocol_combo)));
#else
  strcpy (name, (char *) gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(protocol_combo)));
#endif
  
  p_pl = (struct Protocol_List *) user_data;

  if (msgbox_yes_no (_("Delete protocol %s?"), name) == GTK_RESPONSE_YES)
    {
      pl_remove (p_pl, name);
      gtk_label_set_markup (GTK_LABEL (label_status), g_strconcat (name, " deleted", NULL));
      refresh_protocols (p_pl);
      modified = 0;
    }
}

void 
prot_changed_cb (GtkWidget *widget, gpointer data) 
{    
  if (!status_enabled) return;

  modified = 1;
  gtk_label_set_markup (GTK_LABEL (label_status), "<span color=\"red\">modified</span>");
}

void
cmd_check_clicked_cb (GtkButton *button, gpointer user_data)
{
  char cmd[2048];
  
#ifdef DEBUG
  printf ("cmd_check_clicked_cb()\n");
#endif

  strcpy (cmd, gtk_entry_get_text (GTK_ENTRY (command_entry)));
  
  if (check_command (cmd))
    msgbox_info (_("%s found"), cmd);
  else
    msgbox_error (_("Can't find %s"), cmd);
}

void
refresh_protocols (struct Protocol_List *p_pl)
{
  struct Protocol *p;
  int i;

  /* begin refresh */
  refreshing = 1;
  
  /* don't change status in this phase */
  status_enabled = 0;
  
  /* empty list */

#if (GTK_MAJOR_VERSION == 2)
  for (i=0; i<pl_count (p_pl)+1; i++)
    {
      gtk_combo_box_remove_text (GTK_COMBO_BOX (protocol_combo), 0);
    }
#else
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (protocol_combo));
#endif
  
  /* recreate list */

  p = p_pl->head;

  while (p)
    {
#if (GTK_MAJOR_VERSION == 2)
      gtk_combo_box_append_text (GTK_COMBO_BOX (protocol_combo), p->name);
#else
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(protocol_combo), p->name);
#endif

      p = p->next;
    }

  /* end of refresh so fields will be automatically filled by callback function */
  refreshing = 0; 
  
  gtk_combo_box_set_active (GTK_COMBO_BOX (protocol_combo), 0);
  
  /* re-enable satus changes */
  status_enabled = 1;
}

void
manage_protocols (struct Protocol_List *p_pl)
{
  GtkWidget *dialog;
  GtkWidget *cancel_button, *ok_button;
  GList *client_glist = NULL;
  struct Protocol *p;
  gint result;

#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *main_vbox = gtk_vbox_new (FALSE, 5);
  GtkWidget *edit_hbox = gtk_hbox_new (FALSE, 20);
  GtkWidget *entries_vbox = gtk_vbox_new (FALSE, 5);
  GtkWidget *buttons_vbox = gtk_vbox_new (FALSE, 5);
#else
  GtkWidget *main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);    /* main vertical box: contains everything */
  GtkWidget *edit_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 20);   /* edit horizontal box: contains entries_vbox box and buttons_vbox*/
  GtkWidget *entries_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5); /* entries vertical box: contains entry controls */
  GtkWidget *buttons_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5); /* buttons vertical box: contains buttons */
#endif

  /* protocol combo */

#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *protocol_hbox = gtk_hbox_new (FALSE, 10);
#else
  GtkWidget *protocol_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
#endif

  gtk_box_pack_start (GTK_BOX (protocol_hbox), gtk_widget_new (GTK_TYPE_LABEL, "label", _("Protocol"), "xalign", 0.0, NULL), FALSE, TRUE, 5);

#if (GTK_MAJOR_VERSION == 2)
  protocol_combo = gtk_combo_box_new_text ();
#else
  protocol_combo = gtk_combo_box_text_new_with_entry ();
#endif

  gtk_box_pack_end (GTK_BOX (protocol_hbox), protocol_combo, TRUE, TRUE, 5);
  gtk_box_pack_start (GTK_BOX (entries_vbox), protocol_hbox, FALSE, TRUE, 5);

  /* command */

  command_entry = gtk_entry_new ();
  gtk_entry_set_max_length (GTK_ENTRY(command_entry), 255);
  GtkWidget *command_hbox = create_entry_control (_("Command"), command_entry);
  gtk_box_pack_start (GTK_BOX (entries_vbox), command_hbox, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (GTK_ENTRY (command_entry)), "changed", G_CALLBACK (prot_changed_cb), NULL);

  GtkWidget *cmd_check_button = gtk_button_new_with_label ("Check");
  gtk_box_pack_start (GTK_BOX (command_hbox), cmd_check_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (cmd_check_button), "clicked", G_CALLBACK (cmd_check_clicked_cb), p_pl);

  /* arguments */

  
  arguments_entry = gtk_entry_new ();
  gtk_entry_set_max_length (GTK_ENTRY(arguments_entry), 1024);
  GtkWidget *arguments_hbox = create_entry_control (_("Arguments"), arguments_entry);
  gtk_box_pack_start (GTK_BOX (entries_vbox), arguments_hbox, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (GTK_ENTRY (arguments_entry)), "changed", G_CALLBACK (prot_changed_cb), NULL);

  /* default port */
  
  port_entry = gtk_entry_new ();
  gtk_entry_set_max_length (GTK_ENTRY(port_entry), 1024);
  GtkWidget *port_hbox = create_entry_control (_("Default port"), port_entry);
  gtk_box_pack_start (GTK_BOX (entries_vbox), port_hbox, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (GTK_ENTRY (port_entry)), "changed", G_CALLBACK (prot_changed_cb), NULL);

  /* connect signal now, after every control has been created */

  g_signal_connect (G_OBJECT (GTK_COMBO_BOX (protocol_combo)), "changed", G_CALLBACK (change_edit_protocol_cb), p_pl);

  /*
  p = p_pl->head;

  while (p)
    {
      //client_glist = g_list_append (client_glist, p->name);
      gtk_combo_box_append_text (GTK_COMBO_BOX (protocol_combo), p->name);
      p = p->next;
    }

  //gtk_combo_set_popdown_strings (GTK_COMBO (protocol_combo), client_glist);
  gtk_combo_box_set_active (GTK_COMBO_BOX (protocol_combo), 0);
  */
  
  gtk_box_pack_start (GTK_BOX (edit_hbox), entries_vbox, FALSE, FALSE, 0);
  
  askuser_check = gtk_check_button_new_with_mnemonic (_("Detect username prompt"));
  g_signal_connect (askuser_check, "toggled", G_CALLBACK (prot_changed_cb), p_pl);

  askpassword_check = gtk_check_button_new_with_mnemonic (_("Detect password prompt"));
  g_signal_connect (askpassword_check, "toggled", G_CALLBACK (prot_changed_cb), p_pl);

  disconnectclose_check = gtk_check_button_new_with_mnemonic (_("Close tab on disconnection"));
  g_signal_connect (disconnectclose_check, "toggled", G_CALLBACK (prot_changed_cb), p_pl);

  gtk_box_pack_start (GTK_BOX (entries_vbox), askuser_check, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (entries_vbox), askpassword_check, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (entries_vbox), disconnectclose_check, FALSE, FALSE, 0);

  /* buttons */

  GtkWidget *new_button = gtk_button_new_from_stock (GTK_STOCK_NEW);
  gtk_box_pack_start (GTK_BOX (buttons_vbox), new_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (new_button), "clicked", G_CALLBACK (prot_new_clicked_cb), p_pl);

  GtkWidget *save_button = gtk_button_new_from_stock (GTK_STOCK_SAVE);
  gtk_box_pack_start (GTK_BOX (buttons_vbox), save_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (save_button), "clicked", G_CALLBACK (prot_save_clicked_cb), p_pl);

  GtkWidget *revert_button = gtk_button_new_from_stock (GTK_STOCK_REVERT_TO_SAVED);
  gtk_box_pack_start (GTK_BOX (buttons_vbox), revert_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (revert_button), "clicked", G_CALLBACK (prot_revert_clicked_cb), p_pl);

  GtkWidget *delete_button = gtk_button_new_from_stock (GTK_STOCK_DELETE);
  gtk_box_pack_start (GTK_BOX (buttons_vbox), delete_button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (delete_button), "clicked", G_CALLBACK (prot_delete_clicked_cb), p_pl);

  gtk_box_pack_start (GTK_BOX (edit_hbox), buttons_vbox, TRUE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (main_vbox), edit_hbox, TRUE, FALSE, 0);

  /* status label */

  GtkWidget *status_align = gtk_alignment_new (0, 0, 0, 0);
  gtk_container_set_border_width (GTK_CONTAINER (status_align), 0);

  label_status = gtk_widget_new (GTK_TYPE_LABEL, "label", "", "justify", GTK_JUSTIFY_LEFT, NULL);
  gtk_container_add (GTK_CONTAINER (status_align), label_status);
  //gtk_widget_show (status_align);

  gtk_box_pack_start (GTK_BOX (main_vbox), status_align, TRUE, FALSE, 0);
  
  /* first refresh */

  refresh_protocols (p_pl);

  /* variables */
  
  GtkWidget *variables_frame = gtk_frame_new (_("Substitutions")); /* frame */
  gtk_container_set_border_width (GTK_CONTAINER (variables_frame), 5);
  gtk_widget_show (variables_frame);
  
#if (GTK_MAJOR_VERSION == 2)
  GtkWidget *variables_vbox = gtk_vbox_new (TRUE, 0); /* vbox */
#else
  GtkWidget *variables_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0); /* vbox */
#endif

  gtk_container_set_border_width (GTK_CONTAINER (variables_vbox), 5);
  gtk_widget_show (variables_vbox);
  gtk_container_add (GTK_CONTAINER (variables_frame), variables_vbox);
  gtk_box_pack_start (GTK_BOX (main_vbox), variables_frame, FALSE, TRUE, 5);

  GtkWidget *label_variables = gtk_widget_new (GTK_TYPE_LABEL, "label", "", "justify", GTK_JUSTIFY_LEFT, NULL);
  gtk_label_set_markup (GTK_LABEL (label_variables),
    "Following variables will be replaced with values\n"
    "of selected connection.\n\n" 
    "<i>%h</i> : Host\n"
    "<i>%p</i> : Port\n"
    "<i>%u</i> : User\n"
    "<i>%P</i> : Password\n"
    "<i>%d</i> : Directory\n"
    "<i>%%</i> : % character"
  );

  GtkWidget *variables_align = gtk_alignment_new (0, 0, 0, 0);
  gtk_container_set_border_width (GTK_CONTAINER (variables_align), 0);
  gtk_container_add (GTK_CONTAINER (variables_align), label_variables);
  gtk_widget_show (variables_align);

  gtk_box_pack_start (GTK_BOX (variables_vbox), variables_align, TRUE, FALSE, 0);

  /* create dialog */

  dialog = gtk_dialog_new ();

  gtk_window_set_title (GTK_WINDOW (dialog), "Edit protocols");
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 10);

  //cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_box_pack_start (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), main_vbox, TRUE, TRUE, 0);

  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  /* run dialog */
  
  while (1)
    {
      result = gtk_dialog_run (GTK_DIALOG (dialog));
      
      if (modified)
        {
          if (msgbox_yes_no (_("Changes have not been saved.\nClose anyway?")) == GTK_RESPONSE_YES)
            break;
        }
      else
        break;
    }
    
  gtk_widget_destroy (dialog);
}


