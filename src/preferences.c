
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
 * @file preferences.c
 * @brief Preferences management
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <sys/utsname.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "main.h"
#include "gui.h"
#include "protocol.h"
#include "preferences.h"
#include "config.h"
#include "main.h"
#include "connection.h"
#include "profile.h"

extern Globals globals;
extern Prefs prefs;
extern GtkWidget *main_window;
extern struct ConnectionTab *p_current_connection_tab;
extern struct ProfileList g_profile_list;

struct Profile *g_selected_profile;

GtkWidget *dialog_preferences;
GtkWidget *default_profile_combo;
GtkWidget *vte_profile;
GtkWidget *fontbutton_terminal;

enum { COLUMN_PRF_NAME, N_PRF_COLUMNS };
GtkListStore *list_store_profiles;
GtkTreeModel *tree_model_profiles;

void
refresh_profiles_list_store ()
{
  struct Profile *p;
  GtkTreeIter iter;
  int i = 0, default_pos = 0;

  gtk_list_store_clear (list_store_profiles);
  
#if (GTK_MAJOR_VERSION == 2)
  for (i=0; i<profile_count (&g_profile_list)+1; i++)
    gtk_combo_box_remove_text (GTK_COMBO_BOX (default_profile_combo), 0);
#else
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (default_profile_combo));
#endif

  p = g_profile_list.head;
  
  i = 0;
  while (p)
    {
      gtk_list_store_append (list_store_profiles, &iter);
      gtk_list_store_set (list_store_profiles, &iter, COLUMN_PRF_NAME, p->name, -1);
      
      log_debug ("%d %s\n", i, p->name);

#if (GTK_MAJOR_VERSION == 2)
      gtk_combo_box_append_text (GTK_COMBO_BOX (default_profile_combo), p->name);
#else
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (default_profile_combo), p->name);
#endif
      
      if (p->id == g_profile_list.id_default)
        default_pos = i;
      
      i ++;
      p = p->next;
    }

  log_debug ("default profile position: %d\n", default_pos);

  gtk_combo_box_set_active (GTK_COMBO_BOX (default_profile_combo), default_pos);
}

void
check_use_system_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
#if (GTK_MAJOR_VERSION == 2)
  gtk_widget_set_sensitive (fontbutton_terminal, !gtk_toggle_button_get_active (togglebutton));
#else
  gtk_widget_set_state_flags (fontbutton_terminal, 
                              gtk_toggle_button_get_active (togglebutton) ? GTK_STATE_FLAG_INSENSITIVE : GTK_STATE_FLAG_NORMAL, 
                              TRUE);
#endif
}

void
profile_edit (struct Profile *p_profile)
{
  GtkBuilder *builder;
  GError *error=NULL; 
  GtkWidget *dialog;
  struct Profile new_profile;
  char ui[256];

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/profile.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }

  /* Create dialog */

  dialog = gtk_dialog_new_with_buttons
                 (_("Edit profile"), NULL,
                  GTK_DIALOG_MODAL, 
                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                  GTK_STOCK_OK, GTK_RESPONSE_OK,
                  NULL);

  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));
  //gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  //gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  GtkWidget *notebook1 = GTK_WIDGET (gtk_builder_get_object (builder, "notebook1"));

  /* name */
  GtkWidget *entry_name = GTK_WIDGET (gtk_builder_get_object (builder, "entry_name"));
  if (p_profile)
    gtk_entry_set_text (GTK_ENTRY (entry_name), p_profile->name);

  /* font */
  fontbutton_terminal = GTK_WIDGET (gtk_builder_get_object (builder, "fontbutton_terminal"));
  gtk_font_button_set_font_name (GTK_FONT_BUTTON (fontbutton_terminal), p_profile && p_profile->font[0] ? p_profile->font : "Monospace 9");

  GtkWidget *check_use_system = GTK_WIDGET (gtk_builder_get_object (builder, "check_use_system"));
  g_signal_connect (check_use_system, "toggled", G_CALLBACK (check_use_system_cb), NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_use_system), p_profile ? p_profile->font_use_system : TRUE);

  /* cursor */
  GtkWidget *combo_shape = GTK_WIDGET (gtk_builder_get_object (builder, "combo_shape"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo_shape), p_profile ? p_profile->cursor_shape : 0);

  GtkWidget *check_blinking = GTK_WIDGET (gtk_builder_get_object (builder, "check_blinking"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_blinking), p_profile ? p_profile->cursor_blinking : TRUE);

  /* bell */
  GtkWidget *check_audible_bell = GTK_WIDGET (gtk_builder_get_object (builder, "check_audible_bell"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_audible_bell), p_profile ? p_profile->bell_audible : TRUE);

  GtkWidget *check_visible_bell = GTK_WIDGET (gtk_builder_get_object (builder, "check_visible_bell"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_visible_bell), p_profile ? p_profile->bell_visible : TRUE);

  /* colors */
  GtkWidget *color_fg = GTK_WIDGET (gtk_builder_get_object (builder, "color_fg"));
  GtkWidget *color_bg = GTK_WIDGET (gtk_builder_get_object (builder, "color_bg"));

