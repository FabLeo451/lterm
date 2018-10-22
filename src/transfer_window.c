
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
 * @file transfer_window.c
 * @brief Shows uploads and downloads
 */

#include <gtk/gtk.h>
#include "main.h"
#include "sftp-panel.h"
#include "utils.h"
#include "transfer_window.h"

extern GtkWidget *main_window;
extern Globals globals;
extern Prefs prefs;
extern struct SFTP_Panel sftp_panel;
extern GdkPixbuf *pixbuf_file, *pixbuf_dir;

GtkTreeSelection *g_transfer_selection;
GtkWidget *transferQueueWindow = 0;
//GtkAdjustment *transferAdjustment;
gboolean gTransferWindow, gForceRefresh, gRefreshing;
GdkPixbuf *pixbufUpload, *pixbufDownload;

enum { TR_COL_ACTION, TR_COL_FILE_ICON, TR_COL_FILENAME, TR_COL_STATUS, TR_COL_PROGRESS, 
       TR_COL_TOTAL_SIZE, TR_COL_TRANSFERRED, TR_COL_SPEED, TR_COL_ETA, N_TRANSFER_COLUMNS };
//enum { SORTID_ICON = 0, SORTID_NAME, SORTID_SIZE, SORTID_DATE };
  
GtkListStore *ls_transfer;
GtkTreeModel *tm_transfer;

GtkActionEntry transfer_popup_menu_items [] = {
  { "Details", NULL, N_("_Details"), "", NULL, G_CALLBACK (transfer_details) },
  { "Cancel", NULL, N_("_Cancel"), "", NULL, G_CALLBACK (transfer_cancel) },
  { "RemoveCompleted", NULL, N_("_Remove completed or cancelled"), "", NULL, G_CALLBACK (transfer_remove_completed) }
};

gchar *ui_transfer_popup_desc =
  "<ui>"
  "  <popup name='TransferWindowPopupMenu' accelerators='true'>"
  "    <menuitem action='Details'/>"
  "    <menuitem action='Cancel'/>"
  "    <separator />"
  "    <menuitem action='RemoveCompleted'/>"
  "  </popup>"
  "</ui>";
  
gint
transfer_window_delete_event_cb (GtkWidget *window, GdkEventAny *e, gpointer data)
{
  gTransferWindow = FALSE;

  return TRUE;
}

int
get_selected_transfer_nth ()
{
  GtkTreeIter iter;
  GtkTreePath *path;
  int *i, position = -1;

  if (gtk_tree_selection_get_selected (g_transfer_selection, &tm_transfer, &iter)) {
    path = gtk_tree_model_get_path (tm_transfer, &iter) ;
    i = gtk_tree_path_get_indices (path);
    position = i[0];
  }

  return position;
}

void 
transfer_details ()
{
  STransferInfo *pTi;
  int i;

  i = get_selected_transfer_nth ();

  log_debug ("%d\n", i);

  if (i < 0)
    return;

  pTi = (STransferInfo *) g_list_nth_data (sftp_panel.queue, i);

  GtkWidget *dialog = gtk_dialog_new ();

  gtk_window_set_title (GTK_WINDOW (dialog), _("Details"));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  //gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 10);
  //gtk_box_set_spacing (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), 10);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  GtkWidget *ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  GtkWidget *label_details = gtk_label_new (NULL);

  char details[4096], tmpSize[32], tmpWorked[32], startTime[32];

  struct tm *tml = localtime (&pTi->start_time);
  strftime (startTime, sizeof (startTime), "%Y-%m-%d %H:%M:%S", tml);

  sprintf (details,
          "<b>Action:</b> %s\n"
          "<b>Start date:</b>: %s\n"
          "<b>File:</b> %s\n"
          "<b>Size:</b> %s (%lld bytes)\n"
          "<b>Transferred:</b> %s (%lld bytes)\n"
          "<b>Source:</b> %s\n"
          "<b>Destination folder:</b> %s\n"
          "<b>Remote host:</b> %s\n"
          "<b>Status:</b> %s\n"
          "<b>Error:</b> %s",
          pTi->action == SFTP_ACTION_UPLOAD ? "Upload" : "Download",
          startTime,
          pTi->filename,
          bytes_to_human_readable (pTi->size, tmpSize), pTi->size,
          bytes_to_human_readable (pTi->worked, tmpWorked), pTi->worked,
          pTi->source,
          pTi->destDir,
          pTi->host,
          getTransferStatusDesc (pTi->state),
          pTi->result != 0 ? transfer_get_error (pTi) : ""
      );

  gtk_label_set_markup (GTK_LABEL(label_details), details);
  
  gtk_box_pack_start (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), label_details, TRUE, TRUE, 0);

  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  // Run dialog
  int result = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