#if (GTK_MAJOR_VERSION == 2)
  GdkColor fg, bg;
  if (p_profile)
    {
      gdk_color_parse (p_profile->fg_color, &fg);
      gdk_color_parse (p_profile->bg_color, &bg);
    }
  else
    {
      gdk_color_parse ("light gray", &fg);
      gdk_color_parse ("black", &bg);
    }  
    
  gtk_color_button_set_color (GTK_COLOR_BUTTON (color_fg), &fg);
  gtk_color_button_set_color (GTK_COLOR_BUTTON (color_bg), &bg);
#else
  GdkRGBA fg, bg;
  if (p_profile)
    {
      gdk_rgba_parse (&fg, p_profile->fg_color);
      gdk_rgba_parse (&bg, p_profile->bg_color);
    }
  else
    {
      gdk_rgba_parse (&fg, "light gray");
      gdk_rgba_parse (&bg, "black");
    }

  gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER(color_fg), &fg);
  gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER(color_bg), &bg);
#endif
/*
  GtkWidget *scale_opacity = GTK_WIDGET (gtk_builder_get_object (builder, "scale_opacity"));
  gtk_range_set_value (GTK_RANGE(scale_opacity), p_profile ? p_profile->alpha : 1.0);
*/
  gtk_container_add (GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), notebook1);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
  
  while (1)
    {
      gint result = gtk_dialog_run (GTK_DIALOG (dialog));

      if (result == GTK_RESPONSE_OK)
        {
          memset (&new_profile, 0, sizeof (struct Profile));

          if (p_profile)
            {
              new_profile.id = p_profile->id;
              new_profile.next = p_profile->next;
            }

          strcpy (new_profile.name, gtk_entry_get_text (GTK_ENTRY (entry_name)));

          if (new_profile.name[0] == 0)
            {
              msgbox_error (_("Missing name for profile")); 
              continue;
            }

          struct Profile *p = profile_get_by_name (&g_profile_list, new_profile.name);

          if (p)
            {
              if (p_profile == NULL || (p_profile && (p_profile != p)))
                {
                  msgbox_error (_("Name is already assigned to another profile")); 
                  continue;
                }
            }

          new_profile.font_use_system = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_use_system)) ? 1 : 0;
          strcpy (new_profile.font, gtk_font_button_get_font_name (GTK_FONT_BUTTON(fontbutton_terminal)));

          new_profile.cursor_shape = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_shape));
          new_profile.cursor_blinking = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_blinking)) ? 1 : 0;

          new_profile.bell_audible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_audible_bell)) ? 1 : 0;
          new_profile.bell_visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_visible_bell)) ? 1 : 0;
          
#if (GTK_MAJOR_VERSION == 2)
          gtk_color_button_get_color (GTK_COLOR_BUTTON (color_fg), &fg);
          gtk_color_button_get_color (GTK_COLOR_BUTTON (color_bg), &bg);
          strcpy (new_profile.fg_color, gdk_color_to_string (&fg));
          strcpy (new_profile.bg_color, gdk_color_to_string (&bg));
#else
          gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER(color_fg), &fg);
          strcpy (new_profile.fg_color, gdk_rgba_to_string (&fg));

          gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER(color_bg), &bg);
          strcpy (new_profile.bg_color, gdk_rgba_to_string (&bg));
#endif
          //new_profile.alpha = gtk_range_get_value (GTK_RANGE(scale_opacity));

          if (p_profile)
            memcpy (p_profile, &new_profile, sizeof (struct Profile));
          else
            profile_list_append (&g_profile_list, &new_profile);

          break;
        }
      else
        {
          break;
        }
    }

  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));
}

void
profile_new_cb (GtkButton *button, gpointer user_data)
{
  profile_edit (NULL);

  refresh_profiles_list_store ();
  refresh_profile_menu ();
}

void
profile_edit_cb (GtkButton *button, gpointer user_data)
{
  profile_edit (g_selected_profile);

  refresh_profiles_list_store ();
  refresh_profile_menu ();
}