void 
transfer_details_cb (GtkButton *button, 
                     gpointer user_data)
{
  transfer_details ();
}

void 
transfer_cancel ()
{
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  GtkTreeIter       iter;
  STransferInfo *pTi;
  int i;

  log_debug ("\n");

  lockSFTPQueue (__func__, TRUE);

  i = get_selected_transfer_nth ();

  log_debug ("Selected item: %d\n", i);

  if (i >= 0) {
    pTi = sftp_queue_nth (i);

    if (pTi->state <= TR_PAUSED) {
      transfer_set_error (pTi, 1, "Cancelled by user");
      pTi->state = TR_CANCELLED_USER;
      gForceRefresh = TRUE;

      log_write ("Cancelled item %d\n", i);
    }
  }

  lockSFTPQueue (__func__, FALSE);
}

void
transfer_cancel_cb (GtkButton *button, 
                    gpointer user_data)
{
  transfer_cancel ();
}

void 
transfer_remove_completed ()
{
  int i, nDel = 0;
  STransferInfo *pTi;
  gpointer data;
  GList *current;

  log_debug ("\n");

  lockSFTPQueue (__func__, TRUE);

  current = g_list_first (sftp_panel.queue);

  while (current) {
    data = current->data;
    pTi = (STransferInfo *) data;

    log_debug ("%s %s\n", pTi->filename, getTransferStatusDesc (pTi->state));

    current = g_list_next (current);

    if (pTi->state > TR_PAUSED) {
      log_write ("Removing item %s from SFTP queue..\n", pTi->filename);

      sftp_panel.queue = g_list_remove (sftp_panel.queue, data);
      nDel ++;
    }
  }

  lockSFTPQueue (__func__, FALSE);

  log_debug ("Removed %d\n", nDel);

  if (nDel) {
    refresh_transfer_list_store ();
    gForceRefresh = TRUE;
  }
}

void
transfer_remove_completed_cb (GtkButton *button, 
                    gpointer user_data)
{
  transfer_remove_completed ();
}

void
transfer_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  GtkUIManager *ui_manager;
  GtkActionGroup *action_group;
  GtkWidget *popup;

  log_debug ("\n");

  action_group = gtk_action_group_new ("TransferWindowPopupMenu");
  gtk_action_group_add_actions (action_group, transfer_popup_menu_items, G_N_ELEMENTS (transfer_popup_menu_items), treeview);

  ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

  gtk_ui_manager_add_ui_from_string (ui_manager, ui_transfer_popup_desc, -1, NULL);

  popup = gtk_ui_manager_get_widget (ui_manager, "/TransferWindowPopupMenu");

  /* Note: event can be NULL here when called from view_onPopupMenu;
   *  gdk_event_get_time() accepts a NULL argument */
  gtk_menu_popup (GTK_MENU(popup), NULL, NULL, NULL, NULL,
                 (event != NULL) ? event->button : 0,
                 gdk_event_get_time((GdkEvent*)event));
}

gboolean
transfer_onButtonPressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  //GtkTreeSelection *selection;

  log_debug ("\n");

  /* single click with the right mouse button? */
  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
    {
      log_debug ("Single right click on the tree view.\n");

      /* select row if no row is selected */

      //selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(treeview));

      GtkTreePath *path;

      // Get tree path for row that was clicked
      if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(treeview),  (gint) event->x, (gint) event->y, &path, NULL, NULL, NULL))
        {
          gtk_tree_selection_unselect_all (g_transfer_selection);
          gtk_tree_selection_select_path (g_transfer_selection, path);
          /*
          int *i;
          i = gtk_tree_path_get_indices (path);
          gSelectedTransferIndex = i[0];

          gtk_tree_path_free (path);*/
        }
      else
        {
          return FALSE;
        }

      transfer_popup_menu (treeview, event, userdata);

      log_debug ("handled\n");
      return TRUE; /* we handled this */
    }

  log_debug ("not handled\n");
  return FALSE; /* we did not handle this */
}
/*
gboolean
transfer_onPopupMenu (GtkWidget *treeview, gpointer userdata)
{
  log_debug ("\n");
  
  //transfer_popup_menu (treeview, NULL, userdata);

  return TRUE; // we handled this
}
*/

void
status_cell_data_func (GtkTreeViewColumn *tree_column,
                       GtkCellRenderer *renderer,
                       GtkTreeModel *tree_model,
                       GtkTreeIter *iter,
                       gpointer data)
{
  GtkTreePath *path;
  STransferInfo *pTi;
  int *iSet, i;

  path = gtk_tree_model_get_path (tree_model, iter);
  iSet = gtk_tree_path_get_indices (path);
  i = iSet[0];

  pTi = (STransferInfo *) g_list_nth_data (sftp_panel.queue, i);

  //log_debug ("%s %s\n", pTi->shortenedFilename, getTransferStatusDesc (pTi->state));

  switch (pTi->state) {
    case TR_READY:
      g_object_set(renderer, "foreground", "Blue", "foreground-set", TRUE, NULL);
      break;

    case TR_CANCELLED_USER:
    case TR_CANCELLED_ERRORS:
      g_object_set(renderer, "foreground", "Red", "foreground-set", TRUE, NULL);
      break;

    case TR_COMPLETED:
      g_object_set(renderer, "foreground", "Green", "foreground-set", TRUE, NULL);
      break;

    default:
      g_object_set(renderer, "foreground", "Black", "foreground-set", TRUE, NULL);
      break;
  }
}

void
progress_cell_data_func (GtkTreeViewColumn *tree_column,
                       GtkCellRenderer *renderer,
                       GtkTreeModel *tree_model,
                       GtkTreeIter *iter,
                       gpointer data)
{
  GtkTreePath *path;
  STransferInfo *pTi;
  int *iSet, i;

  //if (gtk_list_store_iter_is_valid (ls_transfer, iter) == FALSE)
  //  return;

  path = gtk_tree_model_get_path (tree_model, iter);
  iSet = gtk_tree_path_get_indices (path);
  i = iSet[0];

  pTi = sftp_queue_nth (i);

  int progress;
  char pct[64];

  if (pTi->state != TR_READY)
    progress = pTi->size > 0 ? (guint)(((double)pTi->worked/(double)pTi->size)*(double)100.0) : 100;
  else
    progress = 0;

  sprintf (pct, "%d%%", progress);

  //log_debug ("[%d] %s %s %s\n", i, pTi->shortenedFilename, pTi->sourceIsDir ? "DIR" : "FILE", getTransferStatusDesc (pTi->state));

  switch (pTi->state) {
    case TR_READY:
      if (pTi->sourceIsDir)
        g_object_set(renderer, "pulse", 0, "text", "", NULL);
      else
        g_object_set(renderer, "pulse", -1, "value", 0, "text", pct, NULL);
      break;

    case TR_IN_PROGRESS:
      if (pTi->sourceIsDir)
        g_object_set(renderer, "pulse", (time (NULL) - pTi->start_time) % 10, "text", "", NULL);
      else
        g_object_set(renderer, "pulse", -1, "value", progress, "text", pct, NULL);
      break;

    case TR_CANCELLED_USER:
    case TR_CANCELLED_ERRORS:
      if (pTi->sourceIsDir)
        g_object_set(renderer, "pulse", 0, "text", "", NULL);
      else
        g_object_set(renderer, "pulse", -1, "value", progress, "text", pct, NULL);
      break;

    case TR_COMPLETED:
      g_object_set(renderer, "pulse", -1, "value", 100, "text", "100%", NULL);
      break;

    default:
      log_debug ("default state: %d\n", pTi->state);
      g_object_set(renderer, "pulse", 0, NULL);
      break;
  }
}