void
profile_delete_cb (GtkButton *button, gpointer user_data)
{
  int result;

  if (g_selected_profile == NULL)
    return;

  result = msgbox_yes_no ("Delete profile %s?", g_selected_profile->name);

  if (result == GTK_RESPONSE_NO)
    return;

  profile_list_delete (&g_profile_list, g_selected_profile);
  g_selected_profile = NULL;

  refresh_profiles_list_store ();
  refresh_profile_menu ();
}

gboolean
profile_selected_cb (GtkTreeView *tree_view, gpointer user_data)
{
  GtkTreePath *path;
  GtkTreeIter iter;
  char *tmp_s;
  int i;

  gtk_tree_view_get_cursor (tree_view, &path, NULL);

  if (path == NULL)
    return (FALSE);

  tmp_s = gtk_tree_path_to_string (path);
  //log_debug ("%s\n", tmp_s);
  
  i = atoi (tmp_s);
  g_free (tmp_s);
  
  g_selected_profile = profile_get_by_position (&g_profile_list, i);
  
  log_debug ("%s\n", g_selected_profile->name);
  apply_profile_terminal (vte_profile, g_selected_profile);

  return (TRUE);
}

void
radio_ask_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
#if (GTK_MAJOR_VERSION == 2)
  gtk_widget_set_sensitive (GTK_WIDGET(user_data), FALSE);
#else
  gtk_widget_set_state_flags (GTK_WIDGET(user_data), GTK_STATE_FLAG_INSENSITIVE, TRUE);
#endif
}

void
radio_dir_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
#if (GTK_MAJOR_VERSION == 2)
  gtk_widget_set_sensitive (GTK_WIDGET(user_data), TRUE);
#else
  gtk_widget_set_state_flags (GTK_WIDGET(user_data), GTK_STATE_FLAG_NORMAL, TRUE);
#endif
}

void
show_preferences ()
{
  GtkBuilder *builder; 
  GError *error=NULL;
  GtkWidget *button_ok, *button_cancel;
  GtkWidget *dialog, *notebook;
  GtkWidget *font_entry, *fg_color_entry, *bg_color_entry;
  struct Profile *p_profile;
  //char *ui = "/home/fabio/src/lterm-0.5.x/data/preferences.glade";
  //char *ui = "/dati/Source/lterm-0.5.x/data/preferences.glade";
  char ui[256], tmp[512];

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/preferences.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }

  /* Create dialog */

  dialog = gtk_dialog_new_with_buttons
                 (_("Preferences"), NULL,
                  GTK_DIALOG_MODAL, 
                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                  GTK_STOCK_OK, GTK_RESPONSE_OK,
                  NULL);

  /*gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);*/
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));
  //gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  //gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

	/* check rgba capabilities */