GtkWidget *
create_transfer_window_tree_view ()
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  //GtkTreeModel *tree_model;
  GtkTreeIter iter;
  //GtkTreeSortable *sortable;

  log_debug ("Creating transfer tree view...\n");

  GtkWidget *tree_view = gtk_tree_view_new ();
  //gtk_tree_view_set_headers_clickable (GTK_TREE_VIEW(tree_view), TRUE);
  
  g_signal_connect (tree_view, "button-press-event", (GCallback) transfer_onButtonPressed, NULL);
  //g_signal_connect (tree_view, "popup-menu", (GCallback) sftp_onPopupMenu, NULL);
  /*g_object_set (tree_view, "has-tooltip", TRUE, (char *) 0);
  g_signal_connect (tree_view, "query-tooltip", (GCallback) sftp_cell_tooltip_cb, 0);*/
  
  // Filename 
  log_debug ("Filename\n");

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("File"));
  gtk_tree_view_column_set_resizable (column, TRUE);

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column, renderer, "pixbuf", TR_COL_ACTION, NULL);

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column, renderer, "pixbuf", TR_COL_FILE_ICON, NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes(column, renderer, "text", TR_COL_FILENAME, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  
  // Status column
  log_debug ("Status\n");
/*
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Status"), renderer, "text", TR_COL_STATUS, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
*/

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Status"));
  gtk_tree_view_column_set_resizable (column, TRUE);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes(column, renderer, "text", TR_COL_STATUS, NULL);

  gtk_tree_view_column_set_cell_data_func (column, renderer, status_cell_data_func, NULL, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  
  // Progress bar
  log_debug ("Progress bar\n");
  GtkCellRenderer *progress = gtk_cell_renderer_progress_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Progress"), progress, "value", TR_COL_PROGRESS, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  gtk_tree_view_column_set_cell_data_func (column, progress, progress_cell_data_func, NULL, NULL);

  // Total size
  log_debug ("Total size\n");
  renderer = gtk_cell_renderer_text_new ();
  //g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  //gtk_cell_renderer_set_alignment (renderer, 1.0, 0.0);
  column = gtk_tree_view_column_new_with_attributes (_("Total size"), renderer, "text", TR_COL_TOTAL_SIZE, NULL);
  //gtk_tree_view_column_set_alignment (column, 1.0);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  
  // Transferred size
  log_debug ("Transferred\n");
  renderer = gtk_cell_renderer_text_new ();
  //g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  //gtk_cell_renderer_set_alignment (renderer, 1.0, 0.0);
  column = gtk_tree_view_column_new_with_attributes (_("Transferred"), renderer, "text", TR_COL_TRANSFERRED, NULL);
  //gtk_tree_view_column_set_alignment (column, 1.0);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  // Speed
  log_debug ("Speed\n");
  renderer = gtk_cell_renderer_text_new ();
  //g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_("Speed"), renderer, "text", TR_COL_SPEED, NULL);
  //gtk_tree_view_column_pack_start (column, renderer, TRUE);
  //gtk_tree_view_column_set_attributes(column, renderer, "text", TR_COL_SPEED, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  // Time left
  log_debug ("Time left\n");
  renderer = gtk_cell_renderer_text_new ();
  //g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_("Time left"), renderer, "text", TR_COL_ETA, NULL);
  //gtk_tree_view_column_pack_start (column, renderer, TRUE);
  //gtk_tree_view_column_set_attributes(column, renderer, "text", TR_COL_ETA, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
 
  // Create list store
  ls_transfer = gtk_list_store_new (N_TRANSFER_COLUMNS, GDK_TYPE_PIXBUF, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), GTK_TREE_MODEL (ls_transfer));
  tm_transfer = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));

  // Selection
  g_transfer_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  //gtk_tree_selection_set_mode (g_sftp_selection, GTK_SELECTION_SINGLE

  //g_signal_connect (G_OBJECT (tree_view), "row-activated", G_CALLBACK (row_activated_sftp_cb), g_sftp_selection);

  GError *error=NULL;
  char imgFile[512];

  sprintf (imgFile, "%s/upload.png", globals.img_dir);
  pixbufUpload = gdk_pixbuf_new_from_file_at_scale (imgFile, 16, -1, TRUE, &error);

  sprintf (imgFile, "%s/download.png", globals.img_dir);
  pixbufDownload = gdk_pixbuf_new_from_file_at_scale (imgFile, 16, -1, TRUE, &error);

  return tree_view;
}

void
refresh_transfer_list_store ()
{
  int i;
  STransferInfo *pTi;
  GdkPixbuf *icon, *actionIcon;
  GtkTreeIter iter;

  char tmp_s[1024];
  int n=0;

  lockSFTPQueue (__func__, TRUE);

  gtk_list_store_clear (ls_transfer);

  log_debug ("(Re)building list store...\n");

  for (i=0; i<g_list_length (sftp_panel.queue); i++) {
    pTi = (STransferInfo *) g_list_nth (sftp_panel.queue, i)->data;

    gtk_list_store_append (ls_transfer, &/*pTi->*/iter);

    icon = pTi->sourceIsDir ? pixbuf_dir : get_type_pixbuf (pTi->filename);
    
    if (icon == NULL)
      icon = pixbuf_file;

    gtk_list_store_set (ls_transfer, &/*pTi->*/iter, 
                        TR_COL_FILE_ICON, icon, 
                        TR_COL_FILENAME, pTi->shortenedFilename, 
                        TR_COL_ACTION, pTi->action == SFTP_ACTION_UPLOAD ? pixbufUpload : pixbufDownload,
                        TR_COL_TOTAL_SIZE, bytes_to_human_readable (pTi->size, tmp_s), 
                        -1);
  }
  
  lockSFTPQueue (__func__, FALSE);
}

void
transfer_window_refresh ()
{
  int i;
  char tmp_s[1024];
  STransferInfo *pTi;

  lockSFTPQueue (__func__, TRUE);

  log_debug ("Refreshing transfer window... (%d)\n", g_list_length (sftp_panel.queue));

  for (i=0; i<g_list_length (sftp_panel.queue); i++) {
    pTi = (STransferInfo *) g_list_nth (sftp_panel.queue, i)->data;

    gchar status[512];

    if (pTi->state == TR_IN_PROGRESS) {
      sprintf (status, "%s", pTi->action == SFTP_ACTION_UPLOAD ? "Uploading" : "Downloading");
    }
    else {
      //strcat (status, ":\n");
      //strcat (status, pTi->errorDesc);
      strcpy (status, getTransferStatusDesc (pTi->state));
    }

    //gtk_label_set_text (GTK_LABEL(pTi->label_status), status);
    //gtk_label_set_text (GTK_LABEL(pTi->label_from), pTi->source);
  
#if (GTK_MAJOR_VERSION == 3)
    //gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR(pTi->progress_transfer), TRUE);
#endif

    gboolean sensitive;

    sensitive = pTi->state <= TR_PAUSED ? TRUE : FALSE;
    
    //gtk_widget_set_sensitive (pTi->button_cancel, sensitive);


    time_t current_time;
    double elapsed;
    char speed[32], time_left[16];
    uint64_t seconds_left;

    time (&current_time); 
    
    strcpy (time_left, "00:00:00");
    strcpy (speed, "0 MB/sec.");

    if (pTi->state == TR_IN_PROGRESS)
      {
        elapsed = difftime (current_time, pTi->start_time);
        sprintf (speed, "%.1f MB/sec.", elapsed > 0.0 ? (float) pTi->worked / (elapsed * 1024.0 * 1024.0) : 0.0);

        if (pTi->sourceIsDir) {
          strcpy (time_left, "unknown");
          //gtk_progress_bar_pulse (GTK_PROGRESS_BAR(pTi->progress_transfer));
        }
        else {
          seconds_left = ((elapsed * pTi->size) / pTi->worked) - elapsed;
          seconds_to_hhmmdd (seconds_left, time_left);
        }
        
        //gtk_label_set_text (GTK_LABEL(pTi->label_speed), speed);
        //gtk_label_set_text (GTK_LABEL(pTi->label_time_left), time_left);
      }
    
    // Show progress or 100% if not directory

    //if (!pTi->sourceIsDir || pTi->state == TR_COMPLETED)
    //  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(pTi->progress_transfer), pTi->size > 0 ? (float) pTi->worked/pTi->size : 0.0);

    sprintf (tmp_s, "%lld", pTi->size);
/*
    int progress;

    if (pTi->state != TR_READY)
      progress = pTi->size > 0 ? (guint)(((double)pTi->worked/(double)pTi->size)*(double)100.0) : 100;
    else
      progress = 0;

    log_debug ("%s %lld/%lld (%d%%)\n", pTi->shortenedFilename, pTi->worked, pTi->size, progress);
*/

    char totalSize[1024], tmpWorked[64];

    if (pTi->sourceIsDir)
      strcpy (totalSize, "unknown");
    else
      bytes_to_human_readable (pTi->size, totalSize);

    log_debug ("Updating %s\n", pTi->shortenedFilename);

    GtkTreeIter iter;
    gtk_tree_model_iter_nth_child (tm_transfer, &iter, NULL, i);

    gtk_list_store_set (ls_transfer, &/*pTi->*/iter, 
                        TR_COL_STATUS, status,
                        //TR_COL_PROGRESS, progress,
                        TR_COL_TOTAL_SIZE, totalSize, 
                        TR_COL_TRANSFERRED, bytes_to_human_readable (pTi->worked, tmpWorked)/*(guint)pTi->worked*/, 
                        TR_COL_SPEED, speed,
                        TR_COL_ETA, time_left,
                        -1);

  }

  //pthread_mutex_unlock(&sftp_panel.mutexQueue);
  lockSFTPQueue (__func__, FALSE);
}