/*
	GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (dialog));
	GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
	if (visual != NULL && gdk_screen_is_composited (screen))
    {
		  gtk_widget_set_visual (GTK_WIDGET (dialog), visual);
		  log_write ("[%s] RGBA capabilities OK\n", __func__);
	  } 
  else
    {
		  log_write ("[%s] can't get visual, no rgba capabilities!\n", __func__);
	  }
*/
  notebook = GTK_WIDGET (gtk_builder_get_object (builder, "notebook1"));

  /* startup */
  GtkWidget *startlocal_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_startlocal"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (startlocal_check), prefs.startup_local_shell);

  GtkWidget *startconn_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_connections"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (startconn_check), prefs.startup_show_connections);

  GtkWidget *save_session_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_reload"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (save_session_check), prefs.save_session);

  /* tabs */
  GtkWidget *tabs_pos_combo = GTK_WIDGET (gtk_builder_get_object (builder, "combo_tabs_pos"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tabs_pos_combo), _("Left"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tabs_pos_combo), _("Right"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tabs_pos_combo), _("Top"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tabs_pos_combo), _("Bottom"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (tabs_pos_combo), prefs.tabs_position);
  
  /* start directory */
  GtkWidget *start_directory_entry = GTK_WIDGET (gtk_builder_get_object (builder, "entry_dir"));
  gtk_entry_set_text (GTK_ENTRY (start_directory_entry), prefs.local_start_directory);

  GtkWidget *tab_alerts_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_tab_alerts"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab_alerts_check), prefs.tab_alerts);

  /* quick launch window font */
  GtkWidget *qlw_font_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fontbutton_qlw"));
  gtk_font_button_set_font_name (GTK_FONT_BUTTON (qlw_font_entry), prefs.font_quick_launch_window);

  /* profile list */

  g_selected_profile = NULL;

  GtkTreeSelection *select;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *column;
  GtkTreeModel *tree_model;
  GtkTreeIter iter;

  GtkWidget *tree_view = gtk_tree_view_new ();

  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Profile name"), cell, "text", COLUMN_PRF_NAME, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(tree_view), FALSE);
  
  list_store_profiles = gtk_list_store_new (N_PRF_COLUMNS, G_TYPE_STRING);

  gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), GTK_TREE_MODEL (list_store_profiles));
  g_signal_connect (tree_view, "cursor-changed", G_CALLBACK (profile_selected_cb), NULL);
  gtk_widget_show (tree_view);

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

  tree_model_profiles = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));

  /* buttons */
  GtkWidget *button_new = GTK_WIDGET (gtk_builder_get_object (builder, "button_new"));
  g_signal_connect (G_OBJECT (button_new), "clicked", G_CALLBACK (profile_new_cb), NULL);

  GtkWidget *button_edit = GTK_WIDGET (gtk_builder_get_object (builder, "button_edit"));
  g_signal_connect (G_OBJECT (button_edit), "clicked", G_CALLBACK (profile_edit_cb), NULL);

  GtkWidget *button_delete = GTK_WIDGET (gtk_builder_get_object (builder, "button_delete"));
  g_signal_connect (G_OBJECT (button_delete), "clicked", G_CALLBACK (profile_delete_cb), NULL);
  
  /* scrolled window */
  GtkWidget *scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "scrolled_profiles"));
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_ETCHED_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);
  
  vte_profile = GTK_WIDGET (gtk_builder_get_object (builder, "vte_profile"));
  vte_terminal_set_size (VTE_TERMINAL(vte_profile), 55, 12);
  vte_terminal_feed (VTE_TERMINAL(vte_profile), /*"drwxr-xr-x   2 root root  4096 ott 13 22:41 mnt\r\n"
                                  "drwxr-xr-x  10 root root  4096 ott 16 21:48 usr\r\n"
                                  "drwxr-xr-x   3 root root  4096 dic 21 17:31 home\r\n"*/
                                  "drwxr-xr-x  24 root root  4096 dic 21 18:14 lib\r\n"
                                  "drwxr-xr-x  13 root root  4096 dic 21 18:23 var\r\n"
                                  "drwxr-xr-x   2 root root 12288 dic 22 16:32 sbin\r\n"
                                  "drwxr-xr-x   2 root root  4096 dic 22 16:36 bin\r\n"
                                  "dr-xr-xr-x 219 root root     0 gen 23 09:25 proc\r\n"
                                  "dr-xr-xr-x  13 root root     0 gen 23 09:25 sys\r\n"
                                  "drwxr-xr-x 150 root root 12288 gen 23 09:25 etc\r\n"
                                  "drwxr-xr-x  18 root root  4260 gen 23 09:25 dev\r\n", -1);
  
  default_profile_combo = GTK_WIDGET (gtk_builder_get_object (builder, "combo_default_profile"));

  /* mouse */
  GtkWidget *mouse_copy_on_select_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_mouse_copy"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mouse_copy_on_select_check), prefs.mouse_copy_on_select);

  GtkWidget *mouse_paste_on_right_button_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_mouse_paste"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mouse_paste_on_right_button_check), prefs.mouse_paste_on_right_button);

  GtkWidget *mouse_autohide_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_mouse_hide"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mouse_autohide_check), prefs.mouse_autohide);

  /* scrollback */
  GtkWidget *spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "spin_scrollback"));
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button), prefs.scrollback_lines);

  GtkWidget *scroll_on_keystroke_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_scrollkey"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scroll_on_keystroke_check), prefs.scroll_on_keystroke);

  GtkWidget *scroll_on_output_check = GTK_WIDGET (gtk_builder_get_object (builder, "check_scrolloutput"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scroll_on_output_check), prefs.scroll_on_output);

  /* sftp */
  GtkWidget *spin_buffer = GTK_WIDGET (gtk_builder_get_object (builder, "spin_buffer"));
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_buffer), (int) prefs.sftp_buffer / 1024);

  GtkWidget *radio_ask = GTK_WIDGET (gtk_builder_get_object (builder, "radio_ask"));
  GtkWidget *radio_dir = GTK_WIDGET (gtk_builder_get_object (builder, "radio_dir"));

  GtkWidget *entry_download_dir = GTK_WIDGET (gtk_builder_get_object (builder, "entry_download_dir"));
  gtk_entry_set_text (GTK_ENTRY (entry_download_dir), prefs.download_dir);
  g_signal_connect (radio_ask, "toggled", G_CALLBACK (radio_ask_cb), entry_download_dir);
  g_signal_connect (radio_dir, "toggled", G_CALLBACK (radio_dir_cb), entry_download_dir);

  if (prefs.flag_ask_download)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(radio_ask), TRUE);
      