void
view_transfer_window ()
{
  int i;
  GtkWidget *transfer_tree_view = 0;
  GtkBuilder *builder; 
  GError *error=NULL;
  char ui[256], tmp[512];

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/transfer.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }

  // Create the window
  transferQueueWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    
  gtk_window_set_modal (GTK_WINDOW (transferQueueWindow), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (transferQueueWindow), GTK_WINDOW (main_window));
  gtk_window_set_position (GTK_WINDOW (transferQueueWindow), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_title (GTK_WINDOW (transferQueueWindow), _("Uploads and downloads"));

  g_signal_connect (transferQueueWindow, "delete_event", G_CALLBACK (transfer_window_delete_event_cb), NULL);
  //gtk_widget_show_all (p_qlv->scrolled_window);

  GtkWidget *vbox_main = GTK_WIDGET (gtk_builder_get_object (builder, "vbox_main"));

  // Scrolled window
  //GtkWindow *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  GtkWindow *scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "scrolled_window"));
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_ETCHED_IN);
  //transferAdjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled_window));
  //g_signal_connect (transferAdjustment, "changed", G_CALLBACK (transfer_adj_changed_cb), NULL);

  transfer_tree_view = create_transfer_window_tree_view ();

  refresh_transfer_list_store ();

  if (transfer_tree_view)
#if GTK_CHECK_VERSION(3, 8, 0)
    // Since 3.8 will automatically add a GtkViewport if the child doesn't implement GtkScrollable
    gtk_container_add (GTK_CONTAINER (scrolled_window), transfer_tree_view);
#else
    gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window), transfer_tree_view);
#endif

  // Info
  GtkWidget *button_info = GTK_WIDGET (gtk_builder_get_object (builder, "button_info"));
#if GTK_CHECK_VERSION(3, 10, 0)
  gtk_button_set_image (GTK_BUTTON(button_info), gtk_image_new_from_icon_name ("dialog-information", GTK_ICON_SIZE_LARGE_TOOLBAR));
#else
  gtk_button_set_image (GTK_BUTTON(button_info), gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_LARGE_TOOLBAR));
#endif
  g_signal_connect (G_OBJECT (button_info), "clicked", G_CALLBACK (transfer_details_cb), NULL);

  // Clear
  GtkWidget *button_clear = GTK_WIDGET (gtk_builder_get_object (builder, "button_clear"));
#if GTK_CHECK_VERSION(3, 10, 0)
  gtk_button_set_image (GTK_BUTTON(button_clear), gtk_image_new_from_icon_name ("edit-clear", GTK_ICON_SIZE_LARGE_TOOLBAR));
#else
  gtk_button_set_image (GTK_BUTTON(button_clear), gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_LARGE_TOOLBAR));
#endif
  g_signal_connect (G_OBJECT (button_clear), "clicked", G_CALLBACK (transfer_remove_completed_cb), NULL);

  // Cancel
  GtkWidget *button_cancel = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
#if GTK_CHECK_VERSION(3, 10, 0)
  gtk_button_set_image (GTK_BUTTON(button_cancel), gtk_image_new_from_icon_name ("process-stop", GTK_ICON_SIZE_LARGE_TOOLBAR));
#else
  gtk_button_set_image (GTK_BUTTON(button_cancel), gtk_image_new_from_stock (GTK_STOCK_CANCEL, GTK_ICON_SIZE_LARGE_TOOLBAR));
#endif
  g_signal_connect (G_OBJECT (button_cancel), "clicked", G_CALLBACK (transfer_cancel_cb), NULL);

  // Close
  GtkWidget *button_close = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
#if GTK_CHECK_VERSION(3, 10, 0)
  gtk_button_set_image (GTK_BUTTON(button_close), gtk_image_new_from_icon_name ("window-close", GTK_ICON_SIZE_BUTTON));
#else
  gtk_button_set_image (GTK_BUTTON(button_close), gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON));
#endif
  g_signal_connect (G_OBJECT (button_close), "clicked", G_CALLBACK (transfer_window_delete_event_cb), NULL);

  // Add main vbox to the window
  gtk_container_add (GTK_CONTAINER (transferQueueWindow), vbox_main);
  
  gtk_window_set_position (GTK_WINDOW (transferQueueWindow), GTK_WIN_POS_CENTER_ON_PARENT);

  GdkScreen *screen;
  screen = gtk_window_get_screen (GTK_WINDOW (main_window));
  gtk_widget_set_size_request (GTK_WIDGET (transferQueueWindow), gdk_screen_get_height (screen)/1.5, gdk_screen_get_height (screen)/1.8);

  gtk_widget_show_all (transferQueueWindow);

  time_t current_time, last_update;

  // adjValue = gtk_adjustment_get_value (transferAdjustment);
  gTransferWindow = TRUE;
  gForceRefresh = TRUE;
  gRefreshing = FALSE;
  last_update = 0; 

  while (gTransferWindow)
    {
      time (&current_time); 

      if (gForceRefresh || difftime (current_time, last_update) > 1) {
        GList *children, *iter;

        gRefreshing = TRUE;
/*
        children = gtk_container_get_children(GTK_CONTAINER(scrolled_window));

        for (iter = children; iter != NULL; iter = g_list_next(iter))
          gtk_widget_destroy(GTK_WIDGET(iter->data));

        g_list_free(children);

        grid = transfer_window_refresh ();

        if (grid)
          gtk_container_add (GTK_CONTAINER (scrolled_window), grid);
*/
        transfer_window_refresh ();

        time (&last_update);

        if (gForceRefresh)
          gForceRefresh = FALSE;

        gRefreshing = FALSE;
      }

      //gtk_adjustment_set_value (transferAdjustment, adjValue);

      while (gtk_events_pending ())
        gtk_main_iteration ();

      //adjValue = gtk_adjustment_get_value (transferAdjustment);
      // Prevent from 100% cpu usage
      g_usleep (3000);
    }

  //g_object_unref (G_OBJECT (builder));
  gtk_widget_destroy (transferQueueWindow);
  transferQueueWindow = NULL;

  update_statusbar ();
}