#if (GTK_MAJOR_VERSION == 2)
      gtk_widget_set_sensitive (entry_download_dir, FALSE);
#else
      gtk_widget_set_state_flags (entry_download_dir, GTK_STATE_FLAG_INSENSITIVE, TRUE);
#endif
    }
  else
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(radio_dir), TRUE);
      
#if (GTK_MAJOR_VERSION == 2)
      gtk_widget_set_sensitive (entry_download_dir, TRUE);
#else
      gtk_widget_set_state_flags (entry_download_dir, GTK_STATE_FLAG_NORMAL, TRUE);
#endif
    }
    
  GtkWidget *entry_text_editor = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text_editor"));
  gtk_entry_set_text (GTK_ENTRY (entry_text_editor), prefs.text_editor);
    
  //GtkWidget *entry_uri = GTK_WIDGET (gtk_builder_get_object (builder, "entry_uri"));
  //gtk_entry_set_text (GTK_ENTRY (entry_uri), prefs.sftp_open_file_uri);

  /* Hyperlinks */
  
  GtkWidget *check_hyperlink_tooltip = GTK_WIDGET (gtk_builder_get_object (builder, "check_hyperlink_tooltip"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_hyperlink_tooltip), prefs.hyperlink_tooltip_enabled);
  
  GtkWidget *check_hyperlink_leftmouse = GTK_WIDGET (gtk_builder_get_object (builder, "check_hyperlink_leftmouse"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_hyperlink_leftmouse), prefs.hyperlink_click_enabled);


  refresh_profiles_list_store ();

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), notebook);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
  
  gint result = gtk_dialog_run (GTK_DIALOG (dialog));

  if (result == GTK_RESPONSE_OK)
    {
      prefs.startup_local_shell = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (startlocal_check)) ? 1 : 0;
      prefs.startup_show_connections = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (startconn_check)) ? 1 : 0;
      prefs.save_session = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (save_session_check)) ? 1 : 0;

      prefs.tabs_position = gtk_combo_box_get_active (GTK_COMBO_BOX (tabs_pos_combo));
      prefs.tab_alerts = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tab_alerts_check)) ? 1 : 0;
      
      strcpy (prefs.local_start_directory, gtk_entry_get_text (GTK_ENTRY (start_directory_entry)));
          
      strcpy (prefs.font_quick_launch_window, gtk_font_button_get_font_name (GTK_FONT_BUTTON (qlw_font_entry)));

      prefs.mouse_copy_on_select = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (mouse_copy_on_select_check)) ? 1 : 0;
      prefs.mouse_paste_on_right_button = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (mouse_paste_on_right_button_check)) ? 1 : 0;
      prefs.mouse_autohide = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (mouse_autohide_check)) ? 1 : 0;
      
      prefs.scrollback_lines = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin_button));
      prefs.scroll_on_keystroke = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scroll_on_keystroke_check)) ? 1 : 0;
      prefs.scroll_on_output = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scroll_on_output_check)) ? 1 : 0;

      prefs.sftp_buffer = 1024 * gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin_buffer));
      prefs.flag_ask_download = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(radio_ask)) ? 1 : 0;
      strcpy (prefs.download_dir, gtk_entry_get_text (GTK_ENTRY (entry_download_dir)));
      strcpy (prefs.text_editor, gtk_entry_get_text (GTK_ENTRY (entry_text_editor)));
      //strcpy (prefs.sftp_open_file_uri, gtk_entry_get_text (GTK_ENTRY (entry_uri)));
      
      if (p_profile = profile_get_by_position (&g_profile_list, 
                                               gtk_combo_box_get_active (GTK_COMBO_BOX (default_profile_combo))))
        g_profile_list.id_default = p_profile->id;

      prefs.hyperlink_tooltip_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_hyperlink_tooltip)) ? 1 : 0;
      prefs.hyperlink_click_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_hyperlink_leftmouse)) ? 1 : 0;

      apply_preferences ();
      update_all_profiles ();
      //break;
    }
  else
    {
      //retcode = 1; /* cancel */
      //break;
    }

  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));
}

