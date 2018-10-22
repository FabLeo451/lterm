
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
 * @file gui.c
 * @brief Graphic user interface functions
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libssh/libssh.h> 
#include <vte/vte.h>
#include <openssl/opensslv.h>
#include <openssl/md5.h>
#include <sys/utsname.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "main.h"
#include "protocol.h"
#include "connection.h"
#include "preferences.h"
#include "profile.h"
#include "config.h"
#include "gui.h"
#include "utils.h"
#include "sftp-panel.h"
#include "terminal.h"
#include "connection_list.h"
#include "transfer_window.h"

#ifndef MAC_INTEGRATION
#include <gdk/gdkx.h>
#endif

#ifdef MAC_INTEGRATION
#  include <gtkosxapplication.h>
#  define ACCEL_SEARCH_ENTRY "Search connection... ⌘K"
#  define SHORTCUT_COPY "<ctrl>C"
#  define SHORTCUT_PASTE "<ctrl>V"
#  define SHORTCUT_FIND "<ctrl>F"
#  define SHORTCUT_FIND_NEXT "<ctrl>G"
#  define SHORTCUT_QUIT "<ctrl>Q"
#else
#  define ACCEL_SEARCH_ENTRY "Search connection... <Ctrl+K>"
#  define SHORTCUT_COPY "<shift><ctrl>C"
#  define SHORTCUT_PASTE "<shift><ctrl>V"
#  define SHORTCUT_FIND "<shift><ctrl>F"
#  define SHORTCUT_FIND_NEXT "<shift><ctrl>G"
#  define SHORTCUT_QUIT "<Alt>X"
#endif

#define DEFAULT_WORD_CHARS "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

#define USERCHARS "-[:alnum:]"
#define USERCHARS_CLASS "[" USERCHARS "]"
#define PASSCHARS_CLASS "[-[:alnum:]\\Q,?;.:/!%$^*&~\"#'\\E]"
#define HOSTCHARS_CLASS "[-[:alnum:]]"
#define HOST HOSTCHARS_CLASS "+(\\." HOSTCHARS_CLASS "+)*"
#define PORT "(?:\\:[[:digit:]]{1,5})?"
#define PATHCHARS_CLASS "[-[:alnum:]\\Q_$.+!*,:;@&=?/~#%\\E]"
#define PATHTERM_CLASS "[^\\Q]'.:}>) \t\r\n,\"\\E]"
#define SCHEME "(?:news:|telnet:|nntp:|file:\\/|https?:|ftps?:|sftp:|webcal:)"
#define USERPASS USERCHARS_CLASS "+(?:" PASSCHARS_CLASS "+)?"
#define URLPATH   "(?:(/"PATHCHARS_CLASS"+(?:[(]"PATHCHARS_CLASS"*[)])*"PATHCHARS_CLASS"*)*"PATHTERM_CLASS")?"

GRegex *ip_regexpr;

extern Globals globals;
extern Prefs prefs;
extern struct Protocol_List g_prot_list;
extern struct Connection_List conn_list;
extern struct ProfileList g_profile_list;
extern struct GroupTree g_groups;
extern struct SFTP_Panel sftp_panel;

//char *auth_state_desc[] = { "AUTH_STATE_NOT_LOGGED", "AUTH_STATE_GOT_USER", "AUTH_STATE_GOT_PASSWORD", "AUTH_STATE_LOGGED" };

int ifr_index_first = 0;
int ifr_index_insert = 1;
struct Iteration_Function_Request ifr[ITERATION_MAX];

GtkWidget *main_window;
GtkWidget *hpaned;

GtkUIManager *ui_manager;
GtkWidget *menubar, *main_toolbar;
GtkActionGroup *action_group;
GtkWidget *search_entry;
GtkEntryCompletion *completion;
GtkWidget *toolbar_recent_menu;
GtkWidget *statusbar, *sb_msg, *sb_protocol, *sb_transfer, *sb_enc;       /* statusbar */
GtkWidget *notebook;
GtkWidget *notebook_sidebar;
//GtkWidget *align_sidebar;

void update_statusbar();

GdkScreen *g_screen;
//GdkVisual *visual;

/* pointer to the list of open connections or NULL if there's no open connection */
GList *connection_tab_list;
struct ConnectionTab *p_current_connection_tab;
struct QuickLaunchWindow g_quick_launch_window;

GList *g_recent_connections_list; /* mantain a list of connection structs */
GList *g_recent_sessions_list;
GtkActionEntry recent_entries[MAX_RECENT];
GtkActionGroup *recents_action_group;
GtkRecentManager *g_recent_manager;
int recents_handler_id;
int recents_menu_ui_id;

GtkActionGroup *profile_action_group;
int profile_menu_ui_id;

/* number of rows and columns before maximization */
int prev_rows;
int prev_columns;
//int g_window_resized = 0;

int switch_tab_enabled = 1;
//int g_refresh_qlw_tree_view = 0;

char *g_vte_selected_text = NULL;

struct Match g_match;

// Cluster
enum { COLUMN_CLUSTER_TERM_SELECTED, COLUMN_CLUSTER_TERM_NAME, N_CLUSTER_COLUMNS };
GtkListStore *list_store_cluster;
typedef struct TabSelection {
  struct ConnectionTab *pTab;
  gint selected;
} STabSelection;
GArray *tabSelectionArray;

GtkActionEntry main_menu_items[] = {
  { "ConnectionMenu", NULL, N_("_Connection") },
  { "Log on", "_Connect", N_("Log _on"), "<ctrl>L", "Log on", G_CALLBACK (connection_log_on) },
  { "LogOnRecent", NULL, N_("Log on _recent") }, /* not used at the moment */
  { "Log off", NULL, N_("Log o_ff"), NULL, NULL, G_CALLBACK (connection_log_off) },
  { "Duplicate", MY_STOCK_DUPLICATE, N_("_Duplicate"), "<shift><ctrl>D", "Duplicate connection", G_CALLBACK (connection_duplicate) },
  { "Edit protocols", NULL, N_("_Edit protocols"), NULL, NULL, G_CALLBACK (connection_edit_protocols) },
  { "Open terminal", MY_STOCK_TERMINAL, N_("Open _terminal"), "<ctrl>T", "Open terminal", G_CALLBACK (connection_new_terminal) },
  { "ExportMenu", NULL, N_("_Export bookmarks") },
  { "ExportLTerm", NULL, N_("To _lterm"), NULL, NULL, G_CALLBACK (connection_export_lterm) },
  { "ExportMobaXterm", NULL, N_("To _MobaXterm"), NULL, NULL, G_CALLBACK (connection_export_MobaXterm) },
  { "ExportPutty", NULL, N_("To _Putty"), NULL, NULL, G_CALLBACK (connection_export_Putty) },
  { "ExportCSV", NULL, N_("To _CSV"), NULL, NULL, G_CALLBACK (connection_export_CSV) },
  { "ImportMenu", NULL, N_("_Import bookmarks") },
  { "ImportLTerm", NULL, N_("From _lterm"), NULL, NULL, G_CALLBACK (connection_import_lterm) },
  { "ImportMobaXterm", NULL, N_("From _MobaXterm"), NULL, NULL, G_CALLBACK (connection_import_MobaXterm) },
  { "ImportPutty", NULL, N_("From _Putty"), NULL, NULL, G_CALLBACK (connection_import_Putty) },
  { "Close tab", "_Close", N_("Close tab"), "<ctrl>W", NULL, G_CALLBACK (connection_close_tab) },
  { "Quit", "_Quit", N_("_Quit"), SHORTCUT_QUIT, NULL, G_CALLBACK (application_quit) },

  { "EditMenu", NULL, N_("_Edit") },
  { "Copy", "edit-copy", N_("_Copy"), SHORTCUT_COPY, "Copy", G_CALLBACK (edit_copy) },
  { "Paste", "edit-paste", N_("_Paste"), SHORTCUT_PASTE, "Paste", G_CALLBACK (edit_paste) },
  { "Copy and paste", NULL, N_("C_opy and Paste"), NULL, NULL, G_CALLBACK (edit_copy_and_paste) },
  { "Find", "edit-find", N_("_Find"), SHORTCUT_FIND, "Find", G_CALLBACK (edit_find) },
  { "FindNext", NULL, N_("Find _next"), SHORTCUT_FIND_NEXT, NULL, G_CALLBACK (terminal_find_next) },
  { "FindPrevious", NULL, N_("Find pre_vious"), "<ctrl><alt>G", NULL, G_CALLBACK (terminal_find_previous) },
  { "Select all", NULL, N_("_Select all"), NULL, NULL, G_CALLBACK (edit_select_all) },
  { "EditCurrentProfile", NULL, N_("E_dit current profile"), NULL, NULL, G_CALLBACK (edit_current_profile) },
  { "Preferences", MY_STOCK_PREFERENCES, N_("P_references"), NULL, "Preferences", G_CALLBACK (show_preferences) },

  { "ViewMenu", NULL, N_("_View") },
  { "TransferWindow", MY_STOCK_TRANSFERS, N_("_Transfer window"), NULL, "Transfer window", G_CALLBACK (view_transfer_window) },
  { "Fullscreen", "view-fullscreen", N_("_Fullscreen"), "F11", "Fullscreen", G_CALLBACK (view_fullscreen) },
  { "Go back", "go-previous", N_("Go _back"), "<ctrl>Left", "Go back", G_CALLBACK (view_go_back) },
  { "Go forward", "go-next", N_("Go _forward"), "<ctrl>Right", "Go forward", G_CALLBACK (view_go_forward) },
  { "Zoom in", "zoom-in", N_("Zoom _in"), "<control>plus", "Zoom in", G_CALLBACK (zoom_in) },
  { "Zoom out", "zoom-out", N_("Zoom _out"), "<control>minus", "Zoom out", G_CALLBACK (zoom_out) },
  { "Zoom 100", "zoom-fit-best", N_("Zoom _100"), "<control>0", "Zoom 100", G_CALLBACK (zoom_100) },

  { "TerminalMenu", NULL, N_("_Terminal") },
  { "ProfileMenu", NULL, N_("Change profile") },
  { "CharacterEncodingMenu", NULL, N_("Character Encoding") },
  { "Reset", NULL, N_("_Reset"), NULL, NULL, G_CALLBACK (terminal_reset) },
  { "DetachRight", MY_STOCK_SPLIT_H, N_("_Detach tab to the right"), "<control>P", "Detach tab to the right", G_CALLBACK (terminal_detach_right) },
  { "DetachDown", MY_STOCK_SPLIT_V, N_("Detach tab _down"), NULL, "Detach tab down", G_CALLBACK (terminal_detach_down) },
  { "AttachCurrent", NULL, N_("_Attach to main group"), NULL, NULL, G_CALLBACK (terminal_attach_current_to_main) },
  { "RegroupAll", MY_STOCK_REGROUP, N_("_Regroup all tabs"), NULL, "Regroup all tabs", G_CALLBACK (terminal_regroup_all) },
  { "Cluster", MY_STOCK_CLUSTER, N_("Send command to cluster"), NULL, "Send command to cluster", G_CALLBACK (terminal_cluster) },

  { "SessionMenu", NULL, N_("_Session") },
  { "Load session", "document-open", N_("Load session"), NULL, NULL, G_CALLBACK (session_load) },
  { "Save session", "document-save", N_("Save session"), NULL, NULL, G_CALLBACK (session_save) },
  { "SessionRecent", NULL, N_("Session _recent") },

  { "HelpMenu", NULL, N_("_Help") },
  { "Home page", "go-home", N_("_Home page"), "F1", "Home page", G_CALLBACK (help_home_page) },
  { "About", "help-about", N_("_About"), NULL, "About", G_CALLBACK (Info) }
  
#ifdef DEBUG
  ,
  { "Debug", "_Yes", N_("_Debug"), NULL, NULL, G_CALLBACK (Debug) }
#endif
};

static GtkToggleActionEntry toggle_entries[] = {
  { "Toolbar", NULL, N_("_Toolbar"), NULL, NULL, G_CALLBACK (view_toolbar) },
  { "Statusbar", NULL, N_("_Statusbar"), NULL, NULL, G_CALLBACK (view_statusbar) },
  { "Sidebar", MY_STOCK_SIDEBAR, N_("S_idebar"), "F9", "Show/hide sidebar", G_CALLBACK (view_sidebar) }
};

const gchar ui_main_desc[] = 
  "<ui>"
  "  <menubar name='MainMenu'>"
  "    <menu action='ConnectionMenu'>"
  "      <menuitem action='Log on' />"
  //"      <menu action='LogOnRecent' />" /* filled later */
  "      <menuitem action='Log off' />"
  "      <menuitem action='Duplicate' />"
  "      <separator />"
  "      <menuitem action='Open terminal' />"
  "      <separator />"
  "      <menuitem action='Edit protocols' />"
  "      <separator />"
  "      <menu action='ExportMenu'>"
  "        <menuitem action='ExportLTerm' />"
  "        <menuitem action='ExportMobaXterm' />"
  "        <menuitem action='ExportPutty' />"
  "        <menuitem action='ExportCSV' />"
  "      </menu>"
  "      <menu action='ImportMenu'>"
  "        <menuitem action='ImportLTerm' />"
  "        <menuitem action='ImportMobaXterm' />"
  "        <menuitem action='ImportPutty' />"
  "      </menu>"
  "      <separator />"
  "      <menuitem action='Close tab' />"
  "      <separator />"
  "      <menuitem action='Quit' />"
  "    </menu>"
  "    <menu action='EditMenu'>"
  "      <menuitem action='Copy' />"
  "      <menuitem action='Paste' />"
  "      <menuitem action='Copy and paste' />"
  "      <separator />"
  "      <menuitem action='Find' />"
  "      <menuitem action='FindNext' />"
  "      <menuitem action='FindPrevious' />"
  "      <separator />"
  "      <menuitem action='Select all' />"
  "      <separator />"
  "      <menuitem action='EditCurrentProfile' />"
  "      <separator />"
  "      <menuitem action='Preferences' />"
  "    </menu>"
  "    <menu action='ViewMenu'>"
  "      <menuitem action='TransferWindow' />"
  "      <separator />"
  "      <menuitem action='Toolbar' />"
  "      <menuitem action='Statusbar' />"
  "      <menuitem action='Sidebar' />"
  "      <separator />"
  "      <menuitem action='Fullscreen' />"
  "      <separator />"
  "      <menuitem action='Go back' />"
  "      <menuitem action='Go forward' />"
  "      <separator />"
  "      <menuitem action='Zoom in' />"
  "      <menuitem action='Zoom out' />"
  "      <menuitem action='Zoom 100' />"
  "    </menu>"
  "    <menu action='TerminalMenu'>"
  "      <menu action='ProfileMenu' />" /* filled later */
  "      <menu action='CharacterEncodingMenu' />" /* filled later */
  "      <separator />"
  "      <menuitem action='Reset' />"
  "      <separator />"
  "      <menuitem action='DetachRight' />"
  "      <menuitem action='DetachDown' />"
  "      <menuitem action='AttachCurrent' />"
  "      <menuitem action='RegroupAll' />"
  "      <separator />"
  "      <menuitem action='Cluster' />"
  "    </menu>"
  
  "    <menu action='SessionMenu'>"
  "      <menuitem action='Load session' />"
  "      <menuitem action='Save session' />"
  "      <separator />"
  "      <placeholder name='SessionRecentsPlaceholder'>"
  "        <separator/>"
  "      </placeholder>"
  "    </menu>"
  "    <menu action='HelpMenu'>"
  "      <menuitem action='Home page' />"
  "      <separator />"
  "      <menuitem action='About' />"
  "    </menu>"
  "  </menubar>"
  "  <toolbar name='MainToolbar'>"
  "    <toolitem action='Open terminal'/>"
  "    <toolitem name='Duplicate session' action='Duplicate'/>"
  "    <separator />"
  "    <toolitem action='Copy'/>"
  "    <toolitem action='Paste'/>"
  "    <toolitem action='Find'/>"
  "    <separator />"
  "    <separator />"
  "    <toolitem action='DetachRight'/>"
  "    <toolitem action='DetachDown'/>"
  "    <toolitem action='RegroupAll'/>"
  "    <separator />"
  "    <toolitem action='Cluster'/>"
  "    <separator />"
  "    <toolitem action='Fullscreen'/>"
  "    <separator />"
  "    <toolitem action='Zoom in'/>"
  "    <toolitem action='Zoom out'/>"
  "    <toolitem action='Zoom 100'/>"
  "    <separator />"
  "    <toolitem action='Sidebar'/>"
  "    <separator />"
  "    <toolitem action='TransferWindow'/>"
  "    <separator />"
  "    <toolitem action='Preferences'/>"
  "    <toolitem action='Zoom 100'/>"
  "    <toolitem action='About'/>"
#ifdef DEBUG
  "    <separator />"
  "    <toolitem action='Debug'/>"
#endif
  "  </toolbar>"
  "</ui>";

GtkActionEntry popup_menu_items[] = {
  { "Copy", "_Copy", N_("_Copy"), "<shift><ctrl>C", NULL, G_CALLBACK (edit_copy) },
  { "Paste", "_Paste", N_("_Paste"), "<shift><ctrl>V", NULL, G_CALLBACK (edit_paste) },
  { "Copy and paste", NULL, N_("C_opy and paste"), NULL, NULL, G_CALLBACK (edit_copy_and_paste) },
  { "PasteHost", NULL, N_("Paste host") },
  //{ "Download files", NULL, N_("Download"), NULL, NULL, G_CALLBACK (sftp_download_files) },
  { "Select all", NULL, N_("_Select all"), NULL, NULL, G_CALLBACK (edit_select_all) },
  { "ConnectHost", "_Connect", N_("Connect to this host"), NULL, NULL, G_CALLBACK (hyperlink_connect_host) },
  { "EditHost", "gtk-edit", N_("Edit this host"), NULL, NULL, G_CALLBACK (hyperlink_edit_host) },
  { "AddHost", "list-add", N_("Bookmark this host"), NULL, NULL, G_CALLBACK (hyperlink_add_host) }
};

const gchar *ui_popup_desc =
  "<ui>"
  "  <popup name='TermPopupMenu' accelerators='true'>"
  "    <menuitem action='Copy'/>"
  "    <menuitem action='Paste'/>"
  "    <menuitem action='Copy and paste'/>"
  "    <menu action='PasteHost' />" /* filled later */
  "    <separator />"
  //"    <menuitem action='Download files'/>"
  //"    <separator />"
  "    <menuitem action='Select all' />"
  "  </popup>"
  "</ui>";

struct EncodingEntry
  {
    char name[64];
    char id[32];
  };

struct EncodingEntry enc_array[] =
  {
    { "UTF-8", "UTF-8" },
    { "ISO-8859-1 (Latin 1, West European)", "ISO-8859-1" },
    { "ISO-8859-2 (Latin 2, East European)", "ISO-8859-2" },
    { "ISO-8859-3 (Latin 3, South European)", "ISO-8859-3" },
    { "ISO-8859-4 (Latin 4, North European)", "ISO-8859-4" },
    { "ISO-8859-5 (Cyrillic)", "ISO-8859-5" },
    { "ISO-8859-6 (Arabic)", "ISO-8859-6" },
    { "ISO-8859-7 (Greek)", "ISO-8859-7" },
    { "ISO-8859-8 (Hebrew)", "ISO-8859-8" },
    { "ISO-8859-9 (Turkish)", "ISO-8859-9" },
    { "ISO-8859-10 (Nordic)", "ISO-8859-10" },
    { "ISO-8859-11 (Thai)", "ISO-8859-11" },
    { "ISO-8859-13 (Latin 7)", "ISO-8859-13" },
    { "ISO-8859-14 (Latin 8)", "ISO-8859-14" },
    { "ISO-8859-15 (Latin 9, West European with EURO)", "ISO-8859-15" },
    { "GB2312 (Chinese)", "GB2312" },
    { "BIG5 (Chinese)", "BIG5" },
    { "Shift_JIS (Japanese)", "Shift_JIS" },
    { "KSC (Korean)", "KSC" },
    { "KOI8-R (Russian)", "KOI8-R" },
    { "CP1251 (Russian)", "CP1251" },
    { "EUC-JP (Japanese)", "EUC-JP" }
  };

void
ifr_init ()
{
  int i;

  for (i=0; i<ITERATION_MAX; i++)
    memset (&ifr[i], 0, sizeof (struct Iteration_Function_Request));

  ifr_index_first = 0;
  ifr_index_insert = 0;
}

void
ifr_add (int function_id, void *user_data)
{
  /* Check if all slots are busy */
  if (ifr[ifr_index_insert].id != 0)
    return;
  
  /* Avoid adjacent duplicates */
  if (ifr[ifr_index_first].id == function_id)
    return;

  log_debug ("ifr[%d] = %d\n", ifr_index_insert, function_id);

  ifr[ifr_index_insert].id = function_id;
  ifr[ifr_index_insert].user_data = user_data;

  ifr_index_insert = (ifr_index_insert + 1) % ITERATION_MAX;
}

int
ifr_get (struct Iteration_Function_Request *dest)
{
  if (ifr[ifr_index_first].id == 0)
    return (0);

  dest->id = ifr[ifr_index_first].id;
  dest->user_data = ifr[ifr_index_first].user_data;

  ifr[ifr_index_first].id = 0;

  ifr_index_first = (ifr_index_first + 1) % ITERATION_MAX;

  return (1);
}

#ifdef DEBUG
void
ifr_dump ()
{
  int i;

  for (i=0; i<ITERATION_MAX; i++)
    {
      printf ("ifr[%d] = %d", i, ifr[i].id);

      if (i == ifr_index_insert)
        printf ("<- insert");

      if (i == ifr_index_first)
        printf ("<- first");

      printf ("\n");
    }
}
#endif

void
msgbox_error (const char *fmt, ...)
{
  GtkWidget *dialog;
  va_list ap;
  char msg[2048];

  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);

  dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, msg, 0);

  gtk_window_set_title (GTK_WINDOW (dialog), PACKAGE);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  
  log_write ("%s\n", msg);
}

void
msgbox_info (const char *fmt, ...)
{
  GtkWidget *dialog;
  va_list ap;
  char msg[1024];

  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);

  dialog = gtk_message_dialog_new (GTK_WINDOW (main_window),
                            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, msg, 0);
  gtk_window_set_title (GTK_WINDOW (dialog), PACKAGE);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

gint
msgbox_yes_no (const char *fmt, ...)
{
  GtkWidget *dialog;
  va_list ap;
  char msg[1024];

  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);

  dialog = gtk_message_dialog_new (GTK_WINDOW (main_window),
                            GTK_DIALOG_DESTROY_WITH_PARENT,
                            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, msg, 0);
  gint result = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  return result;
}

/**
 * child_exit() - handler of SIGCHLD
 */
void
child_exit ()
{
  int pid, status;

  pid = wait (&status);

#ifdef DEBUG
  printf ("child_exit() : process ended : %d\n", pid);
#endif

  if (status > 0)
    printf ("process %d terminated with code %d (%s)\n", pid, status, strerror (status));
}

void
segv_handler(int signum)
{
  log_write("!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  //log_write("Critical error. Received signal %d\n", signum);
  msgbox_error ("Received signal %d.\n"
                "Sorry, this is a critical error and the program will be killed.\n"
                "Take a look on the website about what to do in cases like this.", 
                signum);

  log_write ("Removing all watch file descriptors...\n");
  sftp_panel_mirror_file_clear (NULL, 1);

  signal(signum, SIG_DFL);
  kill(getpid(), signum);
}

void
tabInitConnection(SConnectionTab *pConn)
{
  //pConn->connected = 0; // DEPRECATED: use connectionStatus
  //pConn->logged = 0; // DEPRECATED: use flags
  tabSetConnectionStatus (pConn, TAB_CONN_STATUS_DISCONNECTED);
  pConn->auth_state = AUTH_STATE_NOT_LOGGED;
  pConn->auth_attempt = 0;
  pConn->changes_count = 0;
//  pConn->status = TAB_STATUS_NORMAL;
  pConn->flags = 0;
}

char *connectionStatusDesc[] = { "disconnected", "connecting", "connected" };

char *tabGetConnectionStatusDesc(int status) {
  return (connectionStatusDesc[status]);
}

void tabSetConnectionStatus(SConnectionTab *pConn, int status)
{
  pConn->connectionStatus = status;
}

int tabGetConnectionStatus(SConnectionTab *pConn)
{
  return (pConn->connectionStatus);
}

int tabIsConnected(SConnectionTab *pConn)
{
  return (pConn->connectionStatus >= TAB_CONN_STATUS_CONNECTED);
}

void tabSetFlag (SConnectionTab *pConn, unsigned int bitmask) {
  pConn->flags |= bitmask;
}

void tabResetFlag (SConnectionTab *pConn, unsigned int bitmask) {
  pConn->flags = pConn->flags &= ~bitmask;
}

unsigned int tabGetFlag (SConnectionTab *pConn, unsigned int bitmask) {
  return (pConn->flags & bitmask);
}

void
set_title (char *user_s)
{
  char title[256];
  char appname[256];

  sprintf (appname, "%s %s", PACKAGE, VERSION);

  if (user_s)
    sprintf (title, "%s - %s", user_s, appname);
  else
    strcpy (title, appname);

  gtk_window_set_title (GTK_WINDOW (main_window), title);
}

/**
 * query_value() - open a dialog asking for a parameter value
 * @return length of value or -1 if user cancelled operation
 */
int
query_value (char *title, char *labeltext, char *default_value, char *buffer, int type)
{
  int ret;
  char imagefile[256];
  GtkWidget *dialog;
  GtkWidget *p_label;
  GtkWidget *user_entry;
  GtkWidget *l_align;
  

  dialog = gtk_dialog_new_with_buttons (title, NULL, 0,
                                        "_Cancel",
                                        GTK_RESPONSE_CANCEL,
                                        "_Ok",
                                        GTK_RESPONSE_OK,
                                        NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  user_entry = gtk_entry_new ();
  gtk_entry_set_max_length (GTK_ENTRY (user_entry), 512);
  gtk_entry_set_text (GTK_ENTRY (user_entry), default_value);
  gtk_entry_set_activates_default (GTK_ENTRY (user_entry), TRUE);
  gtk_entry_set_visibility (GTK_ENTRY (user_entry), type == QUERY_PASSWORD ? FALSE : TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));

  p_label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (p_label), labeltext);

  l_align = gtk_alignment_new (0, 0, 0, 0);
  gtk_container_add (GTK_CONTAINER (l_align), p_label);
  gtk_widget_show (l_align);

  GtkWidget *main_hbox;
  GtkWidget *text_vbox;
  
#if (GTK_MAJOR_VERSION == 2)
  main_hbox = gtk_hbox_new (FALSE, 5);
  text_vbox = gtk_vbox_new (FALSE, 5);
#else
  main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
  text_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
#endif

  /* image */

  GdkScreen *screen;
  gint w_width, w_height;

  switch (type) {
    case QUERY_USER:
      strcpy (imagefile, "/user.png");
      break;
    case QUERY_PASSWORD:
      strcpy (imagefile, "/keys-64.png");
      break;
    case QUERY_FILE_NEW:
      strcpy (imagefile, "/file_new.png");
      break;
    case QUERY_FOLDER_NEW:
      strcpy (imagefile, "/folder_new.png");
      break;
    case QUERY_RENAME:
      strcpy (imagefile, "/rename.png");
      screen = gtk_window_get_screen (GTK_WINDOW (main_window));
      gtk_window_get_size (GTK_WINDOW (dialog), &w_width, &w_height);
      gtk_widget_set_size_request (GTK_WIDGET (dialog), gdk_screen_get_width (screen)/1.8, w_height);
      break;
    default:
      strcpy (imagefile, "");
      break;
  }
      
  if (imagefile[0])
    {
      gtk_box_pack_start (GTK_BOX (main_hbox), gtk_image_new_from_file (g_strconcat (globals.img_dir, imagefile, NULL)), FALSE, TRUE, 2);
    }
  else
    gtk_container_set_border_width (GTK_CONTAINER (main_hbox), 0);

  /* text and field */

  gtk_box_pack_start (GTK_BOX (text_vbox), l_align, FALSE, TRUE, 5);
  gtk_box_pack_start (GTK_BOX (text_vbox), user_entry, FALSE, TRUE, 5);

  gtk_box_pack_start (GTK_BOX (main_hbox), text_vbox, TRUE, TRUE, 5);

  gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), main_hbox);

/*
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), gtk_image_new_from_file (g_strconcat (globals.img_dir, "/keys-64.png", NULL)));
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), l_align);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), user_entry);
*/

  gtk_widget_show_all (dialog);

  gtk_widget_grab_focus (user_entry);

  /* Start the dialog window */

  gint result = gtk_dialog_run (GTK_DIALOG (dialog));

  strcpy (buffer, "");

  if (result == GTK_RESPONSE_OK)
    {
      strcpy (buffer, gtk_entry_get_text (GTK_ENTRY (user_entry)));
      ret = strlen (buffer);
    }
  else
    ret = -1;

  gtk_widget_destroy (dialog);

  return (ret);
}

/**
 * expand_args() - expands command line parameters
 * @param[in] p_conn pointer to chosen connection (ip, port)
 * @param[in] args string to be expanded
 * @param[in] prefix a static string to put before expended
 * @param[out] dest string buffer receiving the expanded string
 * @return 0 if ok, 1 otherwise
 */
int
expand_args (struct Connection *p_conn, char *args, char *prefix, char *dest)
{
  int i, c, i_dest;
  int go_on;
  char expanded[256];
  char title[256];
  char label[256];

  i = 0;
  go_on = 1;

  strcpy (dest, prefix != NULL ? prefix : "");

  if (prefix)
    strcat (dest, " ");

  i_dest = strlen(dest);

  while (go_on > 0 && i<strlen (/*p_prot->*/args))
    {
      c = /*p_prot->*/args[i];

      if (c == '%')
        {
          i ++;
          c = /*p_prot->*/args[i];

          switch (c) {
            /* host */
            case 'h': strcpy (expanded, p_conn->host); break;

            /* port */
            case 'p': sprintf (expanded, "%d", p_conn->port); break;

            /* user */
            case 'u': 
              if (p_conn->auth_mode == CONN_AUTH_MODE_SAVE || p_conn->user[0])
                {
                  strcpy (expanded, p_conn->user);
                }
              else
                {
                  strcpy (title, "Log on");
                  sprintf (label, _("Enter user for <b>%s</b>:"), p_conn->name);
                  go_on = query_value (title, label, p_conn->last_user, expanded, QUERY_USER);
                  strcpy (p_conn->user, expanded);
                  //p_conn->auth_flags |= AUTH_STEP_USER;
                }

              rtrim (p_conn->user);

              if (p_conn->user[0] != 0)
                {
                  strcpy (p_conn->last_user, p_conn->user);

                  if (conn_update_last_user (p_conn->name, p_conn->last_user))
                    printf ("unable to update last user '%s' for connection %s\n", p_conn->user, p_conn->name);
                }

              break;

            /* password */
            case 'P': 
              if (p_conn->auth_mode == CONN_AUTH_MODE_SAVE || p_conn->password[0])
                {
#ifdef DEBUG
                  printf ("expand_args() : auth_password = '%s'\n", p_conn->password);
#endif
                  strcpy (expanded, p_conn->password);
                }
              else
                {
                  strcpy (title, "Log on");
                  sprintf (label, _("Enter password for <b>%s@%s</b>:"), p_conn->user, p_conn->name);
                  go_on = query_value (title, label, "", expanded, QUERY_PASSWORD);
                  strcpy (p_conn->password, expanded);
                  //p_conn->auth_flags |= AUTH_STEP_PASSWORD;
                }
              //else
              //  strcpy (expanded, p_conn->password);

              break;

            /* directory */
            case 'd': 
              if (p_conn->directory[0] == 0)
                {
                  strcpy (title, "Log on");
                  sprintf (label, _("Enter directory for <b>%s</b>:"), p_conn->name);
                  go_on = query_value (title, label, p_conn->directory, expanded, FALSE);
                }
              else
                strcpy (expanded, p_conn->directory);

              break;

            case '%': strcpy (expanded, "%"); break;
            default:
              strcpy (expanded, ""); break;
          }

          strcat (dest, expanded);
          i_dest += strlen (expanded);
        }
      else
        {
          dest[i_dest] = c;
          dest[i_dest + 1] = 0;
          i_dest ++;
        }
        
      i++;
    }
  
  return (go_on > 0 ? 0 : 1);
}

int
show_login_mask (struct ConnectionTab *p_conn_tab, struct SSH_Auth_Data *p_auth)
{
  GtkWidget *dialog;
  GtkBuilder *builder;
  GError *error=NULL;
  char ui[256], image_auth_filename[256];
  int result, rc = 0, i;

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/login.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  sprintf (image_auth_filename, "%s/keys-64.png", globals.img_dir);

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return (1);
    }

  GtkWidget *hbox_main = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_main"));
  GtkWidget *image_auth = GTK_WIDGET (gtk_builder_get_object (builder, "image_auth"));
  GtkWidget *label_name = GTK_WIDGET (gtk_builder_get_object (builder, "label_name"));
  GtkWidget *label_ip = GTK_WIDGET (gtk_builder_get_object (builder, "label_ip"));
  GtkWidget *entry_user = GTK_WIDGET (gtk_builder_get_object (builder, "entry_user"));
  GtkWidget *entry_password = GTK_WIDGET (gtk_builder_get_object (builder, "entry_password"));
  GtkWidget *check_enable_sftp = GTK_WIDGET (gtk_builder_get_object (builder, "check_enable_sftp"));

  gtk_label_set_text (GTK_LABEL (label_name), p_conn_tab->connection.name);
  gtk_label_set_text (GTK_LABEL (label_ip), p_conn_tab->connection.host);

  gtk_image_set_from_file (GTK_IMAGE(image_auth), image_auth_filename);

  if (p_conn_tab->connection.last_user[0])
    gtk_entry_set_text (GTK_ENTRY (entry_user), p_conn_tab->connection.last_user);

  /* Create dialog */

  dialog = gtk_dialog_new_with_buttons
                 (_("Authentication"), NULL,
                  GTK_DIALOG_MODAL, 
                  "_Cancel", GTK_RESPONSE_CANCEL,
                  "_Ok", GTK_RESPONSE_OK,
                  NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gtk_entry_set_activates_default (GTK_ENTRY (entry_user), TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (entry_password), TRUE);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), hbox_main);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
    
  if (p_auth->mode == CONN_AUTH_MODE_KEY) {
    GtkWidget *label_password = GTK_WIDGET (gtk_builder_get_object (builder, "label_password"));
    gtk_widget_hide (GTK_WIDGET (entry_password));
    gtk_widget_hide (GTK_WIDGET (label_password));
  }

run_dialog:

  result = gtk_dialog_run (GTK_DIALOG (dialog));

  if (result == GTK_RESPONSE_OK)
    {
      log_debug ("Clicked OK\n");

      lt_ssh_init (&p_conn_tab->ssh_info);

      strcpy (p_auth->user, gtk_entry_get_text (GTK_ENTRY (entry_user)));
      strcpy (p_auth->password, gtk_entry_get_text (GTK_ENTRY (entry_password)));
      strcpy (p_auth->host, p_conn_tab->connection.host);
      p_auth->port = p_conn_tab->connection.port;
      p_auth->sftp_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_enable_sftp)) ? 1 : 0;
      rc = 0;
    }
  else
    {
      rc = -1;
    }

  log_debug ("Closing window\n");

  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));

  log_debug ("Returning\n");

  while (gtk_events_pending ())
    gtk_main_iteration ();

  return (rc);
}

int
connection_tab_count (int type)
{
  int n = 0;
  GList *item;

  item = g_list_first (connection_tab_list);

  while (item)
    {
      if (tabIsConnected((struct ConnectionTab *) item->data)/*->connected*/ && ((struct ConnectionTab *) item->data)->type == type)
        n ++;

      item = g_list_next (item);
    }

  return (n);
}

struct ConnectionTab *
get_current_connection_tab ()
{
  return (p_current_connection_tab);
}

struct ConnectionTab *
get_connection_tab_from_child (GtkWidget *child)
{
  int i;
  GList *item;

  struct ConnectionTab *p_ct = NULL;

  for (i=0; i<g_list_length (connection_tab_list); i++)
    {
      item = g_list_nth (connection_tab_list, i);
      p_ct = (struct ConnectionTab *) item->data;

      if (p_ct->hbox_terminal == child)
        break;;
    }

  return (p_ct);
}

void
set_window_resized_all (int value)
{
  int i;
  GList *item;
  
  //log_debug ("value=%d\n", value);

  struct ConnectionTab *p_ct = NULL;

  for (i=0; i<g_list_length (connection_tab_list); i++)
    {
      item = g_list_nth (connection_tab_list, i);
      p_ct = (struct ConnectionTab *) item->data;

      p_ct->window_resized = value;
    }
}

void
connection_tab_close (struct ConnectionTab *p_ct)
{
  int page, retcode, can_close;
  GtkWidget *child;
  char prompt[256];

  //log_debug ("ptr = %d\n", (unsigned int) p_ct);
    
  if (/*p_ct->connected*/tabIsConnected(p_ct))
    {
      log_debug("%s seems connected\n", p_ct->connection.name);

      if (p_ct->type == CONNECTION_REMOTE)
        sprintf (prompt, _("Close connection to %s?"), p_ct->connection.name);
      else
        strcpy (prompt, _("Close this terminal?"));

      retcode = msgbox_yes_no (prompt);

      if (retcode == GTK_RESPONSE_YES)
        can_close = 1;
      else
        can_close = 0;
    }
  else
    can_close = 1;

  if (can_close)
    {
      // Regroup this tab to adjust the view
      if (p_ct->notebook != notebook)
        terminal_attach_to_main(p_ct);

      if (p_ct->type == CONNECTION_REMOTE && /*p_ct->connected*/tabIsConnected(p_ct))
        {
          /*int fd = vte_pty_get_fd (vte_terminal_get_pty (VTE_TERMINAL (p_ct->vte)));
          log_debug ("fd = %d\n", fd);
          close (vte_pty_get_fd (vte_terminal_get_pty (VTE_TERMINAL (p_ct->vte))));*/
          //vte_pty_close (vte_terminal_get_pty (VTE_TERMINAL (p_ct->vte))); // Dangerous!
          lt_ssh_disconnect (&p_ct->ssh_info);
          //refresh_sftp_panel (&p_ct->ssh_info);
        }

      page = gtk_notebook_page_num (GTK_NOTEBOOK (p_ct->notebook), p_ct->hbox_terminal);

      log_write ("page = %d\n", page);

      if (page >= 0)
        {
          log_write ("Removing page %d %s\n", page, p_ct->connection.name);
          
          gtk_notebook_remove_page (GTK_NOTEBOOK (p_ct->notebook), page);

          connection_tab_list = g_list_remove (connection_tab_list, p_ct);

          if (gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook)) == 0)
            p_current_connection_tab = NULL;
        }
    }

  update_screen_info ();
}

void
close_button_clicked_cb (GtkButton *button, gpointer user_data)
{
  int page, retcode, can_close;
  struct ConnectionTab *p_ct;

  p_ct = (struct ConnectionTab *) user_data;

  connection_tab_close (p_ct);
}

gboolean
query_tooltip_cb (GtkWidget  *widget,
               gint        x,
               gint        y,
               gboolean    keyboard_mode,
               GtkTooltip *tooltip,
               gpointer    user_data)
{
  //log_debug ("x=%d, y=%d\n");
  int row, col;
  GtkBorder *inner_border = NULL;
  struct Connection *p_conn;
  char s[1024];
  
  if (!prefs.hyperlink_tooltip_enabled)
    return FALSE;

  /* Get matched text */
  
  //gtk_widget_style_get (widget, "inner-border", &inner_border, NULL);
  col = (x - (inner_border ? inner_border->left : 0)) / vte_terminal_get_char_width (VTE_TERMINAL (widget));
  row = (y - (inner_border ? inner_border->top : 0)) / vte_terminal_get_char_height (VTE_TERMINAL (widget));
  //gtk_border_free (inner_border);
  
  //log_debug ("row=%d, col=%d\n", row, col);
  
  /* If there is no user selection, check matched strings */
  if (vte_terminal_get_has_selection (VTE_TERMINAL (widget)) == FALSE)
    g_match.matched_string = vte_terminal_match_check (VTE_TERMINAL (widget), col, row, &g_match.tag);
  
  if (g_match.matched_string)
    {
      if (p_conn = get_connection_by_host (g_match.matched_string))
        {
          sprintf (s, "<b><big>%s</big></b>\n<i>Click to connect</i>", p_conn->name);
          gtk_tooltip_set_markup (tooltip, s);
        }
      else
        {
          sprintf (s, "<b><big>%s unknown</big></b>\n<i>Click to bookmark</i>", g_match.matched_string);
          gtk_tooltip_set_markup (tooltip, s);
        }
      
      return TRUE;
    }
    
  return FALSE;
}

struct ConnectionTab *
connection_tab_new ()
{
  struct ConnectionTab *connection_tab;

  connection_tab = g_new0 (struct ConnectionTab, 1);
  //memset (connection_tab, 0, sizeof (struct ConnectionTab));

  connection_tab_list = g_list_append (connection_tab_list, connection_tab);

  //log_debug ("%d\n", (unsigned int) connection_tab);
  //log_debug ("%d\n", (unsigned int) (struct ConnectionTab *) g_list_last (connection_tab_list)->data);

  connection_tab->vte = vte_terminal_new ();

  g_signal_connect (connection_tab->vte, "child-exited", G_CALLBACK (child_exited_cb), connection_tab);
  g_signal_connect (connection_tab->vte, "eof", G_CALLBACK (eof_cb), connection_tab);
  g_signal_connect (connection_tab->vte, "increase-font-size", G_CALLBACK (increase_font_size_cb), NULL);
  g_signal_connect (connection_tab->vte, "decrease-font-size", G_CALLBACK (decrease_font_size_cb), NULL);
  g_signal_connect (connection_tab->vte, "char-size-changed", G_CALLBACK (char_size_changed_cb), main_window);
  g_signal_connect (connection_tab->vte, "maximize-window", G_CALLBACK (maximize_window_cb), main_window);
  //g_signal_connect (connection_tab->vte, "resize-window", G_CALLBACK (resize_window_cb), main_window);
  //g_signal_connect (connection_tab->vte, "status-line-changed", G_CALLBACK (status_line_changed_cb), NULL);
  g_signal_connect (connection_tab->vte, "button-press-event", G_CALLBACK (button_press_event_cb), connection_tab->vte);
  g_signal_connect (connection_tab->vte, "window-title-changed", G_CALLBACK (window_title_changed_cb), connection_tab);
  g_signal_connect (connection_tab->vte, "selection-changed", G_CALLBACK (selection_changed_cb), connection_tab);
  g_signal_connect (connection_tab->vte, "contents-changed", G_CALLBACK (contents_changed_cb), connection_tab);
  g_signal_connect (connection_tab->vte, "grab-focus", G_CALLBACK (terminal_focus_cb), connection_tab);
  
  g_signal_connect_after (connection_tab->vte, "query-tooltip", G_CALLBACK (query_tooltip_cb), NULL);
  gtk_widget_set_has_tooltip (connection_tab->vte, TRUE);

  int tag;
  /*tag = vte_terminal_match_add_gregex (VTE_TERMINAL(connection_tab->vte), url_regexpr, 0);
  vte_terminal_match_set_cursor_type (VTE_TERMINAL(connection_tab->vte), tag, GDK_HAND2);*/

  tag = vte_terminal_match_add_gregex (VTE_TERMINAL(connection_tab->vte), ip_regexpr, 0);
  vte_terminal_match_set_cursor_type (VTE_TERMINAL(connection_tab->vte), tag, GDK_HAND2);
/*
  connection_tab->connected = 0;
  connection_tab->logged = 0;
  connection_tab->auth_state = AUTH_STATE_NOT_LOGGED;
  connection_tab->auth_attempt = 0;
  connection_tab->changes_count = 0;
  connection_tab->status = TAB_STATUS_NORMAL;
*/
  tabInitConnection(connection_tab);

  connection_tab->cx = -1;
  connection_tab->cy = -1;

  memset (&connection_tab->connection, 0, sizeof (struct Connection));

  return (connection_tab);
}

void
connection_tab_add (struct ConnectionTab *connection_tab)
{
  GtkWidget *tab_label;
  GtkWidget *close_button;
  gint w, h;
  PangoFontDescription *font_desc;
  int font_size;

#if (GTK_MAJOR_VERSION == 2)
  connection_tab->hbox_terminal = gtk_hbox_new (FALSE, 0); /* for vte and scrolbar */
  connection_tab->scrollbar = gtk_vscrollbar_new (vte_terminal_get_adjustment (VTE_TERMINAL (connection_tab->vte)));
#else
  connection_tab->hbox_terminal = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0); /* for vte and scrolbar */
#if (VTE_CHECK_VERSION(0,38,3) == 1)
  connection_tab->scrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL,
                                                 gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(connection_tab->vte)));
#else
  connection_tab->scrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, 
                                                 vte_terminal_get_adjustment (VTE_TERMINAL (connection_tab->vte))
                                                 /*gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(connection_tab->vte))*/);
#endif
#endif


  gtk_box_pack_start (GTK_BOX (connection_tab->hbox_terminal), connection_tab->vte, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (connection_tab->hbox_terminal), connection_tab->scrollbar, FALSE, FALSE, 0);
  gtk_widget_show_all (connection_tab->hbox_terminal);

#if (GTK_MAJOR_VERSION == 2)
  tab_label = gtk_hbox_new (FALSE, 0);
#else
  tab_label = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
#endif

  gtk_container_set_border_width (GTK_CONTAINER (tab_label), 0);
  gtk_box_set_spacing (GTK_BOX (tab_label), 8);

  GtkWidget *image_type;
  
  /* create label with icon, text and close button */

  if (connection_tab->type == CONNECTION_REMOTE)
#if (GTK_CHECK_VERSION(3,6,5) == 1)
    image_type = gtk_image_new_from_icon_name("network-workgroup", GTK_ICON_SIZE_MENU);
#else
    image_type = gtk_image_new_from_stock (GTK_STOCK_NETWORK, GTK_ICON_SIZE_MENU); // Mac OS X
#endif
  else
    image_type = gtk_image_new_from_stock (MY_STOCK_TERMINAL, GTK_ICON_SIZE_MENU);

  connection_tab->label = gtk_label_new (connection_tab->connection.name);

  close_button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (close_button), GTK_RELIEF_NONE);
  gtk_container_set_border_width (GTK_CONTAINER (close_button), 0);

  GtkWidget *image = gtk_image_new_from_icon_name ("window-close", GTK_ICON_SIZE_MENU);
  /*
  gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (close_button), GTK_ICON_SIZE_SMALL_TOOLBAR, &w, &h);
  gtk_widget_set_size_request (close_button, w+2, h+2);
  */
  gtk_container_add (GTK_CONTAINER (close_button), image);

#if (GTK_MAJOR_VERSION == 2)
  g_signal_connect (GTK_OBJECT (close_button), "clicked", GTK_SIGNAL_FUNC (close_button_clicked_cb), connection_tab);
#else
  g_signal_connect (close_button, "clicked", G_CALLBACK (close_button_clicked_cb), connection_tab);
#endif

/*
  gtk_box_pack_start (GTK_BOX (tab_hbox), image_type, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (tab_hbox), connection_tab->label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (tab_hbox), close_button, FALSE, FALSE, 0);
*/

  gtk_box_pack_end (GTK_BOX (tab_label), close_button, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (tab_label), connection_tab->label, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (tab_label), image_type, FALSE, FALSE, 0);

  gtk_widget_show_all (tab_label);


  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), connection_tab->hbox_terminal, tab_label, gtk_label_new (connection_tab->connection.name));

  gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (notebook), connection_tab->hbox_terminal, TRUE);
  gtk_notebook_set_tab_detachable (GTK_NOTEBOOK (notebook), connection_tab->hbox_terminal, FALSE);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), -1);

  connection_tab->notebook = notebook;

  /* Store original font */

  font_desc = pango_font_description_copy (vte_terminal_get_font (VTE_TERMINAL (connection_tab->vte)));
  globals.original_font_size = pango_font_description_get_size (font_desc) / PANGO_SCALE;
  strcpy (globals.system_font, pango_font_description_to_string (font_desc));

  pango_font_description_free (font_desc);  

  log_debug ("Original font : %s\n", globals.system_font);

  //connection_tab_set_status (connection_tab, TAB_STATUS_NORMAL);
  
  if (connection_tab->type == CONNECTION_LOCAL)
    gtk_label_set_text (GTK_LABEL (connection_tab->label), "local shell");

  apply_preferences (connection_tab->vte);
  apply_profile (connection_tab, 0);

  gtk_widget_grab_focus (connection_tab->vte);

  select_current_profile_menu_item (connection_tab);
}

/**
 * refreshTabStatus() - Changes tab label depending on current status
 */
void
refreshTabStatus (SConnectionTab *pTab)
{
  char *l_text;
  char l_text_new[256];
  
  if (!prefs.tab_alerts)
    return;

  if (!GTK_IS_LABEL (pTab->label))
    return;
  
  l_text = (char *) gtk_label_get_text (GTK_LABEL(pTab->label));

  sprintf (l_text_new, "%s", l_text);

  if (tabIsConnected (pTab)) {
    if (tabGetFlag (pTab, TAB_CHANGED)) {
      if (pTab != p_current_connection_tab)
        sprintf (l_text_new, "<span color=\"%s\">%s</span>", prefs.tab_status_changed_color, l_text);
      else
        sprintf (l_text_new, "%s", l_text);

      tabResetFlag (pTab, TAB_CHANGED);
    }
  }
  else {
    if (pTab == p_current_connection_tab)
      sprintf (l_text_new, "<span color=\"%s\">%s</span>", prefs.tab_status_disconnected_color, l_text);
    else
      sprintf (l_text_new, "<span color=\"%s\">%s</span>", prefs.tab_status_disconnected_alert_color, l_text);
  }
  
  gtk_label_set_markup (GTK_LABEL (pTab->label), l_text_new);   
}

/**
 * connection_tab_set_status() - Changes tab label (deprecated, use refreshTabStatus())
 */
/*
void
connection_tab_set_status (struct ConnectionTab *connection_tab, int status)
{
\*
  char *l_text;
  char l_text_new[256];
  
  if (!prefs.tab_alerts)
    return;
  
  if (connection_tab == p_current_connection_tab)
    {
      if (tabIsConnected(connection_tab)\*->connected*\)
        status = TAB_STATUS_NORMAL;
      else
        status = TAB_STATUS_DISCONNECTED;
    }
  
  connection_tab->status = status;

  if (!GTK_IS_LABEL (connection_tab->label))
    return;
  
  l_text = (char *) gtk_label_get_text (GTK_LABEL(connection_tab->label));
  
  switch (status) {
    case TAB_STATUS_NORMAL:
      sprintf (l_text_new, "%s", l_text);
      break;
      
    case TAB_STATUS_CHANGED:
      sprintf (l_text_new, "<span color=\"%s\">%s</span>", prefs.tab_status_changed_color, l_text);
      break;
      
    case TAB_STATUS_DISCONNECTED:
      if (connection_tab == p_current_connection_tab)
        sprintf (l_text_new, "<span color=\"%s\">%s</span>", prefs.tab_status_disconnected_color, l_text);
      else
        sprintf (l_text_new, "<span color=\"%s\">%s</span>", prefs.tab_status_disconnected_alert_color, l_text);
      break;
      
    default:
      sprintf (l_text_new, "%s", l_text);
      break;     
  }
  
  gtk_label_set_markup (GTK_LABEL (connection_tab->label), l_text_new);
*\
}
*/
int
connection_tab_getcwd (struct ConnectionTab *p_ct, char *directory)
{
  char buffer[1024];
  char filename[1024];
  char window_title[1024];
  char *pc;
  int n;

  if (p_ct == 0)
    return 1;
  
  strcpy (directory, "");
  
  if (p_ct->type == CONNECTION_LOCAL)
    {
      sprintf (filename, "/proc/%d/cwd", p_ct->pid);
      
      n = readlink (filename, buffer, 1024);
          
      if (n < 0)
        return 3;
    
      buffer[n] = 0;
    }
  else if (p_ct->type == CONNECTION_REMOTE && /*!strcmp (p_ct->connection.protocol, "ssh")*/p_ct->type == PROT_TYPE_SSH)
    {
      if (p_ct->vte == 0)
        return 1;
      
      if (vte_terminal_get_window_title (VTE_TERMINAL(p_ct->vte)))
        strcpy (window_title, vte_terminal_get_window_title (VTE_TERMINAL(p_ct->vte)));
      else
        return 1;

      pc = (char *) strchr (window_title, ':');
      

      if (pc)
        {
          *pc ++;
          strcpy (buffer, pc);
#ifdef DEBUG
          printf ("connection_tab_getcwd() : directory = '%s'\n", buffer);
#endif
        }
    }
  else
    return 2;

  strcpy (directory, buffer);
      
  return 0;
}

void
connection_log_on_param (struct Connection *p_conn)
{
  int retcode = 0;
  struct Protocol *p_prot;
  struct ConnectionTab *p_connection_tab;

  p_connection_tab = connection_tab_new ();
  p_connection_tab->type = CONNECTION_REMOTE;
          
  if (p_conn)
    {
      connection_copy (&p_connection_tab->connection, p_conn);

      p_connection_tab->auth_attempt = 0;
     // p_connection_tab->auth_flags = 0;
      //p_connection_tab->logged = 0;
      tabResetFlag (p_connection_tab, TAB_LOGGED);

      log_debug ("user = '%s'\n", p_connection_tab->connection.user);
    }
  else
    retcode = choose_manage_connection (&p_connection_tab->connection);

#ifdef DEBUG
  //printf ("connection_log_on_param(): %s : connected = %d, logged = %d\n", p_connection_tab->connection.name, p_connection_tab->connected, p_connection_tab->logged);
#endif 

  if (retcode == 0)
    {
      if (p_connection_tab->connection.auth_mode == CONN_AUTH_MODE_SAVE && p_connection_tab->connection.auth_user[0])
        strcpy (p_connection_tab->connection.user, p_connection_tab->connection.auth_user);

      if (p_connection_tab->connection.auth_mode == CONN_AUTH_MODE_SAVE && p_connection_tab->connection.auth_password[0])
        strcpy (p_connection_tab->connection.password, p_connection_tab->connection.auth_password);

      /* Add the new tab */ 
      log_debug ("Adding new tab...\n");
      connection_tab_add (p_connection_tab);
      p_current_connection_tab = p_connection_tab;  
      //connection_tab_set_status (p_current_connection_tab, TAB_STATUS_NORMAL);
      refreshTabStatus (p_current_connection_tab);

      log_write ("Log on...\n");

      retcode = log_on (p_connection_tab);
      
      log_debug ("log_on() returns %d\n", retcode);

      if (retcode == 0)
        {
          /*
          log_debug ("user=%s last_user=%s\n", p_connection_tab->connection.user, p_connection_tab->connection.last_user);
          
          connection_tab_add (p_connection_tab);
          p_current_connection_tab = p_connection_tab;  
          connection_tab_set_status (p_current_connection_tab, TAB_STATUS_NORMAL);
          */
          if (lt_ssh_is_connected (&p_current_connection_tab->ssh_info))
            {
              sftp_refresh_directory_list (&p_current_connection_tab->ssh_info);
              refresh_sftp_panel (&p_current_connection_tab->ssh_info);
              refresh_panel_history ();
            }
        }

      //connection_tab_set_status (p_current_connection_tab, TAB_STATUS_NORMAL);
      refreshTabStatus (p_current_connection_tab);
    }

  update_screen_info ();
  
  /* update session file */

  if (prefs.save_session)
    save_session_file (NULL);
}

/**
 * connection_log_on() - this is the function called by menu item
 */
void
connection_log_on ()
{
  connection_log_on_param (NULL);
}

void
connection_log_off ()
{
  log_debug ("\n");

  if (!p_current_connection_tab)
    return;

  //vte_terminal_feed_child (VTE_TERMINAL(vte), "exit\n", -1);

  if (p_current_connection_tab->type == CONNECTION_REMOTE && /*p_current_connection_tab->connected*/tabIsConnected(p_current_connection_tab))
    {
      //vte_pty_close (vte_terminal_get_pty (VTE_TERMINAL (p_current_connection_tab->vte))); // Dangerous!
      //close (vte_terminal_get_pty (VTE_TERMINAL (p_current_connection_tab->vte)));
      kill (p_current_connection_tab->pid, SIGTERM);
      lt_ssh_disconnect (&p_current_connection_tab->ssh_info);
      refresh_sftp_panel (&p_current_connection_tab->ssh_info);

      log_write ("Terminal closed\n");

      //vte_terminal_set_pty (VTE_TERMINAL (vte), 0);
/*
      p_current_connection_tab->connected = 0;
      p_current_connection_tab->logged = 0;
      p_current_connection_tab->auth_state = AUTH_STATE_NOT_LOGGED;
*/
      tabInitConnection(p_current_connection_tab);
    }
  else
    log_write ("Local terminal can't be disconnected");

  update_screen_info ();
}

void
connection_duplicate ()
{
  char directory[1024];
  char command[1024];
  
  if (!p_current_connection_tab)
    return;

  if (p_current_connection_tab->type == CONNECTION_LOCAL)
    {
      connection_tab_getcwd (p_current_connection_tab, directory);
      
      connection_new_terminal_dir (directory);
    }
  else if (p_current_connection_tab->type == CONNECTION_REMOTE)
    {
      connection_tab_getcwd (p_current_connection_tab, directory);

      /* force autentication */
      //p_current_connection_tab->connection.auth ++;
      //p_current_connection_tab->connection.auth_attempt = 0;

      connection_log_on_param (&p_current_connection_tab->connection);
      
      /* TODO: change directory */
      /*
      if (directory[0])
        {
          sprintf (command, "cd %s\n", directory);  
          vte_terminal_feed_child (p_current_connection_tab->vte, command, -1);
        }
      */

      /* back to original value */
      //p_current_connection_tab->connection.auth --;
    }
  else
    return;
}

void
connection_edit_protocols ()
{
  manage_protocols (&g_prot_list);
}

void
connection_new_terminal_dir (char *directory)
{
  struct Protocol *p_prot;
  struct ConnectionTab *p_connection_tab;
  gboolean success;
  char error_msg[1024];
  int rc;

  log_write ("Creating a new terminal\n");

  p_connection_tab = connection_tab_new ();

  if (p_connection_tab)
    {
      success = terminal_new (p_connection_tab, directory);

      if (success)
        {
          log_debug ("Adding new tab ...\n");
     
          //p_connection_tab->connected = 1;
          tabSetConnectionStatus (p_connection_tab, TAB_CONN_STATUS_CONNECTED);
          p_connection_tab->type = CONNECTION_LOCAL;
          strcpy (p_connection_tab->connection.name, prefs.label_local);
          connection_tab_add (p_connection_tab);

          p_current_connection_tab = p_connection_tab;

          /* update session file */

          if (!prefs.save_session)
            save_session_file (NULL);
        }
      else
        msgbox_error ("%s", error_msg);
    }

  update_screen_info ();
}

/**
 * connection_new_terminal() - this is the function called by menu item
 */
void
connection_new_terminal ()
{
  connection_new_terminal_dir (NULL);
}

void
connection_close_tab ()
{
  if (!p_current_connection_tab)
    return;

  connection_tab_close (p_current_connection_tab);
  
  /* update session file */

  if (!prefs.save_session)
    save_session_file (NULL);
}

void
application_quit ()
{
  int retcode;
  int can_quit = 1;
  int n;
  char message[1024];
  GList *item;
  
  n = connection_tab_count (CONNECTION_REMOTE) + connection_tab_count (CONNECTION_LOCAL);

  if (n)
    {
      sprintf (message, _("There are %d active terminal/s.\nExit anyway?"), n);

      if (prefs.save_session)
        strcat (message, "\nCurrent session will be reloaded at next startup.");

      retcode = msgbox_yes_no (message);

      if (retcode == GTK_RESPONSE_YES)
        can_quit = 1;
      else
        can_quit = 0;
    }

  if (can_quit)
    {
      globals.running = 0;
    }
}

void
open_recent_connection_from_info (GtkRecentInfo *info)
{
  GList *item;
  struct Connection *p_conn;
  char display_name[1024];
  char s_tmp[256];

  strcpy (display_name, gtk_recent_info_get_display_name (info));

  log_write ("Selected recent connection: %s\n", display_name);

  /* Find the connection */
  item = g_list_first (g_recent_connections_list);

  while (item)
    {
      p_conn = (struct Connection *) item->data;

      sprintf (s_tmp, "%s@%s[%s]", p_conn->user, p_conn->name, p_conn->protocol);

      log_debug ("%s vs %s\n", display_name, s_tmp);

      if (!strcmp (s_tmp, display_name))
        break;

      item = g_list_next (item);
    }

  if (item)
    {
      log_write ("Found recent connetcion: %s@%s[%s]\n", p_conn->user, p_conn->name, p_conn->protocol);
      connection_log_on_param (p_conn);
    }
  else
    log_write ("Recent connection not found.\n");

  g_free (info);
}

void
open_recent_session_from_info (GtkRecentInfo *info)
{
  GList *item;
  struct Connection *p_conn;
  char filename[1024];
  char s_tmp[256];
  
  strcpy (filename, gtk_recent_info_get_display_name (info));

  log_debug ("%s\n", filename);

  load_session_file (filename);

  g_free (info);
}

void
session_recent_cb (GtkAction *action, gpointer user_data)
{
  int i;
  char s_tmp[256];
  GList *item;
  struct Connection *p_conn;
  GtkRecentInfo *info;

#ifdef DEBUG
  //printf ("connection_recent_cb() : %s\n", (char *) user_data);
#endif

  info = g_object_get_data (G_OBJECT (action), "gtk-recent-info");

#ifdef DEBUG
  printf ("session_recent_cb() : %s\n", gtk_recent_info_get_display_name (info));
#endif

  open_recent_session_from_info (info);
}

gchar * 
utils_escape_underscores (const gchar* text, gssize length)
{
  GString *str;
  const gchar *p;
  const gchar *end;

  g_return_val_if_fail (text != NULL, NULL);

  if (length < 0)
    length = strlen (text);

  str = g_string_sized_new (length);

  p = text;
  end = text + length;

  while (p != end)
  {
    const gchar *next;
    next = g_utf8_next_char (p);

    switch (*p)
    {
      case '_':
        g_string_append (str, "__");
        break;
      default:
        g_string_append_len (str, p, next - p);
        break;
    }

    p = next;
  }

  return g_string_free (str, FALSE);
}

void
clean_recents ()
{
  FILE *fp;
  GList *l, *items;

  log_write ("\n");
  
  /* remove items from recent manager */

  items = gtk_recent_manager_get_items (g_recent_manager);

  for (l = items; l != NULL; l = l->next)
    {
      GtkRecentInfo *info = l->data;

      if (gtk_recent_info_has_group (info, "lterm"))
        {
          log_debug ("%s\n", gtk_recent_info_get_uri (info));
          gtk_recent_manager_remove_item (g_recent_manager, gtk_recent_info_get_uri (info), NULL);
        }
    }

  /* remove from internal list */

  g_list_free (g_recent_connections_list);
  g_recent_connections_list = 0;

  /* reset recent-file */

  fp = fopen (globals.recent_connections_file, "w");
  fclose (fp);
}

void
update_recent_sessions_menu ()
{
  GtkRecentManager *recent_manager;
  GList *actions, *l, *items;
  GList *filtered_items = NULL;
  gint i;

  /* clean */

  if (recents_menu_ui_id != 0)
    gtk_ui_manager_remove_ui (ui_manager, recents_menu_ui_id);

  actions = gtk_action_group_list_actions (recents_action_group);

  for (l = actions; l != NULL; l = l->next)
    {
      g_signal_handlers_disconnect_by_func (GTK_ACTION (l->data), G_CALLBACK (session_recent_cb), (gpointer) recent_entries[i].name);
      gtk_action_group_remove_action (recents_action_group, GTK_ACTION (l->data));
    }

  g_list_free (actions);

  /* create */

  recents_menu_ui_id = gtk_ui_manager_new_merge_id (ui_manager);

  //recent_manager =  gtk_recent_manager_get_default ();
  recent_manager =  g_recent_manager;
  items = gtk_recent_manager_get_items (recent_manager);

  /* filter */

  for (l = items; l != NULL; l = l->next)
    {
      GtkRecentInfo *info = l->data;

      if (!gtk_recent_info_has_group (info, "lterm-session"))
        continue;

      filtered_items = g_list_prepend (filtered_items, info);
    }

  i = 0;
  l = g_list_first (filtered_items);

  while (l && i < prefs.max_recent_connections)
    {
      gchar *action_name;
      const gchar *display_name;
      gchar *escaped;
      gchar *label;
      gchar *uri;
      gchar *ruri;
      gchar *tip;
      GtkAction *action;
      GtkRecentInfo *info = l->data;
      GFile *location;
      char s_tmp[256];

      action_name = g_strdup_printf ("recent-info-%d", i);
      display_name = gtk_recent_info_get_display_name (info);
      escaped = utils_escape_underscores (display_name, -1);

      label = g_strdup_printf ("%d. %s", i+1, escaped);
      
      log_debug("%s\n", label);

      g_free (escaped);

      action = gtk_action_new (action_name, label, NULL, NULL);

      g_object_set_data_full (G_OBJECT (action), "gtk-recent-info", gtk_recent_info_ref (info), (GDestroyNotify) gtk_recent_info_unref);
      g_signal_connect (action, "activate", G_CALLBACK (session_recent_cb), NULL /*recent_entries[i].name*/);

      gtk_action_group_add_action (recents_action_group, action);
      g_object_unref (action);
      
      gtk_ui_manager_add_ui (ui_manager,
                             recents_menu_ui_id,
                             "/MainMenu/SessionMenu/SessionRecentsPlaceholder",
                             action_name,
                             action_name,
                             GTK_UI_MANAGER_MENUITEM,
                             FALSE);

      g_free (action_name);
      g_free (label);
      //g_free (tip); /* not allocated */

      i ++;
      l = g_list_next (l);
    }

  g_list_free (filtered_items);
  //g_list_free_full (items, (GDestroyNotify) gtk_recent_info_unref);
}

/**
 * get_recent_connection_item()
 * Returns the g_list element of the recent list
 */
GList *
get_recent_connection_item (char *user, char *name, char *protocol)
{
  GList *item;
  struct Connection *p_conn = NULL;

  item = g_list_first (g_recent_connections_list);

  while (item)
    {
      p_conn = (struct Connection *) item->data;

      if (!strcmp (user, p_conn->user) && !strcmp (name, p_conn->name) && !strcmp (protocol, p_conn->protocol))
        return (item);
      
      item = g_list_next (item);
    }

  return (NULL);
}

/**
 * get_recent_connection()
 * Returns a pointer to the found connection
 */
struct Connection *
get_recent_connection (char *user, char *name, char *protocol)
{
  GList *item = get_recent_connection_item (user, name, protocol);

  return (item != NULL ? (struct Connection *) item->data : NULL);
}

void
add_recent_connection (struct Connection *p_conn)
{
  GList *item;
  struct Connection *p_conn_recent;
  GtkRecentManager *recent_manager;
  GtkRecentData *recent_data;
  char s_tmp[256];
  int found = 0;

  log_debug ("\n");

  static gchar *groups[2] = {
    "lterm",
    NULL
  };

  /* delete from internal list if present */
/*
  item = g_list_first (g_recent_connections_list);

  while (item)
    {
      p_conn_recent = (struct Connection *) item->data;

      if (!strcmp (p_conn_recent->user, p_conn->user) 
          && !strcmp (p_conn_recent->name, p_conn->name) 
          && !strcmp (p_conn_recent->protocol, p_conn->protocol) 
         )
        {
          //log_debug ("remove %s@%s[%s]\n", p_conn_recent->user, p_conn_recent->name, p_conn_recent->protocol);
          //g_recent_connections_list = g_list_remove (g_recent_connections_list, p_conn_recent);
          found = 1;
          break;
        }

      item = g_list_next (item);
    }
*/
/*
1.4.0-test11
  if (!get_recent_connection (p_conn->user, p_conn->name, p_conn->protocol))
    {
      // add to internal list

      //g_recent_connections_list = g_list_prepend (g_recent_connections_list, p_conn);
      g_recent_connections_list = g_list_append (g_recent_connections_list, p_conn);
    }
*/

  // If already present, remove it before inserting
  item = get_recent_connection_item (p_conn->user, p_conn->name, p_conn->protocol);

  if (item) {
    log_debug ("Removing %s@%s\n", p_conn->user, p_conn->name);
    g_recent_connections_list = g_list_remove (g_recent_connections_list, item->data);
  }

  // Add connection
  g_recent_connections_list = g_list_append (g_recent_connections_list, p_conn);

  /* update the recent manager */

  //recent_manager =  gtk_recent_manager_get_default ();
  recent_manager =  g_recent_manager;

  recent_data = g_slice_new (GtkRecentData);

  sprintf (s_tmp, "%s@%s[%s]", p_conn->user, p_conn->name, p_conn->protocol);
  recent_data->display_name = g_strdup (s_tmp);
  recent_data->description = g_strdup ("lterm connection string" /*p_conn->name*/);
  recent_data->mime_type = (gchar *) "application/lterm-connection";
  recent_data->app_name = (gchar *) g_get_application_name ();
  recent_data->app_exec = g_strjoin (" ", g_get_prgname (), g_strconcat ("conn:", recent_data->display_name, NULL), NULL);
  recent_data->groups = groups;
  recent_data->is_private = FALSE;

  gtk_recent_manager_add_full (recent_manager, recent_data->display_name, recent_data);
  
  log_debug ("Added %s\n", recent_data->display_name);

  g_free (recent_data->app_exec);

  g_slice_free (GtkRecentData, recent_data);

  //update_recent_connections_menu ();
}

void
add_recent_session (char *filename)
{
  char uri[1024];
  GtkRecentManager *recent_manager;
  GtkRecentData *recent_data;

  static gchar *groups[2] = {
    "lterm-session",
    NULL
  };

  if (filename == 0)
    return;
  
  /* update the recent manager */

  //recent_manager =  gtk_recent_manager_get_default ();
  recent_manager =  g_recent_manager;

  recent_data = g_slice_new (GtkRecentData);

  recent_data->display_name = g_strdup (filename);
  recent_data->description = g_strdup ("lterm sessison file");
  recent_data->mime_type = (gchar *) "application/lterm-session";
  recent_data->app_name = g_strdup ("lterm"); //(gchar *) g_get_application_name ();
  recent_data->app_exec = g_strjoin (" ", g_get_prgname (), "%F", NULL);
  recent_data->groups = groups;
  recent_data->is_private = FALSE;

  sprintf (uri, "file://%s", filename);
  gtk_recent_manager_add_full (recent_manager, uri, recent_data);

  log_debug ("Added %s\n", recent_data->display_name);

  g_free (recent_data->app_exec);
  g_slice_free (GtkRecentData, recent_data);

  update_recent_sessions_menu ();
}

static void
recent_chooser_item_activated (GtkRecentChooser *chooser)
{
  GList *item;
  GtkRecentInfo *info;
  struct Connection *p_conn;
  char display_name[1024];
  char s_tmp[256];

  log_debug ("\n");

  info = gtk_recent_chooser_get_current_item (chooser);

  open_recent_connection_from_info (info);
}

static GtkWidget *
setup_toolbar_connect_button (GtkWidget *toolbar)
{
  GtkRecentManager *recent_manager;
  GtkRecentFilter *filter;
  GtkWidget *toolbar_recent_menu;
  GtkToolItem *connect_button;
  GtkAction *action;

  log_debug ("\n");

  //recent_manager = gtk_recent_manager_get_default ();
  recent_manager = g_recent_manager;

  /* recent files menu tool button */
  toolbar_recent_menu = gtk_recent_chooser_menu_new_for_manager (recent_manager);

  gtk_recent_chooser_set_local_only (GTK_RECENT_CHOOSER (toolbar_recent_menu), FALSE);
  gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (toolbar_recent_menu), GTK_RECENT_SORT_MRU);
  gtk_recent_chooser_set_limit (GTK_RECENT_CHOOSER (toolbar_recent_menu), prefs.max_recent_connections);
  gtk_recent_chooser_set_show_icons (GTK_RECENT_CHOOSER (toolbar_recent_menu), FALSE);
  gtk_recent_chooser_menu_set_show_numbers (GTK_RECENT_CHOOSER_MENU (toolbar_recent_menu), FALSE);
  gtk_recent_chooser_set_show_private (GTK_RECENT_CHOOSER (toolbar_recent_menu), TRUE);

  filter = gtk_recent_filter_new ();
  gtk_recent_filter_add_group (filter, "lterm");
  gtk_recent_chooser_set_filter (GTK_RECENT_CHOOSER (toolbar_recent_menu), filter);

  g_signal_connect (toolbar_recent_menu, "item_activated", G_CALLBACK (recent_chooser_item_activated), NULL);

  /* add the custom Connect button to the toolbar */
#if (GTK_CHECK_VERSION(3,10,0) == 1)
  GtkWidget *image = gtk_image_new_from_icon_name ("gtk-connect", GTK_ICON_SIZE_SMALL_TOOLBAR);
  connect_button = gtk_menu_tool_button_new (image, _("Connect"));
#else
  connect_button = gtk_menu_tool_button_new_from_stock (GTK_STOCK_CONNECT);
#endif
  g_signal_connect (G_OBJECT (connect_button), "clicked", G_CALLBACK (connection_log_on), NULL);
  gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (connect_button), toolbar_recent_menu);
  //gtk_recent_chooser_menu_set_show_numbers (GTK_RECENT_CHOOSER (toolbar_recent_menu), TRUE);
  gtk_widget_show (GTK_WIDGET(connect_button));

  gtk_tool_item_set_tooltip_text (connect_button, _("Log on"));
  gtk_menu_tool_button_set_arrow_tooltip_text (GTK_MENU_TOOL_BUTTON (connect_button), _("Log on recent"));

  //action = gtk_action_group_get_action (window->priv->always_sensitive_action_group, "FileOpen");
  //g_object_set (action, "is_important", TRUE, "short_label", _("Open"), NULL);
  action = gtk_action_group_get_action (recents_action_group, "SessionRecent");
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (connect_button), action);

  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), connect_button, 0);

  return toolbar_recent_menu;
}

int
load_recent_connections ()
{
  int rc;
  GList *item;
  struct Connection_List cl;
  struct Connection *c;
    
  log_debug ("\n");
/*
  cl_init (&cl);

  log_write ("Loading %s\n", globals.recent_connections_file);

  rc = load_connections_from_file (globals.recent_connections_file, &cl);
  
  if (rc)
    {
      log_write ("Can't open %s\n", globals.recent_connections_file);
      return (rc);
    }

  c = cl.head;
  
  while (c)
    { 
      //log_debug ("added %s@%s[%s]... ", c->user, c->name, c->protocol);

      if (!get_recent_connection (c->user, c->name, c->protocol))
        {
          g_recent_connections_list = g_list_prepend (g_recent_connections_list, c);
          //log_debug ("added\n");
        }
      else
        {
          //log_debug ("duplicated\n");
        }
        
      c = c->next;
    }
*/

  log_write ("Loading recents: %s\n", globals.recent_connections_file);

  g_recent_connections_list = load_connection_list_from_file_xml (globals.recent_connections_file);

  log_write ("Recents loaded: %d\n", g_list_length (g_recent_connections_list));

  g_recent_connections_list = g_list_reverse (g_recent_connections_list);
  
  return 0;
}

int
save_recent_connections ()
{
  struct Connection conn;
  struct Connection *p_conn;
  struct Connection_List cl;
  char *p_enc;
  char *p_enc_b64;
  char name_tmp[256];
  int len, rc = 0;
  int i;
  GList *item;
/*
  // Create a new list fron recents
  cl_init (&cl);

  i = 0;
  item = g_list_first (g_recent_connections_list);

  while (item)
    {
      p_conn = (struct Connection *) item->data;

      // Names must be distinct for proper loading

      //sprintf (name_tmp, "%s~%d", p_conn->name, i);
      //strcpy (p_conn->name, name_tmp);

      //p_conn->auth = 1;
      
      // encrypt password
      strcpy (p_conn->password_encrypted, des_encrypt_b64 (p_conn->password));
      
      cl_append (&cl, p_conn);

      i ++;
      item = g_list_next (item);
    }

  //rc = save_connections_to_file (globals.recent_connections_file, &cl, CONN_SAVE_USERPASS);
  rc = save_connections_to_file_xml_from_list (&cl, globals.recent_connections_file);
    
  cl_release (&cl);
*/
  rc = save_connections_to_file_xml_from_glist (g_recent_connections_list, globals.recent_connections_file);
  
  return 0;
}

GtkWidget *
_get_active_widget()
{
  GtkWindow *active;
  
  GList *list = gtk_window_list_toplevels ();
  
  GList *item;
  item = g_list_first (list);

  while (item)
    {
      active = GTK_WINDOW(item->data);
      
      if (gtk_window_is_active (active))
        {
          log_debug ("Active window: %s\n", gtk_window_get_title (active));
          GtkWidget *w = gtk_window_get_focus (active);
          return (w);
        }

      item = g_list_next (item);
    }
    
  return (NULL);
}

void
edit_copy ()
{
  int done = 0;
  GtkClipboard *clipboard;
  
  /* Get the active widget */
  GtkWidget *w = _get_active_widget ();
  
  if (p_current_connection_tab)
    {
      /* Check if the terminal has the focus or the current widget is null (popup menu) */
      if (gtk_widget_has_focus (p_current_connection_tab->vte) || w == NULL)
        {
          /* If pointing at an hypertext then copy it, else copy text */
          if (g_match.matched_string)
            {
              clipboard = gtk_widget_get_clipboard (p_current_connection_tab->vte, GDK_SELECTION_CLIPBOARD);
              gtk_clipboard_set_text (clipboard, g_match.matched_string, -1);
              gtk_clipboard_store (clipboard);
            }
          else
            vte_terminal_copy_clipboard (VTE_TERMINAL (p_current_connection_tab->vte));
            
          done = 1;
        }
    }
    
  /* If we did nothing with the terminal then do it with the current widget */
  if (!done)
    { 
      if (w)
        g_signal_emit_by_name (w, "copy-clipboard", NULL);
    }
}

void
edit_paste ()
{
  int done = 0;
  
  /* Get the active widget */
  GtkWidget *w = _get_active_widget ();
    
  /**
   *  If terminal has the focus or the current widget is null (popup menu), then paste clipboard into it.
   *  Else paste into the current active widget.
   *  This is mainly needed on Mac OS X where paste is always Command+V
   */
  if (p_current_connection_tab)
    {
      if (gtk_widget_has_focus (p_current_connection_tab->vte) || w == NULL)
        {
          vte_terminal_paste_clipboard (VTE_TERMINAL (p_current_connection_tab->vte));
          done = 1;
        }
    }
    
  if (!done)
    {
      if (w)
        g_signal_emit_by_name (w, "paste-clipboard", NULL);
      
      /* See: http://stackoverflow.com/questions/10508810/find-out-which-gtk-widget-has-the-current-selection */
    }
}

void
edit_copy_and_paste ()
{
  if (!p_current_connection_tab)
    return;

  //vte_terminal_copy_clipboard (VTE_TERMINAL (p_current_connection_tab->vte));
  //vte_terminal_paste_clipboard (VTE_TERMINAL (p_current_connection_tab->vte));
  edit_copy ();
  edit_paste ();
}

static void
paste_host (GtkAction *action)
{
  struct Connection *p_conn;

  if (!p_current_connection_tab)
    return;

  gchar *name = (gchar *) gtk_action_get_name (GTK_ACTION (action));
  p_conn = get_connection_by_name (name);

  if (p_conn)
    vte_terminal_feed_child (VTE_TERMINAL(p_current_connection_tab->vte), p_conn->host, -1);
}

void
edit_find ()
{
  GtkWidget *dialog;
  GtkBuilder *builder;
  GError *error=NULL;
  char ui[256];
  int result, rc = 0, i;
  
  if (p_current_connection_tab == NULL)
    return;

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/find.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }

  GtkWidget *vbox_main = GTK_WIDGET (gtk_builder_get_object (builder, "vbox_main"));
  GtkWidget *entry_expr = GTK_WIDGET (gtk_builder_get_object (builder, "entry_expr"));

  if (globals.find_expr[0])
    gtk_entry_set_text (GTK_ENTRY (entry_expr), globals.find_expr);

  /* Create dialog */

  dialog = gtk_dialog_new_with_buttons
                 (_("Find"), NULL,
                  GTK_DIALOG_MODAL, 
                  "_Cancel", GTK_RESPONSE_CANCEL,
                  "_Ok", GTK_RESPONSE_OK,
                  NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gtk_entry_set_activates_default (GTK_ENTRY (entry_expr), TRUE);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox_main);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
  
run_dialog:

  result = gtk_dialog_run (GTK_DIALOG (dialog));

  if (result == GTK_RESPONSE_OK)
    {
      strcpy (globals.find_expr, gtk_entry_get_text (GTK_ENTRY (entry_expr)));
      terminal_set_search_expr (globals.find_expr);
      rc = 0;
    }
  else
    {
      rc = -1;
    }

  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));
  
  if (rc == 0)
    terminal_find_next ();
}

void
edit_select_all ()
{
  if (!p_current_connection_tab)
    return;

  vte_terminal_select_all (VTE_TERMINAL (p_current_connection_tab->vte));
}

void
edit_current_profile ()
{
  struct Profile *p_profile;

  if (!p_current_connection_tab)
    return;

  if (p_profile = profile_get_by_id (&g_profile_list, p_current_connection_tab->profile_id))
    profile_edit (p_profile);

  apply_profile (p_current_connection_tab, p_current_connection_tab->profile_id);
}
/*
GtkWidget *transferQueueWindow = 0;
GtkAdjustment *transferAdjustment;
gboolean gTransferWindow, gForceRefresh, gRefreshing;

gint
transfer_window_delete_event_cb (GtkWidget *window, GdkEventAny *e, gpointer data)
{
  gTransferWindow = FALSE;

  return TRUE;
}

void
sftp_transfer_cancel_cb (GtkButton *button, gpointer user_data)
{
  STransferInfo *pTi;

  pTi = (STransferInfo *) user_data;
  pTi->result = 1;
  pTi->state = TR_CANCELLED_USER;
  gForceRefresh = TRUE;
}

GtkWidget *
transfer_window_init ()
{
  GtkBuilder *builder; 
  GError *error=NULL;
  char ui[256];
  int i;

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/transfer-item.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  GtkWidget *grid = gtk_grid_new ();
  gtk_grid_set_column_homogeneous (grid, FALSE);

  STransferInfo *pTi;

  //pthread_mutex_lock(&sftp_panel.mutexQueue);
  lockSFTPQueue (__func__, TRUE);

  log_debug ("Initializating transfer window...\n");

  for (i=0; i<g_list_length (sftp_panel.queue); i++) {
    pTi = (STransferInfo *) g_list_nth (sftp_panel.queue, i)->data;

    if (gtk_builder_add_from_file (builder, ui, &error) == 0)
      {
        msgbox_error ("Can't load user interface file:\n%s", error->message);
        //return 0;
        break;
      }

    pTi->vbox_transfer = GTK_WIDGET (gtk_builder_get_object (builder, "vbox_main"));

    // Cancel button
    pTi->button_cancel = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
    gtk_button_set_image (GTK_BUTTON(pTi->button_cancel), gtk_image_new_from_icon_name ("process-stop", GTK_ICON_SIZE_BUTTON));
    g_signal_connect (G_OBJECT (pTi->button_cancel), "clicked", G_CALLBACK (sftp_transfer_cancel_cb), pTi);

    gboolean sensitive;

    sensitive = pTi->state <= TR_PAUSED ? TRUE : FALSE;
    
    gtk_widget_set_sensitive (pTi->button_cancel, sensitive);

    pTi->label_status = GTK_WIDGET (gtk_builder_get_object (builder, "label_status"));
    pTi->label_from = GTK_WIDGET (gtk_builder_get_object (builder, "label_filename_from"));
    pTi->label_speed = GTK_WIDGET (gtk_builder_get_object (builder, "label_speed"));
    pTi->label_time_left = GTK_WIDGET (gtk_builder_get_object (builder, "label_time_left"));
    pTi->progress_transfer = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar1"));

    gtk_grid_attach (grid, pTi->vbox_transfer, 0, i, 1, 1);

    //log_debug ("Added to transfer window: %s\n", pTi->label_from);
  }

  //pthread_mutex_unlock(&sftp_panel.mutexQueue);
  lockSFTPQueue (__func__, FALSE);

  gtk_widget_show_all (grid);

  return (grid);
}

void
transfer_window_refresh ()
{
  int i;
  STransferInfo *pTi;

  lockSFTPQueue (__func__, TRUE);

  log_debug ("Refreshing transfer window...\n");

  for (i=0; i<g_list_length (sftp_panel.queue); i++) {
    pTi = (STransferInfo *) g_list_nth (sftp_panel.queue, i)->data;

    gchar status[512];
    sprintf (status, "%s %s", pTi->action == SFTP_ACTION_UPLOAD ? "upload" : "download", getTransferStatusDesc (pTi->state));

    if (pTi->state == TR_CANCELLED_ERRORS) {
      strcat (status, ":\n");
      strcat (status, pTi->errorDesc);
    }

    gtk_label_set_text (GTK_LABEL(pTi->label_status), status);
    gtk_label_set_text (GTK_LABEL(pTi->label_from), pTi->source);
  
#if (GTK_MAJOR_VERSION == 3)
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR(pTi->progress_transfer), TRUE);
#endif

    gboolean sensitive;

    sensitive = pTi->state <= TR_PAUSED ? TRUE : FALSE;
    
    gtk_widget_set_sensitive (pTi->button_cancel, sensitive);


    time_t current_time;
    double elapsed;
    char speed[32], time_left[16];
    uint64_t seconds_left;

    time (&current_time); 
    
    if (pTi->state == TR_IN_PROGRESS)
      {
        elapsed = difftime (current_time, pTi->start_time);
        sprintf (speed, "%.1f MB/sec.", elapsed > 0.0 ? (float) pTi->worked / (elapsed * 1024.0 * 1024.0) : 0.0);

        if (pTi->sourceIsDir) {
          strcpy (time_left, "unknown");
          gtk_progress_bar_pulse (GTK_PROGRESS_BAR(pTi->progress_transfer));
        }
        else {
          seconds_left = ((elapsed * pTi->size) / pTi->worked) - elapsed;
          seconds_to_hhmmdd (seconds_left, time_left);
        }
        
        gtk_label_set_text (GTK_LABEL(pTi->label_speed), speed);
        gtk_label_set_text (GTK_LABEL(pTi->label_time_left), time_left);
      }
    
    // Show progress or 100% if not directory

    if (!pTi->sourceIsDir || pTi->state == TR_COMPLETED)
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(pTi->progress_transfer), pTi->size > 0 ? (float) pTi->worked/pTi->size : 0.0);
  }

  //pthread_mutex_unlock(&sftp_panel.mutexQueue);
  lockSFTPQueue (__func__, FALSE);
}

void
view_transfer_window ()
{
  int i;
  GtkWidget *grid;

  grid = 0;

  // Create the window
  transferQueueWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    
  gtk_window_set_modal (GTK_WINDOW (transferQueueWindow), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (transferQueueWindow), GTK_WINDOW (main_window));
  gtk_window_set_position (GTK_WINDOW (transferQueueWindow), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_title (GTK_WINDOW (transferQueueWindow), _("Transfer queue"));

  GdkScreen *screen;
  screen = gtk_window_get_screen (GTK_WINDOW (main_window));
  gtk_widget_set_size_request (GTK_WIDGET (transferQueueWindow), gdk_screen_get_height (screen)/1.5, gdk_screen_get_height (screen)/1.8);

  g_signal_connect (transferQueueWindow, "delete_event", G_CALLBACK (transfer_window_delete_event_cb), NULL);
  //gtk_widget_show_all (p_qlv->scrolled_window);

  // Scrolled window
  GtkWindow *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_ETCHED_IN);
  transferAdjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled_window));
  //g_signal_connect (transferAdjustment, "changed", G_CALLBACK (transfer_adj_changed_cb), NULL);

  grid = transfer_window_init ();

  if (grid)
#if GTK_CHECK_VERSION(3, 8, 0)
    // Since 3.8 will automatically add a GtkViewport if the child doesn’t implement GtkScrollable
    gtk_container_add (GTK_CONTAINER (scrolled_window), grid);
#else
    gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window), grid);
#endif

  gtk_container_add (GTK_CONTAINER (transferQueueWindow), scrolled_window);
  
  gtk_window_set_position (GTK_WINDOW (transferQueueWindow), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_widget_show_all (transferQueueWindow);

  time_t current_time, last_update;

  gdouble adjValue = gtk_adjustment_get_value (transferAdjustment);
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
\/*
        children = gtk_container_get_children(GTK_CONTAINER(scrolled_window));

        for (iter = children; iter != NULL; iter = g_list_next(iter))
          gtk_widget_destroy(GTK_WIDGET(iter->data));

        g_list_free(children);

        grid = transfer_window_refresh ();

        if (grid)
          gtk_container_add (GTK_CONTAINER (scrolled_window), grid);
*\/
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
*/
void
view_toolbar ()
{
  GtkWidget *toggle;

  toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Toolbar"));

  if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (toggle)))
    {
      gtk_widget_show_all (main_toolbar);
      prefs.toolbar = 1;
    }
  else
    {
      gtk_widget_hide (main_toolbar);
      prefs.toolbar = 0;
    }
}

void
view_statusbar ()
{
  GtkWidget *toggle;

  toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Statusbar"));

  if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (toggle)))
    {
      gtk_widget_show_all (statusbar);
      prefs.statusbar = 1;
    }
  else
    {
      gtk_widget_hide (statusbar);
      prefs.statusbar = 0;
    }
}

void
show_sidebar (gboolean show)
{
  /* Set resize flag for all the tabs. Will be checked in contents_changed_cb() */
  set_window_resized_all (1);
        
  if (show)
    {
      gtk_widget_show_all (notebook_sidebar);
      sftp_spinner_stop (); // Hide spinner
      sftp_end (); // Hide stop button
      prefs.show_sidebar = 1;
   
      if (p_current_connection_tab)
        {
          sftp_panel_show_filter ((gboolean) p_current_connection_tab->ssh_info.filter);
        }
      else
        sftp_panel_show_filter (FALSE);

      /* grab focus */
      gtk_widget_grab_focus (notebook_sidebar);
    }
  else
    {
      gtk_widget_hide (notebook_sidebar);
      prefs.show_sidebar = 0;
    }
}

void
view_sidebar ()
{
  GtkWidget *toggle;

  toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Sidebar"));

  show_sidebar (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (toggle)));
}

void
view_fullscreen ()
{
  GtkWidget *toggle;

  toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Fullscreen"));

  if (prefs.fullscreen)
    {
      gtk_window_unfullscreen (GTK_WINDOW (main_window));
      prefs.fullscreen = 0;
      /* GTK_CHECK_MENU_ITEM (toggle)->active = 0; */
    }
  else
    {
      gtk_window_fullscreen (GTK_WINDOW (main_window));
      prefs.fullscreen = 1;
      /* GTK_CHECK_MENU_ITEM (toggle)->active = 1; */
    }
}

void view_go_back () { gtk_notebook_prev_page (GTK_NOTEBOOK (notebook)); }
void view_go_forward () { gtk_notebook_next_page (GTK_NOTEBOOK (notebook)); }

void zoom_in() { if (p_current_connection_tab) adjust_font_size (p_current_connection_tab->vte, 1); }
void zoom_out() { if (p_current_connection_tab) adjust_font_size (p_current_connection_tab->vte, -1); }
void zoom_100() { if (p_current_connection_tab) adjust_font_size (p_current_connection_tab->vte, 0); }

void
terminal_reset ()
{
  if (!p_current_connection_tab)
    return;

  vte_terminal_reset (VTE_TERMINAL (p_current_connection_tab->vte), TRUE, FALSE);
}

void
moveTab(struct ConnectionTab *connTab, GtkWidget *child, GtkWidget *notebookFrom, GtkWidget *notebookTo)
{
  //GtkWidget *child = gtk_notebook_get_nth_page (notebookFrom, gtk_notebook_get_current_page (notebookFrom));
  //GtkWidget *child = p_current_connection_tab->hbox_terminal;

  //log_debug("%s\n", child == p_current_connection_tab->hbox_terminal ? "YES" : "NO");

  // Detach tab label
  GtkWidget *tab_label = gtk_notebook_get_tab_label (GTK_NOTEBOOK(notebookFrom), child);
  g_object_ref(tab_label);
  gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(tab_label)), tab_label);

  // Move terminal to the new notebook
  g_object_ref(child);
  gtk_container_remove(GTK_CONTAINER(notebookFrom), child);
  //gtk_container_add(GTK_CONTAINER(notebookTo), child);
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebookTo), child, tab_label, gtk_label_new (p_current_connection_tab->connection.name));
  connTab->notebook = notebookTo;

  // Assign the tab label
  //gtk_notebook_set_tab_label (GTK_NOTEBOOK(notebookTo), child, tab_label);
}

void
terminal_detach (GtkOrientation orientation)
{
  struct ConnectionTab *currentTab;

 /* if (!p_current_connection_tab)
    return;*/

  if (gtk_notebook_get_n_pages (GTK_NOTEBOOK(notebook)) < 2)
    return;

  // Get the current connection tab in the main notebook
  GtkWidget *child = gtk_notebook_get_nth_page (GTK_NOTEBOOK(notebook), gtk_notebook_get_current_page (GTK_NOTEBOOK(notebook)));
  //GtkWidget *child = currentTab->hbox_terminal;
  currentTab = get_connection_tab_from_child (child);

  log_write ("Detaching %s\n", currentTab->connection.name);

  GtkWidget *parent; // Main notebook parent
  GtkWidget *hpaned_split;

  // Detaching a tab causes switching to another tab, so disable switch management
  switch_tab_enabled = FALSE;

  // Create a new paned window
#if (GTK_MAJOR_VERSION == 2)
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    hpaned_split = gtk_hpaned_new ();
  else
    hpaned_split = gtk_vpaned_new ();
#else
  hpaned_split = gtk_paned_new (orientation);
#endif

  // Get the notebook parent
  parent = gtk_widget_get_parent(notebook);

  // Re-parent main notebook
  g_object_ref(notebook);
  gtk_container_remove(GTK_CONTAINER(parent), notebook);
  //gtk_container_add(GTK_CONTAINER(hpaned_split), notebook);
  gtk_paned_add1(GTK_PANED(hpaned_split), notebook);

  // Create a new notebook
  GtkWidget *notebook_new = gtk_notebook_new ();
  gtk_paned_add2(GTK_PANED(hpaned_split), notebook_new);

  // Move connection tab
  moveTab(currentTab, child, notebook, notebook_new);

  // Assign the tab label
  //gtk_notebook_set_tab_label (GTK_NOTEBOOK(notebook_new), child, tab_label);

  gtk_widget_show_all(hpaned_split);

  // Add the new paned window to the main one
  if (parent == hpaned)
    gtk_paned_add2(GTK_PANED(hpaned), hpaned_split); // First division
  else
    gtk_paned_add1(GTK_PANED(parent), hpaned_split);

  // Do an iteration, otherwise allocation is not correctly set
  lterm_iteration ();

  GtkAllocation allocation;
  gtk_widget_get_allocation (hpaned_split, &allocation);

  log_debug ("naturalSize.width = %d\n", allocation.width);
  log_debug ("naturalSize.height = %d\n", allocation.height);

  gtk_paned_set_position (GTK_PANED(hpaned_split), 
                          orientation == GTK_ORIENTATION_HORIZONTAL ? allocation.width/2 : allocation.height/2);

  switch_tab_enabled = TRUE;
}

void
terminal_detach_right()
{
  terminal_detach(GTK_ORIENTATION_HORIZONTAL);
}

void
terminal_detach_down()
{
  terminal_detach(GTK_ORIENTATION_VERTICAL);
}

void
terminal_attach_to_main (struct ConnectionTab *connectionTab)
{
  if (!connectionTab)
    return;

  GtkWidget *notebookCurrent = connectionTab->notebook;

  if (notebookCurrent == notebook)
    {
      log_debug("%s is already the main notebook\n", vte_terminal_get_window_title (VTE_TERMINAL(connectionTab->vte)));
      return;
    }

  log_write ("Attaching %s\n", vte_terminal_get_window_title (VTE_TERMINAL(connectionTab->vte)));

  // Get the widget to be moved (surely it's the second child)
  GtkWidget *vteMove = gtk_notebook_get_nth_page (GTK_NOTEBOOK(notebookCurrent), gtk_notebook_get_current_page (GTK_NOTEBOOK(notebookCurrent)));

  // Get the widget than will remain (surely it's the first child)
  GtkWidget *parent = gtk_widget_get_parent(notebookCurrent);
  GtkWidget *childRemain = gtk_paned_get_child1 (GTK_PANED(parent));

  // Move connection tab
  moveTab(connectionTab, vteMove, notebookCurrent, notebook);

  // Re-parent childRemain and destroy the paned
  g_object_ref(childRemain);
  gtk_container_remove(GTK_CONTAINER(parent), childRemain);
  gtk_widget_destroy (notebookCurrent);

  if (parent == hpaned)
    gtk_paned_add2(GTK_PANED(hpaned), childRemain);
  else
    gtk_paned_add1(GTK_PANED(parent), childRemain);
  
  gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (notebook), vteMove, TRUE);
}

void
terminal_attach_current_to_main ()
{
  if (!p_current_connection_tab)
    return;

  terminal_attach_to_main (p_current_connection_tab);
}

void
terminal_regroup_all ()
{
  int n = 0;
  struct ConnectionTab *cTab;
  GList *item;

  if (!p_current_connection_tab)
    return;

  item = g_list_first (connection_tab_list);

  while (item)
    {
      cTab = (struct ConnectionTab *) item->data;

      if (cTab->notebook != notebook) {
        terminal_attach_to_main (cTab);
        n ++;
      }

      item = g_list_next (item);
    }
  
  log_write ("Regrouped %d tab/s\n", n);
}

gboolean
cluster_get_selected(int i)
{
  STabSelection *selectedTab = &g_array_index (tabSelectionArray, STabSelection, i);
  return (selectedTab->selected);
}

void
cluster_set_selected(int i, gboolean value)
{
  GtkTreeModel *model = GTK_TREE_MODEL (list_store_cluster);
  GtkTreeIter iter;
  GtkTreePath *path;
  gchar path_str[64];

  sprintf(path_str, "%d", i);
  path = gtk_tree_path_new_from_string (path_str);

  gtk_tree_model_get_iter (model, &iter, path);

  STabSelection *selectedTab = &g_array_index (tabSelectionArray, STabSelection, i);

  selectedTab->selected = value;
  
  log_debug ("%s %s %d\n", path_str, selectedTab->pTab->connection.name, selectedTab->selected);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_CLUSTER_TERM_SELECTED, selectedTab->selected, -1);
}

void
cluster_select_all_cb (GtkButton *button, gpointer user_data)
{
  int i;

  for (i=0; i<tabSelectionArray->len; i++)
    cluster_set_selected(i, TRUE);
}

void
cluster_deselect_all_cb (GtkButton *button, gpointer user_data)
{
  int i;

  for (i=0; i<tabSelectionArray->len; i++)
    cluster_set_selected(i, FALSE);
}

void
cluster_invert_selection_cb (GtkButton *button, gpointer user_data)
{
  int i;

  for (i=0; i<tabSelectionArray->len; i++)
    cluster_set_selected(i, !cluster_get_selected(i));
}

void
terminal_toggled_cb (GtkCellRendererToggle *cell_renderer, gchar *path_str, gpointer data)
{
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);

  cluster_set_selected(atoi(path_str), !cluster_get_selected(atoi(path_str)));
}

/**
 * terminal_cluster()
 * Send the same command to the selected tabs
 */
void
terminal_cluster ()
{
  GtkBuilder *builder; 
  GError *error=NULL;
  GtkWidget *button_ok, *button_cancel;
  GtkWidget *dialog;
  char ui[256], tmp[512];

  if (g_list_length (connection_tab_list) == 0) {
    log_write ("No tabs for cluster command\n");
    return;
  }

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/cluster.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0) {
    msgbox_error ("Can't load user interface file:\n%s", error->message);
    return;
  }

  /* Create dialog */

  dialog = gtk_dialog_new_with_buttons
                 (_("Cluster"), NULL,
                  GTK_DIALOG_MODAL, 
                  "_Cancel", GTK_RESPONSE_CANCEL,
                  "_Apply", GTK_RESPONSE_OK,
                  NULL);

  GtkWidget *vbox = GTK_WIDGET (gtk_builder_get_object (builder, "vbox_cluster"));

  /* Terminal list */
  //g_selected_profile = NULL;

  GtkTreeSelection *select;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *column;
  GtkTreeModel *tree_model;

  GtkWidget *tree_view = gtk_tree_view_new ();
  
  list_store_cluster = gtk_list_store_new (N_CLUSTER_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING);

  /* Selected */
  GtkCellRenderer/*Toggle*/ *toggle_cell = gtk_cell_renderer_toggle_new ();
  g_signal_connect (toggle_cell, "toggled", G_CALLBACK (terminal_toggled_cb), GTK_TREE_MODEL (list_store_cluster));
  column = gtk_tree_view_column_new_with_attributes (_("Enabled"), toggle_cell, "active", COLUMN_CLUSTER_TERM_SELECTED, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));

  /* Name */
  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Tab"), cell, "text", COLUMN_CLUSTER_TERM_NAME, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(tree_view), TRUE);

  // Populate list
  GtkTreeIter iter;
  GList *item;
  struct ConnectionTab *p_ct;
  int i = 0;

  gtk_list_store_clear (list_store_cluster);

  // Create an array of integers for toggle terminals
  tabSelectionArray = g_array_new(FALSE, TRUE, sizeof (STabSelection));

  int nAdded = 0;
  for (i=0; i<g_list_length (connection_tab_list); i++)
    {
      item = g_list_nth (connection_tab_list, i);
      p_ct = (struct ConnectionTab *) item->data;
      char *label = p_ct->type == CONNECTION_LOCAL ? "local shell" : p_ct->connection.name;

      if (!/*p_ct->connected*/tabIsConnected(p_ct)) {
        log_write ("Cluster: tab %s is disconnected\n", label);
        continue;
      }

      gtk_list_store_append (list_store_cluster, &iter);
      gtk_list_store_set (list_store_cluster, &iter, 
                          COLUMN_CLUSTER_TERM_SELECTED, FALSE, 
                          COLUMN_CLUSTER_TERM_NAME, label, -1);
   
      // Init to zero (not selected)
      log_debug ("Adding %d %s...\n", nAdded, label);
      STabSelection tab = { p_ct, 0 };
      g_array_insert_val(tabSelectionArray, nAdded, tab);

      log_write ("Cluster: added %d %s\n", nAdded, label);
      nAdded ++;
    }
    
  log_write ("Cluster: %d tab/s available\n", nAdded);

  gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), GTK_TREE_MODEL (list_store_cluster));
  //g_signal_connect (tree_view, "cursor-changed", G_CALLBACK (profile_selected_cb), NULL);
  gtk_widget_show (tree_view);

  //select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  //gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

  ///*GtkWidget **/GtkTreeModel *tree_model_cluster = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));

  /* scrolled window */
  GtkWidget *scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "scrolled_terminals"));
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_ETCHED_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

  // Buttons
  GtkWidget *button_select_all = GTK_WIDGET (gtk_builder_get_object (builder, "button_select_all"));
  g_signal_connect (G_OBJECT (button_select_all), "clicked", G_CALLBACK (cluster_select_all_cb), NULL);

  GtkWidget *button_deselect_all = GTK_WIDGET (gtk_builder_get_object (builder, "button_deselect_all"));
  g_signal_connect (G_OBJECT (button_deselect_all), "clicked", G_CALLBACK (cluster_deselect_all_cb), NULL);

  GtkWidget *button_invert_selection = GTK_WIDGET (gtk_builder_get_object (builder, "button_invert_selection"));
  g_signal_connect (G_OBJECT (button_invert_selection), "clicked", G_CALLBACK (cluster_invert_selection_cb), NULL);

  // Command
  GtkWidget *entry_command = GTK_WIDGET (gtk_builder_get_object (builder, "entry_command"));

  gint w_width, w_height;
  GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (main_window));
  gtk_window_get_size (GTK_WINDOW (dialog), &w_width, &w_height);
  gtk_widget_set_size_request (GTK_WIDGET (dialog), w_width, gdk_screen_get_height (screen)/2);

  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  //gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  gint result = gtk_dialog_run (GTK_DIALOG (dialog));

  if (result == GTK_RESPONSE_OK)
    {
      log_write("Cluster command: %s\n", gtk_entry_get_text (GTK_ENTRY (entry_command)));

      for (i=0; i<tabSelectionArray->len; i++) {
        STabSelection *tab = &g_array_index (tabSelectionArray, STabSelection, i);

        if (tab->selected) {
          log_write("Sending cluster command to %s\n", tab->pTab->connection.name);
          terminal_write_child_ex(tab->pTab, gtk_entry_get_text (GTK_ENTRY (entry_command)));
          terminal_write_child_ex(tab->pTab, "\n");
        }
      }
    }
  else
    {
      //retcode = 1; /* cancel */
      //break;
    }

  g_array_free (tabSelectionArray, FALSE);
  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));
}

void
sftp_upload_files ()
{
  GtkWidget *dialog;
  GSList *files=NULL;
  gint result;
  char title[256], remote_directory[1024] = "";
  char *s_tmp;
  struct Connection *p_conn;
  
  if (p_current_connection_tab == NULL)
    return;
  
  if (p_current_connection_tab->type != CONNECTION_REMOTE 
      || p_current_connection_tab->ssh_info.ssh_node == NULL
      || p_current_connection_tab->ssh_info.ssh_node->sftp == NULL)
    return;
    
  log_debug ("Destination: %s\n", p_current_connection_tab->connection.name);
  
  /* Get pointer to the entry in the main list, so user directory can be saved */
  p_conn = get_connection_by_name (p_current_connection_tab->connection.name);
  
  /* If not found (it appens when opening a recent connection which is not the list any more) use the copy */
  if (p_conn == NULL)
    {
      log_write ("Connection not found: %s. Using active copy.\n", p_current_connection_tab->connection.name);
      p_conn = &(p_current_connection_tab->connection);
    }

  sprintf (title, _("Upload to %s (%s)"), p_current_connection_tab->connection.name, p_current_connection_tab->connection.host);

  GtkFileFilter *filter_all = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter_all, _("All files"));
  gtk_file_filter_add_pattern (filter_all, "*");
  
  dialog = gtk_file_chooser_dialog_new (title, GTK_WINDOW (main_window), 
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        "_Open", GTK_RESPONSE_ACCEPT,
                                        NULL);
                                         
  if (/*prefs.last_*/p_conn->upload_dir[0])
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), /*prefs.last_*/p_conn->upload_dir);
  
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);

l_upl_dialog:
  result = gtk_dialog_run (GTK_DIALOG (dialog));
  
  if (result == GTK_RESPONSE_ACCEPT)
    {
      files = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dialog));

      gchar *current_folder = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (dialog));
        
      if (current_folder)
        strcpy (p_conn->upload_dir, gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (dialog)));
    }
    
  gtk_widget_destroy (dialog);

  if (result == GTK_RESPONSE_ACCEPT && files)
    {
      if (p_conn = cl_get_by_name (&conn_list, p_current_connection_tab->connection.name))
        strcpy (p_conn->sftp_dir, remote_directory);

      //result = transfer_sftp (SFTP_ACTION_UPLOAD, files, &p_current_connection_tab->ssh_info, NULL); // deprecated
      result = sftp_queue_add (SFTP_ACTION_UPLOAD, files, &p_current_connection_tab->ssh_info, NULL);

      //if (result)
      //  msgbox_error (transfer_get_error ());

      g_slist_free (files);
      
      //sftp_refresh_directory_list (&p_current_connection_tab->ssh_info);
      //refresh_sftp_panel (&p_current_connection_tab->ssh_info);
    }
}

void
clipboard_cb (GtkClipboard *clipboard, const gchar *text, gpointer data)
{
  if (text == NULL)
    g_vte_selected_text = NULL;
  else
    {
      if (g_vte_selected_text)
        free (g_vte_selected_text);

      g_vte_selected_text = (char *) malloc (strlen (text) + 1);
      memcpy (g_vte_selected_text, text, strlen (text) + 1);

      log_debug ("g_vte_selected_text = %s\n", g_vte_selected_text);
    }
}
/*
char *
_vte_terminal_get_selection(VteTerminal *terminal)
{
  g_return_val_if_fail(VTE_IS_TERMINAL(terminal), NULL);

  return g_strdup (terminal->pvt->selection);
}
*/
char *
get_terminal_selection (VteTerminal *terminal)
{
  GdkDisplay *display;
  GtkClipboard *clipboard;

  if (vte_terminal_get_has_selection (terminal))
    {
      vte_terminal_copy_primary (terminal);
      display = gtk_widget_get_display (p_current_connection_tab->vte);
      clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_PRIMARY);
      gtk_clipboard_request_text (clipboard, clipboard_cb, NULL);
    }
  else
    g_vte_selected_text = NULL;

  return (g_vte_selected_text);
}

void
sftp_download_files ()
{
  GtkWidget *dialog = NULL;
  GSList *files=NULL;
  gint result;
  char *local_directory=NULL, *s_tmp;
  char pattern[1024], remote_directory[1024] = "";
  struct Connection *p_conn;

  if (p_current_connection_tab == NULL)
    return;
  
  if (p_current_connection_tab->type != CONNECTION_REMOTE 
      || p_current_connection_tab->ssh_info.ssh_node == NULL
      || p_current_connection_tab->ssh_info.ssh_node->sftp == NULL
      )
    return;
    
  if (sftp_panel_count_selected_rows () == 0)
    return;
  
  /* Get pointer to the entry in the main list, so user directory can be saved */
  p_conn = get_connection_by_name (p_current_connection_tab->connection.name);
  
  /* If not found (it appens when opening a recent connection which is not the list any more) use the copy */
  if (p_conn == NULL)
    {
      log_write ("Not found: %s. Using copy.", p_current_connection_tab->connection.name);
      p_conn = &(p_current_connection_tab->connection);
    }
  
  if (prefs.flag_ask_download)
    {
      dialog = gtk_file_chooser_dialog_new (_("Select folder for download"), GTK_WINDOW (main_window),
                                            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER /*GTK_FILE_CHOOSER_ACTION_SAVE*/,
                                            "_Cancel", GTK_RESPONSE_CANCEL,
                                            "_Save", GTK_RESPONSE_ACCEPT,
                                            NULL);
      
      gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

      if (prefs.download_dir[0])  
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), prefs.download_dir);
      else
        {
          if (/*prefs.last_*/p_conn->download_dir[0])
            gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), /*prefs.last_*/p_conn->download_dir);
        }

l_dwl_dialog:
      result = gtk_dialog_run (GTK_DIALOG (dialog));
      
      if (result == GTK_RESPONSE_ACCEPT)
        {
          local_directory = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (dialog));
        } 
      
      if (dialog)
        gtk_widget_destroy (dialog);
    }
  else
    {
      local_directory = (char *) malloc (sizeof (prefs.download_dir));
      memcpy (local_directory, prefs.download_dir, strlen (prefs.download_dir)+1);
    }

  if (result == GTK_RESPONSE_ACCEPT && local_directory)
    {
      files = sftp_panel_get_selected_files ();

      //result = transfer_sftp (SFTP_ACTION_DOWNLOAD, files, &p_current_connection_tab->ssh_info, local_directory); // deprecated
      result = sftp_queue_add (SFTP_ACTION_DOWNLOAD, files, &p_current_connection_tab->ssh_info, local_directory);

      log_debug ("result = %d\n", result);

      //if (result)
      //  msgbox_error (transfer_get_error ());

      strcpy (/*prefs.last_*/p_conn->download_dir, local_directory);
      g_free (local_directory);
    }
}

void
go_to_url (char *url)
{
  char cmd[64];
  int exit_code;
  GError *error = NULL;
  gboolean success;
  
  log_debug("%s\n", url); 

#ifdef __APPLE__
  sprintf (cmd, "open %s", url);
  exit_code = system (cmd);
#else      
  success = gtk_show_uri (NULL, url, GDK_CURRENT_TIME, &error);
  
  if (!success)
    {
      log_debug("%s() : gtk_show_uri() failed : %s\n", G_STRFUNC, error->message);

      strcpy (cmd, "");
      
      switch (get_desktop_environment ()) {
        case DE_GNOME: sprintf (cmd, "gnome-open %s", url); break;
        case DE_KDE: sprintf (cmd, "kde-open %s", url); break;
        default: sprintf (cmd, "xdg-open %s", url); break;
      }

      log_debug ("%s() : %s\n", G_STRFUNC, cmd);

      if (cmd[0])
        exit_code = system (cmd);
    }
#endif
}

void
help_home_page ()
{
  go_to_url (HTTP_HOME_PAGE);
}

gboolean
home_page_cb (GtkLabel *label, gchar *uri, gpointer user_data)
{
#ifdef DEBUG
  //printf ("home_page_cb() : %s\n", uri);
#endif
  
  go_to_url (uri);
  
  return (TRUE);
}

void
Info ()
{
  int major, minor, micro;
  char sys[256], image_filename[256];
  char s[1024], text[1024];
  char s_linked[1024];
  struct utsname info;
  GtkWidget *dialog;
  GtkWidget *hbox_title, *vbox_versions;
  FILE *fp;
  GtkBuilder *builder; 
  GError *error=NULL;
  char ui[256];

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/credits.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }
    
  dialog = gtk_dialog_new_with_buttons (_("About"), GTK_WINDOW (main_window), GTK_DIALOG_MODAL, "_Ok", GTK_RESPONSE_CLOSE, NULL);

  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));
  
  GtkWidget *vbox_main = GTK_WIDGET (gtk_builder_get_object (builder, "vbox_main"));
  
  /* logo */
  sprintf (image_filename, "%s/main_icon.png", globals.img_dir);
  GtkWidget *image_logo = GTK_WIDGET (gtk_builder_get_object (builder, "image_logo"));
  gtk_image_set_from_file (GTK_IMAGE(image_logo), image_filename);

  /* package, platform and copyright */
  GtkWidget *label_package = GTK_WIDGET (gtk_builder_get_object (builder, "label_package"));
  GtkWidget *label_platform = GTK_WIDGET (gtk_builder_get_object (builder, "label_platform"));
  
  gtk_label_set_text (GTK_LABEL(label_package), PACKAGE_STRING);
  get_system (sys);
  gtk_label_set_text (GTK_LABEL(label_platform), g_strconcat ("For ", sys, NULL));

  /* home page link */
  GtkWidget *label_home_page = GTK_WIDGET (gtk_builder_get_object (builder, "label_home_page"));
  sprintf (text, "<a href=\"%s\">%s</a>", HTTP_HOME_PAGE, HOME_PAGE);
  gtk_label_set_markup (GTK_LABEL(label_home_page), text);
  g_signal_connect (G_OBJECT (label_home_page), "activate-link", G_CALLBACK (home_page_cb), 0);

  /* credits */
  GtkWidget *label_credits = GTK_WIDGET (gtk_builder_get_object (builder, "label_credits"));
  const gchar *credits = gtk_label_get_text (GTK_LABEL(label_credits));
  gtk_label_set_markup (GTK_LABEL(label_credits), credits);
  
  /* libraries */
  GtkWidget *label_libs = GTK_WIDGET (gtk_builder_get_object (builder, "label_libs"));
  
#if (GTK_MAJOR_VERSION == 2)
  major = gtk_major_version;
  minor = gtk_minor_version;
  micro = gtk_micro_version;
#else
  major = gtk_get_major_version ();
  minor = gtk_get_minor_version ();
  micro = gtk_get_micro_version ();
#endif

  sprintf (s_linked,
           "GTK+ version %d.%d.%d\n"
           "VTE version %d.%d.%d\n"
           "%s\n"
           "libssh version %s", 
           major, minor, micro,
           VTE_MAJOR_VERSION, VTE_MINOR_VERSION, VTE_MICRO_VERSION,
           OPENSSL_VERSION_TEXT,
           ssh_version (0));
  gtk_label_set_markup (GTK_LABEL(label_libs), s_linked);
           
  gtk_container_add (GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox_main);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
  
  gtk_dialog_run (GTK_DIALOG (dialog));
  
  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));
}

#ifdef DEBUG
void
Debug ()
{
  char buffer[2048], s[1024];
  char filename[1024];
  int i, rc;
  GList *item;
  SConnectionTab *p_ct;
  
  printf ("---[ DEBUG ]---\n");

  printf ("Thread: 0x%08x\n", pthread_self ());
  printf ("GTK version: %d.%d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
  
  if (p_current_connection_tab)
    {
      printf ("Tab count: %d\n", gtk_notebook_get_n_pages (GTK_NOTEBOOK(p_current_connection_tab->notebook)));
      printf ("tab title : %s\n", vte_terminal_get_window_title (VTE_TERMINAL(p_current_connection_tab->vte)));
      printf ("changes_count = %d\n", p_current_connection_tab->changes_count);
    }
    
  lockSSH (__func__, TRUE);

  ssh_list_dump (&globals.ssh_list);

  lockSSH (__func__, FALSE);

  for (i=0; i<g_list_length (connection_tab_list); i++)
    {
      item = g_list_nth (connection_tab_list, i);
      p_ct = (struct ConnectionTab *) item->data;

      glong x, y;
      vte_terminal_get_cursor_position (p_ct->vte, &x, &y);

      sprintf (buffer, "%d %s [%s] [%s] [%s] [Cursor at %d %d]", i, p_ct->connection.name, 
               tabGetConnectionStatusDesc (tabGetConnectionStatus(p_ct)), 
               tabGetFlag (p_ct, TAB_LOGGED) ? "logged" : "not logged", 
               tabGetFlag (p_ct, TAB_CHANGED) ? "changed" : "",
               x, y);

      log_debug ("%s\n", buffer);
    }

  // Transfers dump
  if (sftp_panel.queue) {
    STransferInfo *pTi;

    log_debug ("Transfers:\n");

    lockSFTPQueue (__func__, TRUE);

    for (i=0; i<g_list_length (sftp_panel.queue); i++) {
      pTi = (STransferInfo *) g_list_nth (sftp_panel.queue, i)->data;

      log_debug ("[%d] %s %s %s\n", 
                 i,
                 pTi->action == SFTP_ACTION_UPLOAD ? "up" : "down", 
                 getTransferStatusDesc (pTi->state),
                 pTi->shortenedFilename
                );
    }

    lockSFTPQueue (__func__, FALSE);
  }

  sftp_panel_mirror_dump ();

  log_debug ("[end]\n");
}
#endif

gchar *
menu_translate (const gchar * path, gpointer data)
{
  return _((gchar *) path);
}

void
select_encoding_cb (GtkWidget *wgt, gpointer cbdata)
{
  int i, n_enc;

  if (!p_current_connection_tab)
    return;

  n_enc = sizeof (enc_array) / sizeof (struct EncodingEntry);

  for (i=0; i<n_enc; i++)
    {
      if (!strcmp (enc_array[i].name, gtk_menu_item_get_label (GTK_MENU_ITEM (wgt))))
        {
          //vte_terminal_set_encoding (VTE_TERMINAL (p_current_connection_tab->vte), enc_array[i].id);
          terminal_set_encoding (p_current_connection_tab, enc_array[i].id);
          update_statusbar();
          break;
        }
    }
}

static void
activate_radio_action (GtkAction *action, GtkRadioAction *current)
{
  if (!p_current_connection_tab)
    return;

  const gchar *name = gtk_action_get_name (GTK_ACTION (current));
  const gchar *typename = G_OBJECT_TYPE_NAME (GTK_ACTION (current));
  gboolean active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (current));
  gint value = gtk_radio_action_get_current_value (GTK_RADIO_ACTION (current));

  //vte_terminal_set_encoding (VTE_TERMINAL (p_current_connection_tab->vte), enc_array[value-1].id);
  terminal_set_encoding (p_current_connection_tab, enc_array[value-1].id);
  update_statusbar ();
}

static void
profile_radio_action_cb (GtkAction *action, GtkRadioAction *current)
{
  int id;

  if (!p_current_connection_tab)
    return;

  const gchar *name = gtk_action_get_name (GTK_ACTION (current));
  gboolean active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (current));
  gint value = gtk_radio_action_get_current_value (GTK_RADIO_ACTION (current));

  sscanf (name, "profile-%d", &id);
  log_debug ("name=%s value=%d id=%d\n", name, value, id);

  apply_profile (p_current_connection_tab, id);
}

void
add_stock_icon (GtkIconFactory *factory, gchar *location, gchar *stock_id)
{
  GtkIconSource *source = gtk_icon_source_new ();
  GtkIconSet *set = gtk_icon_set_new ();
 
  gtk_icon_source_set_filename (source, location);
  gtk_icon_set_add_source (set, source);
  gtk_icon_factory_add (factory, stock_id, set);
} 

void
create_stock_objects ()
{
  GtkIconFactory *factory;

  factory = gtk_icon_factory_new ();
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/main_icon.png", NULL), MY_STOCK_MAIN_ICON);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/terminal_32.png", NULL), MY_STOCK_TERMINAL);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/duplicate.png", NULL), MY_STOCK_DUPLICATE);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/plus.png", NULL), MY_STOCK_PLUS);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/less.png", NULL), MY_STOCK_LESS);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/pencil.png", NULL), MY_STOCK_PENCIL);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/copy.png", NULL), MY_STOCK_COPY);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/folder.png", NULL), MY_STOCK_FOLDER);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/upload.png", NULL), MY_STOCK_UPLOAD);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/download.png", NULL), MY_STOCK_DOWNLOAD);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/sidebar.png", NULL), MY_STOCK_SIDEBAR);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/splitH.png", NULL), MY_STOCK_SPLIT_H);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/splitV.png", NULL), MY_STOCK_SPLIT_V);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/regroup.png", NULL), MY_STOCK_REGROUP);   
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/cluster.png", NULL), MY_STOCK_CLUSTER); 
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/preferences.png", NULL), MY_STOCK_PREFERENCES); 
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/transfers.png", NULL), MY_STOCK_TRANSFERS); 
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/folder_up.png", NULL), MY_STOCK_FOLDER_UP); 
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/folder_new.png", NULL), MY_STOCK_FOLDER_NEW); 
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/home.png", NULL), MY_STOCK_HOME); 
  add_stock_icon (factory, g_strconcat (globals.img_dir, "/file_new.png", NULL), MY_STOCK_FILE_NEW); 
  
  gtk_icon_factory_add_default (factory);
}

static void
recent_manager_changed (GtkRecentManager *manager)
{
  log_debug ("Update recent sessions menu\n");

  /* regenerate the menu when the model changes */
  update_recent_sessions_menu ();
    
  log_debug ("Done\n");
}

#ifdef MAC_INTEGRATION
/* first move all accelerators away from <control> to <command>, and then in a second run
move the <alt> accelerators to <control> (alt doesn't work on osx) */
static void
osx_accel_map_foreach_controltometa_lcb (gpointer data, const gchar * accel_path, guint accel_key,
                                         GdkModifierType accel_mods, gboolean changed)
{
  if (accel_mods & GDK_CONTROL_MASK)
    {
      accel_mods &= (accel_mods & GDK_MOD1_MASK) ? ~GDK_MOD1_MASK : ~GDK_CONTROL_MASK;
      accel_mods |= GDK_META_MASK;
      if (!gtk_accel_map_change_entry (accel_path, accel_key, accel_mods, FALSE))
        {
          printf ("controltometa, could not change accelerator %s\n", accel_path);
        }
    }
}

static void
osx_accel_map_foreach_mod1tocontrol_lcb (gpointer data, const gchar * accel_path, guint accel_key,
                                         GdkModifierType accel_mods, gboolean changed)
{
  //log_debug("accelerator=%s\n", accel_path);
    
  if (accel_mods & GDK_MOD1_MASK)
    {
      //log_debug("accelerator %s has mod1\n", accel_path);
      accel_mods &= ~GDK_MOD1_MASK;
      accel_mods |= GDK_CONTROL_MASK;
      if (!gtk_accel_map_change_entry (accel_path, accel_key, accel_mods, FALSE))
        {
          printf ("mod1tocontrol, could not change accelerator %s\n", accel_path);
        }
    }
}
#endif

void
refresh_profile_menu ()
{
  char enc_desc[10000], item_s[256], tmp_s[32];
  struct Profile *p;
  int i;
  GList *actions, *l, *items;
  GtkAction *action;

  if (profile_menu_ui_id != 0)
    gtk_ui_manager_remove_ui (ui_manager, profile_menu_ui_id);

  actions = gtk_action_group_list_actions (profile_action_group);

  for (l = actions; l != NULL; l = l->next)
    {
      //g_signal_handlers_disconnect_by_func (GTK_ACTION (l->data), G_CALLBACK (profile_radio_action_cb), recent_entries[i].name);
      gtk_action_group_remove_action (profile_action_group, GTK_ACTION (l->data));
    }

  g_list_free (actions);

  GtkRadioActionEntry radio_enc_entries[profile_count (&g_profile_list)];

  strcpy (enc_desc, "<ui>"
                    "  <menubar name='MainMenu'>"
                    "    <menu action='TerminalMenu'>"
                    "      <menu action='ProfileMenu'>");

  p = g_profile_list.head;
  i = 0;
  
  while (p)
    {
      sprintf (tmp_s, "profile-%d", p->id);
      radio_enc_entries[i].name = g_strdup (tmp_s);
      radio_enc_entries[i].stock_id = NULL;
      radio_enc_entries[i].label = g_strdup (p->name);
      radio_enc_entries[i].accelerator = NULL;
      radio_enc_entries[i].tooltip = NULL;
      radio_enc_entries[i].value = p->id /*i+1*/;

      sprintf (item_s, "<menuitem action='%s' />", tmp_s);
      strcat (enc_desc, item_s);

      p = p->next;
      i ++;
    }

  strcat (enc_desc, "      </menu></menu>"
                    "  </menubar>"
                    "</ui>");

  gtk_action_group_add_radio_actions (profile_action_group, radio_enc_entries, G_N_ELEMENTS (radio_enc_entries), 1, 
                                      G_CALLBACK (profile_radio_action_cb), NULL);

  profile_menu_ui_id = gtk_ui_manager_add_ui_from_string (ui_manager, enc_desc, -1, NULL);
}

void
get_main_menu ()
{
  GtkAccelGroup *accel_group;
  GtkRecentManager *recent_manager;
  int i, n_enc;
  char enc_desc[10000], item_s[256], s_tmp[256];
  char recent_connections_desc[10000];

  /* build character encoding menu with actions */

  n_enc = sizeof (enc_array) / sizeof (struct EncodingEntry);

  GtkRadioActionEntry radio_enc_entries[n_enc];

  strcpy (enc_desc, "<ui>"
                    "  <menubar name='MainMenu'>"
                    "    <menu action='TerminalMenu'>"
                    "      <menu action='CharacterEncodingMenu'>");

  for (i=0; i<n_enc; i++)
    {
      radio_enc_entries[i].name = g_strdup (enc_array[i].id);
      radio_enc_entries[i].stock_id = NULL;
      radio_enc_entries[i].label = g_strdup (enc_array[i].name);
      radio_enc_entries[i].accelerator = NULL;
      radio_enc_entries[i].tooltip = NULL;
      radio_enc_entries[i].value = i+1;

      sprintf (item_s, "<menuitem action='%s' />", enc_array[i].id);
      strcat (enc_desc, item_s);
    }

  strcat (enc_desc, "    </menu></menu>"
                    "  </menubar>"
                    "</ui>");

  /* build main menu */

  action_group = gtk_action_group_new ("MenuActions");
  gtk_action_group_add_actions (action_group, main_menu_items, G_N_ELEMENTS (main_menu_items), NULL);
  gtk_action_group_add_toggle_actions (action_group, toggle_entries, G_N_ELEMENTS (toggle_entries), NULL);
  gtk_action_group_add_radio_actions (action_group, radio_enc_entries, G_N_ELEMENTS (radio_enc_entries), 1, G_CALLBACK (activate_radio_action), NULL);

  ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

  accel_group = gtk_ui_manager_get_accel_group (ui_manager);
  gtk_window_add_accel_group (GTK_WINDOW (main_window), accel_group);

  gtk_ui_manager_add_ui_from_string (ui_manager, ui_main_desc, -1, NULL);
  gtk_ui_manager_add_ui_from_string (ui_manager, enc_desc, -1, NULL);

  //g_object_set_data (G_OBJECT (widget), "ui-manager", ui_manager);
  menubar = gtk_ui_manager_get_widget (ui_manager, "/MainMenu");

  /* gtk_ui_manager_set_add_tearoffs (ui_manager, TRUE); */

  /* init entries for recent connections */

  for (i=0;i<MAX_RECENT;i++)
    {
      sprintf (s_tmp, "recent_%d", i);
      recent_entries[i].name = g_strdup (s_tmp);
    }

  //refresh_profile_menu ();


  /* recent files menu http://git.gnome.org/browse/gedit/tree/gedit/gedit-window.c?id=361135ec3c7fa7f27cebfa18f899818c0d5268c5 */

  /* create action group for recent files */
  recents_action_group = gtk_action_group_new ("SessionRecent");
  gtk_action_group_set_translation_domain (recents_action_group, NULL);
  gtk_ui_manager_insert_action_group (ui_manager, recents_action_group, 0);
  g_object_unref (recents_action_group);

  /* create action group for profiles */
  profile_action_group = gtk_action_group_new ("ProfileActions");
  gtk_action_group_set_translation_domain (profile_action_group, NULL);
  gtk_ui_manager_insert_action_group (ui_manager, profile_action_group, 0);
  g_object_unref (profile_action_group);

#ifdef MAC_INTEGRATION
  GtkWidget *menuitem;

  //GtkOSXApplication *theApp = g_object_new (GTK_TYPE_OSX_APPLICATION, NULL);
  GtkosxApplication *theApp = g_object_new (GTKOSX_TYPE_APPLICATION, NULL);
  gtkosx_application_set_menu_bar (theApp, GTK_MENU_SHELL(menubar));

  menuitem = gtk_ui_manager_get_widget (ui_manager, "/MainMenu/EditMenu/Preferences");
  gtkosx_application_insert_app_menu_item (theApp, menuitem, 5);

  menuitem = gtk_ui_manager_get_widget (ui_manager, "/MainMenu/HelpMenu/About");
  gtkosx_application_insert_app_menu_item (theApp, menuitem, 5);
  
  menuitem = gtk_ui_manager_get_widget (ui_manager, "/MainMenu/ConnectionMenu/Quit");
  gtk_widget_hide (menuitem);


// jbuild build meta-gtk-osx-themes
/*
#ifdef GTK_TYPE_OSX_APPLICATION
  log_debug(">>> type1\n");
  GtkOSXApplication *OSXApp = g_object_new (GTK_TYPE_OSX_APPLICATION, NULL);
  gtk_osxapplication_set_menu_bar (OSXApp, GTK_MENU_SHELL (menubar));
#else
  log_debug(">>> type2\n");
  GtkosxApplication *OSXApp = g_object_new (GTKOSX_TYPE_APPLICATION, NULL);
  gtkosx_application_set_menu_bar (OSXApp, GTK_MENU_SHELL (menubar));
#endif
*/

#endif
}

void
activate_search_entry (/*GtkObject *object, gpointer data*/)
{
  

  gtk_widget_grab_focus (search_entry);

  
}

gboolean
completion_match_selected_cb (GtkEntryCompletion *widget, GtkTreeModel *model, 
                              GtkTreeIter *iter, gpointer user_data)
{
  char connection_string[256];

  

  GValue value = {0, };
  gtk_tree_model_get_value(model, iter, 0, &value);
  sprintf (connection_string, "conn:@%s", g_value_get_string(&value));
  g_value_unset(&value);

  open_connection (connection_string);

  search_entry_focus_out_event_cb (search_entry, NULL, NULL);

  

  return (TRUE);
}


/* callback function when user hits Enter key */
void
search_entry_activate_cb (GtkEntry *entry, gpointer user_data)
{
  char connection_string[256];
  char connection_name[256];

  

  strcpy (connection_name, gtk_entry_get_text (entry));
  trim (connection_name);

  if (connection_name[0])
    {
      sprintf (connection_string, "conn:@%s", gtk_entry_get_text (entry));
      open_connection (connection_string);
    }

  
}

gboolean
search_entry_focus_in_event_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  

#if (GTK_MAJOR_VERSION == 2)
  const GdkColor BLACK_COLOR = { 0, 0, 0, 0 };
  gtk_widget_modify_text (widget, GTK_STATE_NORMAL, &BLACK_COLOR);
#else
  gtk_widget_override_color (search_entry, GTK_STATE_NORMAL, NULL);
#endif

  gtk_entry_set_text (GTK_ENTRY (widget), "");

  

  return (TRUE);
}

gboolean
search_entry_focus_out_event_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  

#if (GTK_MAJOR_VERSION == 2)
  const GdkColor GRAY_COLOR = { 0, 45000, 45000, 45000 };
  gtk_widget_modify_text (search_entry, GTK_STATE_NORMAL, &GRAY_COLOR);
#else
  const GdkRGBA GRAY_COLOR = { 0.6, 0.6, 0.6, 1 };
  gtk_widget_override_color (search_entry, GTK_STATE_NORMAL, &GRAY_COLOR);
#endif
  //g_object_set (widget, "foreground-set", "gray", "foreground", TRUE, NULL);

  gtk_entry_set_text (GTK_ENTRY (widget), ACCEL_SEARCH_ENTRY);

  

  return (TRUE);
}

void
create_accelerators ()
{
  GtkAccelGroup *gtk_accel = gtk_accel_group_new ();
  GClosure *closure;
  GdkModifierType mt;
  
#ifdef MAC_INTEGRATION
  mt = GDK_META_MASK;
#else
  mt = GDK_CONTROL_MASK;
#endif

  closure = g_cclosure_new (G_CALLBACK (activate_search_entry), NULL, NULL);
  gtk_accel_group_connect (gtk_accel, gdk_keyval_from_name ("k"), mt, GTK_ACCEL_VISIBLE, closure);
  g_closure_unref (closure);

  gtk_window_add_accel_group (GTK_WINDOW (main_window), gtk_accel);
}

void 
refresh_search_completion ()
{
  GtkListStore *model;
  GtkTreeIter iter; 
  struct Connection *p_conn;
  int i;

  model = (GtkListStore *) gtk_entry_completion_get_model (completion);

  if (model)
    {
      gtk_list_store_clear (model);
      g_object_unref (G_OBJECT (model));
    }

  model = gtk_list_store_new (1, G_TYPE_STRING);

  i = 0;
  while (p_conn = cl_get_by_index (/*cl_get_current ()*/ &conn_list, i))
    {
      gtk_list_store_append (model, &iter);
      gtk_list_store_set (model, &iter, 0, p_conn->name, -1); 
      i ++;
    }

  gtk_entry_completion_set_model (completion, GTK_TREE_MODEL (model));
}

GtkToolItem *
create_toolbar_separator ()
{
  GtkToolItem *separator = gtk_separator_tool_item_new ();
  gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM(separator), FALSE);
  gtk_tool_item_set_expand (separator, TRUE);
  
  return (separator);
}

void
add_toolbar (GtkWidget *box)
{
  main_toolbar = gtk_ui_manager_get_widget (ui_manager, N_("/MainToolbar"));

  /* separator */
  gtk_toolbar_insert (GTK_TOOLBAR (main_toolbar), create_toolbar_separator (), -1);
  
  /* search entry */
  GtkToolItem *item  = gtk_tool_item_new ();
  search_entry = gtk_entry_new ();
  gtk_entry_set_width_chars (GTK_ENTRY(search_entry), strlen (ACCEL_SEARCH_ENTRY));
  gtk_entry_set_icon_from_stock (GTK_ENTRY(search_entry), GTK_ENTRY_ICON_SECONDARY, "edit-find");
  g_signal_connect (search_entry, "activate", G_CALLBACK (search_entry_activate_cb), NULL);
  g_signal_connect (search_entry, "focus-in-event", G_CALLBACK (search_entry_focus_in_event_cb), NULL);
  g_signal_connect (search_entry, "focus-out-event", G_CALLBACK (search_entry_focus_out_event_cb), NULL);

  /* completion entry */
  log_write ("Creating completion entry...\n");
  completion = gtk_entry_completion_new ();
  gtk_entry_completion_set_text_column (completion, 0); /* first column */
  g_signal_connect (G_OBJECT(completion), "match-selected", G_CALLBACK (completion_match_selected_cb), NULL);
  gtk_entry_set_completion (GTK_ENTRY (search_entry), completion);
  search_entry_focus_out_event_cb (search_entry, NULL, NULL);
  gtk_container_add (GTK_CONTAINER (item), GTK_WIDGET (search_entry));
  gtk_toolbar_insert (GTK_TOOLBAR (main_toolbar), GTK_TOOL_ITEM (item), -1);

  gtk_toolbar_set_style (GTK_TOOLBAR (main_toolbar), GTK_TOOLBAR_ICONS);

  gtk_box_pack_start (GTK_BOX (box), main_toolbar, FALSE, TRUE, 0);
  gtk_widget_show (main_toolbar);

  if (prefs.toolbar)
    {
      GtkWidget *toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Toolbar"));

//#if (GTK_MAJOR_VERSION == 2)
//      GTK_CHECK_MENU_ITEM (toggle)->active = 1;
//#else
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (toggle), TRUE);
//#endif

      //gtk_widget_show (main_toolbar);
    }

  toolbar_recent_menu = setup_toolbar_connect_button (main_toolbar);
}

/**
 * create_statusbar() - Creates the statusbar
 */
void create_statusbar()
{
  gint cw = -1;
  PangoLayout *layout;

#if (GTK_MAJOR_VERSION == 2)
  statusbar = gtk_hbox_new (FALSE, 0);
#else
  statusbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
#endif

  sb_msg = gtk_statusbar_new();
  //gtk_statusbar_set_has_resize_grip (GTK_STATUSBAR (sb_msg), FALSE);

  layout = gtk_widget_create_pango_layout (sb_msg, "M");

  if (layout != NULL)
    {
      pango_layout_get_pixel_size (layout, &cw, NULL);
      g_object_unref (G_OBJECT (layout));
    }

  sb_transfer = gtk_statusbar_new ();
  //gtk_widget_set_size_request (sb_transfer, 20*cw, -1);

  sb_protocol = gtk_statusbar_new ();
  //gtk_widget_set_size_request (sb_protocol, 10*cw, -1);

  sb_enc = gtk_statusbar_new ();
  //gtk_widget_set_size_request (sb_enc, 8*cw, -1);

  gtk_box_pack_start (GTK_BOX (statusbar), sb_msg, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (statusbar), sb_enc, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (statusbar), sb_transfer, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (statusbar), sb_protocol, FALSE, FALSE, 0);

  if (prefs.statusbar)
    {
      //gtk_widget_show_all (statusbar);
      GtkWidget *toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Statusbar"));

//#if (GTK_MAJOR_VERSION == 2)
//      GTK_CHECK_MENU_ITEM (toggle)->active = 1;
//#else
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (toggle), TRUE);
//#endif
    }
}

/**
 * update_title() - Updates the window title
 */
void
update_title ()
{
  char title[256];
  char appname[256];
  char label[256];

  if (p_current_connection_tab)
    {
      strcpy (label, p_current_connection_tab->connection.name);
    }
  else
    {
      strcpy (label, "");
    }

  sprintf (appname, "%s %s", PACKAGE, VERSION);

  if (label[0] != 0)
    sprintf (title, "%s - %s", label, appname);
  else
    strcpy (title, appname);

  gtk_window_set_title (GTK_WINDOW (main_window), title);
}

void
statusbar_push (const char *fmt, ...)
{
  va_list ap;
  char msg[2048];
  gint result;

  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);
  
  log_write ("%s\n", msg);

  gtk_statusbar_push (GTK_STATUSBAR (sb_msg), 0, msg);

//  while (gtk_events_pending ())
//    gtk_main_iteration ();
}

void
statusbar_pop ()
{
  gtk_statusbar_pop (GTK_STATUSBAR (sb_msg), 0);

//  while (gtk_events_pending ())
//    gtk_main_iteration ();
}

/**
 * statusbar_msg()
 */
void 
statusbar_msg (const char *fmt, ...)
{
  va_list ap;
  char msg[2048];
  gint result;

  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);
  
  log_write ("%s\n", msg);

  gtk_statusbar_pop (GTK_STATUSBAR (sb_msg), 0);
  gtk_statusbar_push (GTK_STATUSBAR (sb_msg), 0, msg);
  
  ///statusbar_pop ();
  //statusbar_push (msg);

//  while (gtk_events_pending ())
//    gtk_main_iteration ();
}

/**
 * update_statusbar() - Updates the status bar
 */
void 
update_statusbar ()
{
  char s[256];
  //char emulation[256];
  char encoding[256];
  char protocol[256];
  
  //log_debug("Start\n");

  strcpy (protocol, "");

  gtk_statusbar_pop (GTK_STATUSBAR (sb_msg), 0);
  //gtk_statusbar_pop (GTK_STATUSBAR (sb_transfer), 0);
  gtk_statusbar_pop (GTK_STATUSBAR (sb_enc), 0);
  gtk_statusbar_pop (GTK_STATUSBAR (sb_protocol), 0);

  if (p_current_connection_tab)
    {
      /* set emulation and encoding */
      
      if (VTE_IS_TERMINAL (p_current_connection_tab->vte))
        {
          //strcpy (emulation, vte_terminal_get_emulation (VTE_TERMINAL (p_current_connection_tab->vte)));
          //strcpy (emulation, "xterm");
          strcpy (encoding, vte_terminal_get_encoding (VTE_TERMINAL (p_current_connection_tab->vte)));
        }
      else
        {
          //strcpy (emulation, "");
          strcpy (encoding, "");
        }
            
      /* set message text */
      
      if (/*p_current_connection_tab->connected*/tabIsConnected(p_current_connection_tab))
        {
          if (p_current_connection_tab->type == CONNECTION_REMOTE)
            {
              sprintf (s, "%s@%s (%s)", 
                      p_current_connection_tab->connection.user, 
                      p_current_connection_tab->connection.name, 
                      p_current_connection_tab->connection.host);
              
              strcpy (protocol, p_current_connection_tab->connection.protocol);
            }
          else /* local */
            {
              strcpy (s, "Terminal on local host");
            }
        }
      else
        {
          strcpy (s, "Disconnected");
        }
    }
  else
    {
      strcpy (s, "");
      //strcpy (emulation, "");
      strcpy (encoding, "");
      strcpy (protocol, "");
    }
    
  //log_debug("set %s %s %s\n", s, encoding, protocol);

  //log_debug ("Pushing msg...\n");
  gtk_statusbar_push (GTK_STATUSBAR (sb_msg), 0, s);
/*
  int nUp, nDown;
  char transferReport[256];

  sftp_queue_count (&nUp, &nDown);
  sprintf (transferReport, "Uploads: %d, Downloads: %d", nUp, nDown);

  gtk_statusbar_push (GTK_STATUSBAR (sb_transfer), 0, transferReport);
*/
  //log_debug ("Pushing encoding...\n");
  gtk_statusbar_push (GTK_STATUSBAR (sb_enc), 0, encoding);
  //log_debug ("Pushing protocol...\n");
  gtk_statusbar_push (GTK_STATUSBAR (sb_protocol), 0, protocol);

  //log_debug("End\n");
}

gboolean 
update_statusbar_upload_download ()
{
  int nUp, nDown;
  char transferReport[256];

  log_debug ("Getting number of uploads and downloads...\n");

  sftp_queue_count (&nUp, &nDown);
  sprintf (transferReport, "Uploads: %d, Downloads: %d", nUp, nDown);

  log_debug ("%s\n", transferReport);

  gtk_statusbar_pop (GTK_STATUSBAR (sb_transfer), 0);
  gtk_statusbar_push (GTK_STATUSBAR (sb_transfer), 0, transferReport);

  return (G_SOURCE_REMOVE);
}

/**
 * update_screen_info() - Refresh title and statusbar
 */
void
update_screen_info ()
{
  update_title ();
  update_statusbar ();
}

void 
hyperlink_connect_host ()
{
  struct Connection *p_conn;
  
  log_debug ("g_match.matched_string=%s\n", g_match.matched_string);

  if (g_match.matched_string == NULL) return;

  p_conn = get_connection_by_host (g_match.matched_string);
  
  if (p_conn)
    connection_log_on_param (p_conn);
}

void 
hyperlink_edit_host ()
{
  struct Connection *p_conn = NULL;
  struct GroupNode *p_node = NULL;
  
  log_debug ("g_match.matched_string=%s\n", g_match.matched_string);

  if (g_match.matched_string == NULL) return;

  p_conn = get_connection_by_host (g_match.matched_string);
  
  if (p_conn)
    p_node = group_node_find (group_tree_get_root (&g_groups), p_conn->name);
 
  if (p_node)
    p_node = add_update_connection (p_node, NULL);
}

void 
hyperlink_add_host ()
{
  struct Connection conn;
  struct GroupNode *p_node;
  
  log_debug ("g_match.matched_string=%s\n", g_match.matched_string);

  if (g_match.matched_string == NULL) return;
  
  memset (&conn, 0, sizeof (struct Connection));
  
  strcpy (conn.name, g_match.matched_string);
  strcpy (conn.host, g_match.matched_string);
  strcpy (conn.protocol, "ssh");
  conn.port = 22;
  //strcpy (conn.emulation, "xterm");

  p_node = add_update_connection (NULL, &conn);
}

void
terminal_popup_menu (GdkEventButton *event)
{
  GtkUIManager *ui_manager;
  GtkActionGroup *action_group;
  GtkWidget *popup;
  struct Connection *p_conn;
  char conn_desc[10000], ip_desc[1024], item_s[256];
  char s_tmp[256];
  int n_conn, i;

  /* Build PasteHost submenu */
  
  n_conn = count_current_connections ();
  
  log_debug ("n_conn=%d\n", n_conn);

  GtkActionEntry conn_entries[n_conn];

  /* Add IP address menu items */
  
  log_debug ("tag=%d matched_string=%s\n", g_match.tag, g_match.matched_string);
  
  if (g_match.tag >= 0 && g_match.matched_string)
    {
      p_conn = get_connection_by_host (g_match.matched_string);
       
      strcpy (ip_desc, "<ui>"
                       "  <popup name='TermPopupMenu'>");
      
      if (p_conn)
        {
          strcat (ip_desc, "<menuitem action='ConnectHost' />");
          strcat (ip_desc, "<menuitem action='EditHost' />");
        }
      else
        {
          strcat (ip_desc, "<menuitem action='AddHost' />");
        }
        
      strcat (ip_desc, "<separator />");  
  
      strcat (ip_desc, "  </popup>"
                       "</ui>");
    }

  strcpy (conn_desc, "<ui>"
                     "  <popup name='TermPopupMenu'>");

  /* Add PasteHost menu */
  
  strcat (conn_desc, "    <menu action='PasteHost'>");

  for (i=0; i<n_conn; i++)
    {
      p_conn = get_connection_by_index (i);
      //sprintf (s_tmp, "pastehost_%d", i);
      conn_entries[i].name = g_strdup (p_conn->name);
      conn_entries[i].stock_id = NULL;
      sprintf (s_tmp, "%s (%s)", p_conn->name, p_conn->host);
      conn_entries[i].label = g_strdup (s_tmp);
      conn_entries[i].accelerator = NULL;
      conn_entries[i].tooltip = NULL;
      //conn_entries[i].value = i+1;
      conn_entries[i].callback = G_CALLBACK (paste_host);

      sprintf (item_s, "<menuitem action='%s' />", conn_entries[i].name);
      strcat (conn_desc, item_s);
    }

  strcat (conn_desc, "    </menu>");
  strcat (conn_desc, "  </popup>"
                     "</ui>");
  
  //printf ("%s\n", conn_desc);
  
  log_debug ("Creating group\n");
  
  action_group = gtk_action_group_new ("TermPopupActions");
  gtk_action_group_add_actions (action_group, popup_menu_items, G_N_ELEMENTS (popup_menu_items), NULL);
  gtk_action_group_add_actions (action_group, conn_entries, G_N_ELEMENTS (conn_entries), NULL);

  ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

  log_debug ("Adding ui\n");

  if (g_match.tag >= 0 && g_match.matched_string)
    gtk_ui_manager_add_ui_from_string (ui_manager, ip_desc, -1, NULL);
  
  gtk_ui_manager_add_ui_from_string (ui_manager, ui_popup_desc, -1, NULL);
  gtk_ui_manager_add_ui_from_string (ui_manager, conn_desc, -1, NULL);

  //g_object_set_data (G_OBJECT (widget), "ui-manager", ui_manager);
  popup = gtk_ui_manager_get_widget (ui_manager, "/TermPopupMenu");
  /*
  GtkWidget *download = gtk_ui_manager_get_widget (ui_manager, N_("/TermPopupMenu/Download files"));
  
  if (p_current_connection_tab && p_current_connection_tab->type == CONNECTION_LOCAL)
    {
      gtk_widget_set_sensitive (download, FALSE);
    }
  else
    {
      gtk_widget_set_sensitive (download, TRUE);
    }
  */
  log_debug ("Show popup menu\n");
  
  gtk_menu_popup (GTK_MENU (popup), NULL, NULL, NULL, NULL,
                  (event != NULL) ? event->button : 0,
                  gdk_event_get_time ((GdkEvent *) event));
}

gint
delete_event_cb (GtkWidget *window, GdkEventAny *e, gpointer data)
{
  application_quit ();

  return TRUE;
}

void
size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
  //log_debug ("\n");
  
  if (allocation)
    {
#ifdef DEBUG
      /* printf ("size_allocate_cb() : (%d,%d) %d x %d\n", allocation->x, allocation->y, allocation->width, allocation->height); */
#endif
  
      /* Set resize flag for all the tabs. Will be checked in contents_changed_cb() */
      if (allocation->width != prefs.w || allocation->height != prefs.h)
        set_window_resized_all (1);

      /* store new size */
      prefs.w = allocation->width;
      prefs.h = allocation->height;
    }

  if (p_current_connection_tab)
    {
      if (/*p_current_connection_tab->connected*/tabIsConnected(p_current_connection_tab))
        {
          if (VTE_IS_TERMINAL (p_current_connection_tab->vte))
            {
              /* store rows and columns */
              prefs.rows = vte_terminal_get_row_count (VTE_TERMINAL (p_current_connection_tab->vte));
              prefs.columns = vte_terminal_get_column_count (VTE_TERMINAL (p_current_connection_tab->vte));
            }
        }
    }
}

/**
 * child_exited_cb() - This signal is emitted when the terminal detects that a child started using vte_terminal_fork_command() has exited
 */
#if (VTE_CHECK_VERSION(0,38,2) == 1)
void
child_exited_cb (VteTerminal *vteterminal,
               gint         status,
               gpointer     user_data)
#else
void
child_exited_cb (VteTerminal *vteterminal,
               gpointer     user_data)
#endif
{
  struct ConnectionTab *p_ct;
  struct Protocol *p_prot;
  int code;

  //code = vte_terminal_get_child_exit_status (vteterminal);
  p_ct = (struct ConnectionTab *) user_data;

  log_debug ("ptr = %ld\n", (unsigned int) p_ct);

  log_write ("%s\n", p_ct->connection.name);
  //log_write ("[%s] : %s status=%d\n", __func__, p_ct->connection.name, status);
/*
  p_ct->connected = 0;
  p_ct->logged = 0;
  p_ct->auth_state = AUTH_STATE_NOT_LOGGED;
*/
  tabInitConnection(p_ct);

  /* in case of remote connection save it and keep tab, else remove tab */

  if (p_ct->type == CONNECTION_REMOTE)
    {
      connection_copy (&p_ct->last_connection, &p_ct->connection);
  
      //connection_tab_set_status (p_ct, TAB_STATUS_DISCONNECTED);
      refreshTabStatus (p_ct);
      
      log_debug ("Disconnecting\n");
      lt_ssh_disconnect (&p_ct->ssh_info);
       
      if (p_current_connection_tab == p_ct) {
        log_debug ("Refreshing panel\n");
        refresh_sftp_panel (&p_ct->ssh_info);
      }

      log_debug ("Checking protocol settings\n");

      p_prot = get_protocol (&g_prot_list, p_ct->connection.protocol);
      
      if (p_prot->flags & PROT_FLAG_DISCONNECTCLOSE)
        // Enqueue tab closing instead of calling connection_tab_close(). Freezes program if called here.
        //connection_tab_close (p_ct);
        ifr_add (ITERATION_CLOSE_TAB, p_ct);
      else
        terminal_write_ex (p_ct, "\n\rDisconnected. Hit enter to reconnect.\n\r", -1);
    }
  else {
    // Enqueue tab closing instead of calling connection_tab_close(). Freezes program if called here.
    //connection_tab_close (p_ct);
    ifr_add (ITERATION_CLOSE_TAB, p_ct);
  }

  update_screen_info ();

  log_debug ("end\n");
}

/**
 * eof_cb() - Emitted when the terminal receives an end-of-file from a child which is running in the terminal (usually after "child-exited")
 */
void
eof_cb (VteTerminal *vteterminal, gpointer user_data)
{
  struct ConnectionTab *p_ct;

  log_debug ("\n");

  //g_signal_connect (G_OBJECT (vteterminal), "contents-changed", NULL, NULL);

  p_ct = (struct ConnectionTab *) user_data;
  
  log_write ("[%s] : %s\n", __func__, p_ct->connection.name);

  connection_copy (&p_ct->last_connection, &p_ct->connection);
/*
  p_ct->connected = 0;
  p_ct->logged = 0;
  p_ct->auth_state = AUTH_STATE_NOT_LOGGED;
*/
  tabInitConnection(p_ct);
  
  update_screen_info ();
  
  //connection_tab_set_status (p_ct, TAB_STATUS_DISCONNECTED);
  refreshTabStatus (p_ct);
  
  lt_ssh_disconnect (&p_ct->ssh_info);
  
  if (p_current_connection_tab == p_ct)
    refresh_sftp_panel (&p_ct->ssh_info);
  
  //vte_terminal_feed (VTE_TERMINAL(p_ct->vte), "\n\rDisconnected. Hit enter to reconnect.\n\r", -1);
}

/**
 * status_line_changed_cb()
 */
void
status_line_changed_cb (VteTerminal *vteterminal, gpointer user_data)
{
  //printf ("status_line_changed_cb() : %s\n", vte_terminal_get_status_line (vteterminal));
}

gboolean
button_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
  VteTerminal *vte = VTE_TERMINAL (userdata);
  int row, col, tag;
  GtkBorder *inner_border = NULL;
  struct Connection *p_conn;

  /* Get matched text */
  
  //gtk_widget_style_get (widget, "inner-border", &inner_border, NULL);
  col = (event->x - (inner_border ? inner_border->left : 0)) / vte_terminal_get_char_width (vte);
  row = (event->y - (inner_border ? inner_border->top : 0)) / vte_terminal_get_char_height (vte);
  //gtk_border_free (inner_border);
  
  log_debug ("row=%d, col=%d\n", row, col);

  /* If there is no user selection, check matched strings */
  if (vte_terminal_get_has_selection (vte) == FALSE)
    g_match.matched_string = vte_terminal_match_check (vte, col, row, &g_match.tag);

  if (g_match.matched_string)
    {
      log_debug ("matched_string=%s\n", g_match.matched_string);
    }

  if (event->type == GDK_BUTTON_PRESS)
    {
      if (event->button == 3)
        {
          log_write ("Right button pressed\n");

          if (prefs.mouse_paste_on_right_button)
            vte_terminal_paste_clipboard (VTE_TERMINAL (userdata /*p_current_connection_tab->vte*/));
          else
            terminal_popup_menu (event);

          return TRUE;
        }
      else if (event->button == 1)
        {
          //log_write ("Left button pressed\n");
          
          if (prefs.hyperlink_click_enabled && g_match.matched_string)
            {
              if (p_conn = get_connection_by_host (g_match.matched_string))
                hyperlink_connect_host ();
              else
                hyperlink_add_host ();
              
              return TRUE;
            }
        }
    }

  return FALSE;
}

void
window_title_changed_cb (VteTerminal *vteterminal, gpointer user_data)
{
  char title[1024];
  struct ConnectionTab *p_ct;

  log_debug ("%s\n", vte_terminal_get_window_title (vteterminal));

  p_ct = (struct ConnectionTab *) user_data;

  if (p_ct->type == CONNECTION_LOCAL)
    {
      strcpy (title, vte_terminal_get_window_title (vteterminal));

      /* set connection name for window title */

      strcpy (p_ct->connection.name, title);

      /* set the tab label */
/*
      if (strlen (title) > 25)
        {
          title[25] = 0;
          strcat (title, " ...");
        }
*/
      char tmpTitle[64];
      shortenString (title, 25, tmpTitle);

      gtk_label_set_text (GTK_LABEL (p_ct->label), tmpTitle);

      /* update session file */

      if (!prefs.save_session)
        save_session_file (NULL);
    }
  else
    {
      if (lt_ssh_is_connected (&p_ct->ssh_info))
        {
          if (p_ct->ssh_info.follow_terminal_folder)
            follow_terminal_folder ();
        }
    }

  update_title ();
}

void
selection_changed_cb (VteTerminal *vteterminal, gpointer user_data)
{
  //log_debug ("\n");

  if (prefs.mouse_copy_on_select)
    vte_terminal_copy_clipboard (vteterminal);
}

void
contents_changed_cb (VteTerminal *vteterminal, gpointer user_data)
{
  struct ConnectionTab *p_ct;
  glong _cx, _cy, last_x, last_y;
  int nLines, i;
  //GArray *attrs;

  if (p_current_connection_tab == NULL)
    return;

  p_ct = (struct ConnectionTab *) user_data;
  
  //log_debug ("%s\n", p_ct->connection.name);

  //p_prot = get_protocol (&g_prot_list, p_ct->connection.protocol);
  
  /* save terminal buffer */
  
  p_ct->buffer = vte_terminal_get_text (vteterminal, NULL, NULL, NULL);
  
  //log_debug ("Buffer read: %d bytes\n", strlen (p_ct->buffer));
  //log_debug ("p_ct->buffer = '%s'\n", p_ct->buffer);

  char *bufferTmp, *tmp1, *tmp2;
  tmp1 = replace_str (p_ct->buffer, "\n", "");
  tmp2 = replace_str (tmp1, "\r", "");
  bufferTmp = replace_str (tmp2, " ", "");

  //log_debug ("bufferTmp = '%s'\n", bufferTmp);

  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5_CTX mdContext;

  MD5_Init (&mdContext);
  MD5_Update (&mdContext, bufferTmp, strlen(bufferTmp));
  MD5_Final (digest, &mdContext);
  
  char md5string[1024];
  for (i = 0; i < 16; ++i)
    sprintf(&md5string[i*2], "%02x", (unsigned int)digest[i]);

  free (tmp1);
  free (tmp2);
  free (bufferTmp);

  //log_debug ("MD5 = %s\n", md5string);

  /* remove extra new line at the end of buffer */
  
  //////if (p_ct->buffer[strlen (p_ct->buffer)-1] == '\n')
  //  p_ct->buffer[strlen (p_ct->buffer)-1] = 0;
  
  // Get cursor position and current line 
  // FIXME: sometimes gives the wrong row, eg. relogging by enter key
  
  vte_terminal_get_cursor_position (vteterminal, &_cx, &_cy);

  int cursorLine = _cy;
  
  //log_debug ("_cx=%ld _cy=%ld\n", _cx, _cy);
  
  //////if (_cy > list_count (p_ct->buffer, '\n')-1)
  //  _cy = list_count (p_ct->buffer, '\n')-1;
  
  /* if cursor didn't move don't do anything (e.g. resizing window) */
  /*
  if (_cx == p_ct->cx && _cy == p_ct->cy)
    return;
  */
  
  last_x = p_ct->cx;
  last_y = p_ct->cy;
  p_ct->cx = _cx;
  p_ct->cy = _cy;

  //log_debug ("cursorLine=%d p_ct->cx=%d p_ct->cy=%d\n", cursorLine, p_ct->cx, p_ct->cy);

  // Alloc line here so can contain the full buffer
  //char line[strlen (p_ct->buffer)+1];
  //list_get_nth (p_ct->buffer, p_ct->cy+1, '\n', line);

  char **lines = 
    splitString (p_ct->buffer, "\n", FALSE, NULL, FALSE, &nLines);

  //log_debug ("nLines = %d\n", nLines);

  //for (int i=0; i<nLines; i++)
  //  log_debug ("%2d: %s %s %d\n", i, lines[i], i == cursorLine ? "(*)" : "", i == cursorLine ? cursorLine : -1);

  if (cursorLine < nLines) {
    char line[strlen (lines[cursorLine])+1];
    //char *line = lines[nLines-1];
    strcpy(line, lines[cursorLine]);

    //log_debug ("line %d = '%s'\n", p_ct->cy+1, line); // Beware: crash with very long lines
    
    // normalize line for analysis
    
    lower (line);
    trim (line);
    
    //feed_child = 0;

    //log_debug ("%s : logged=%d, type=%d\n", p_ct->connection.name, p_ct->logged, p_current_connection_tab->type);

    if (p_ct->type == CONNECTION_REMOTE)
      {
        if (/*!p_ct->logged*/!tabGetFlag (p_ct, TAB_LOGGED) && (p_ct->cx != last_x || p_ct->cy != last_y) && !check_log_in_state (p_ct, line))
          {
            //connection_log_off ();
            return;
          }
      }
  }

  free (lines);

  //log_debug ("setting status...\n", line);

  //log_debug ("p_ct != p_current_connection_tab = %d\n", p_ct != p_current_connection_tab);
  //log_debug ("p_ct->window_resized = %d\n", p_ct->window_resized);
/* 
  if ((p_ct != p_current_connection_tab) && !p_ct->window_resized)
    {
      //log_debug ("check %s\n", p_ct->connection.name)
      
      if ((p_ct->type == CONNECTION_REMOTE && p_ct->status != TAB_STATUS_DISCONNECTED)
          || p_ct->type == CONNECTION_LOCAL) 
        {
          //log_debug ("%s has changed\n", p_ct->connection.name); 
          connection_tab_set_status (p_ct, TAB_STATUS_CHANGED); 
        }
    }
*/

  if (!p_ct->window_resized && tabIsConnected (p_ct)) {
    /*if ((p_ct->type == CONNECTION_REMOTE && p_ct->status != TAB_STATUS_DISCONNECTED)
        || p_ct->type == CONNECTION_LOCAL) 
      {*/
        if (strcmp (p_ct->md5Buffer, md5string)) {
          //log_debug ("%s has changed\n", p_ct->connection.name); 

          strcpy (p_ct->md5Buffer, md5string);
          //connection_tab_set_status (p_ct, TAB_STATUS_CHANGED);
          tabSetFlag (p_ct, TAB_CHANGED);
          refreshTabStatus (p_ct);
        } 
      //}
  }

  /* Reset the resize flag */
  p_ct->window_resized = 0;

  //log_debug ("end\n");
}

gboolean
commit_cb (VteTerminal *vteterminal, gchar *text, guint size, gpointer userdata)
{
#ifdef DEBUG
  printf ("commit_cb() : %s\n", text);
#endif

  return FALSE;
}

void
adjust_font_size (GtkWidget *widget, /*gpointer data,*/ gint delta)
{
  VteTerminal *terminal;
  PangoFontDescription *desired;
  gint newsize;
  gint columns, rows, owidth, oheight;

#ifdef DEBUG
  printf ("adjust_font_size()\n");
#endif

  /* Read the screen dimensions in cells. */
  terminal = VTE_TERMINAL (widget);
  columns = vte_terminal_get_column_count (terminal);
  rows = vte_terminal_get_row_count (terminal);

  /* Take into account padding and border overhead. */
  //gtk_window_get_size(GTK_WINDOW(data), &owidth, &oheight);
  gtk_window_get_size (GTK_WINDOW (main_window), &owidth, &oheight);
  owidth -= vte_terminal_get_char_width (terminal) * columns;
  oheight -= vte_terminal_get_char_height (terminal) * rows;

  /* Calculate the new font size. */
  desired = pango_font_description_copy (vte_terminal_get_font (terminal));
  newsize = pango_font_description_get_size (desired) / PANGO_SCALE;
  
  if (delta)
    newsize += delta;
  else
    newsize = globals.original_font_size;

  pango_font_description_set_size (desired, CLAMP (newsize, 4, 144) * PANGO_SCALE);

  /* Change the font, then resize the window so that we have the same
   * number of rows and columns. */
  vte_terminal_set_font (terminal, desired);

  //gtk_window_resize(GTK_WINDOW(data), columns * terminal->char_width + owidth, rows * terminal->char_height + oheight);

/*
  if (!prefs.maximize)
    gtk_window_resize (GTK_WINDOW (main_window), columns * vte_terminal_get_char_width (terminal) + owidth, rows * vte_terminal_get_char_height (terminal) + oheight);
*/

#ifdef DEBUG
  printf ("adjust_font_size() : new font : %s\n", pango_font_description_to_string (desired));
#endif

  pango_font_description_free (desired);
}

void
increase_font_size_cb (GtkWidget *widget, gpointer user_data)
{
#ifdef DEBUG
  printf ("increase_font_size_cb()\n");
#endif

  adjust_font_size (widget, /*user_data,*/ 1);
}

void
decrease_font_size_cb (GtkWidget *widget, gpointer user_data)
{
#ifdef DEBUG
  printf ("decrease_font_size_cb()\n");
#endif

  adjust_font_size (widget, /*user_data,*/ -1);
}

void
char_size_changed_cb (VteTerminal *terminal, guint width, guint height, gpointer user_data)
{
  GtkWindow *window;
  GdkGeometry geometry;
  int xpad, ypad;

  log_debug ("Desktop environment is %s\n", get_desktop_environment_name (get_desktop_environment ()));
  log_debug ("width=%d, height=%d\n", width, height);

  if (prefs.maximize)
    return;

#ifdef __APPLE__
  return;
#endif

  /* 
   * Here we must detect desktop environment.
   * In KDE and XFCE resizing is not needed (and doesn't works fine!).
   */
  
  log_debug ("Desktop environment id: %d\n", get_desktop_environment ());
  
  if (get_desktop_environment () == DE_KDE || get_desktop_environment () == DE_XFCE)
    return;

  //terminal = VTE_TERMINAL(widget);
  window = GTK_WINDOW (user_data);
  /* maybe no more useful */
#if (GTK_MAJOR_VERSION == 2)
  vte_terminal_get_padding (terminal, &xpad, &ypad);

  geometry.width_inc = vte_terminal_get_char_width (terminal);
  geometry.height_inc = vte_terminal_get_char_height (terminal);
  geometry.base_width = xpad;
  geometry.base_height = ypad;
  geometry.min_width = xpad + vte_terminal_get_char_width (terminal) * 2;
  geometry.min_height = ypad + vte_terminal_get_char_height (terminal) * 2;

  gtk_window_set_geometry_hints (window, GTK_WIDGET (terminal), &geometry, GDK_HINT_RESIZE_INC | GDK_HINT_BASE_SIZE | GDK_HINT_MIN_SIZE);
#endif
}

void
maximize_window_cb (VteTerminal *terminal, gpointer user_data)
{
  prefs.maximize = 1;
  
  /* Set resize flag for all the tabs. Will be checked in contents_changed_cb() */
  set_window_resized_all (1);
}

/*
void
resize_window_cb (VteTerminal *terminal, guint width, guint height, gpointer user_data)
{
#ifdef DEBUG
  printf ("resize_event_cb() : x=%d, y=%d\n", width, height);
#endif
}
*/
gboolean 
window_state_event_cb (GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
  if (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
    {
      prefs.maximize = 1;
    }
  else
    {
      prefs.maximize = 0;
    }
    
  /* Set resize flag for all the tabs. Will be checked in contents_changed_cb() */
  set_window_resized_all (1);
  
  return FALSE;
}

gboolean
key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  int keyReturn, keyEnter;
  gboolean rc = FALSE;

#if (GTK_MAJOR_VERSION == 2)
  keyReturn = GDK_Return;
  keyEnter = GDK_KP_Enter;
#else
  keyReturn = GDK_KEY_Return;
  keyEnter = GDK_KEY_KP_Enter;
#endif
    
  //log_debug("keyval=%d\n", event->keyval);
    
#ifdef __APPLE__
  /* On Mac OS X pressing Alt_gr+key has a strange behaviour, so detect keys and write the symbol */
  if (event->state & GDK_MOD1_MASK)
    {
      log_debug("ALT pressed with %d\n", event->keyval);
      
      if (p_current_connection_tab)
        {
          vte_terminal_feed_child (p_current_connection_tab->vte, event->string, -1);
          rc = TRUE; /* don't propagate signal handling */
        }
    }
#endif

  if (event->keyval == keyReturn || event->keyval == keyEnter)
    {
      log_debug ("Enter/Return\n");

      if (!p_current_connection_tab)
        return FALSE;
      
      log_debug ("p_current_connection_tab ok\n");

      if (/*p_current_connection_tab->connected == 0*/ tabGetConnectionStatus(p_current_connection_tab) == TAB_CONN_STATUS_DISCONNECTED && 
          p_current_connection_tab->type == CONNECTION_REMOTE && 
          p_current_connection_tab->last_connection.name[0] != 0)
        {
          /*if (VTE_IS_TERMINAL (p_current_connection_tab->vte))
            vte_terminal_reset (VTE_TERMINAL (p_current_connection_tab->vte), TRUE, FALSE);*/
/*
          p_current_connection_tab->changes_count = 0;
          p_current_connection_tab->auth_attempt = 0;
          p_current_connection_tab->logged = 0;
*/
          tabInitConnection (p_current_connection_tab);
          p_current_connection_tab->enter_key_relogging = 1;
          
          log_on (p_current_connection_tab);
          //connection_tab_set_status (p_current_connection_tab, TAB_STATUS_NORMAL); 
          refreshTabStatus (p_current_connection_tab); 
          update_screen_info ();
          
          if (lt_ssh_is_connected (&p_current_connection_tab->ssh_info))
            {
              sftp_refresh_directory_list (&p_current_connection_tab->ssh_info);
              refresh_sftp_panel (&p_current_connection_tab->ssh_info);
            }
        }
    }

  log_debug ("end\n");

  return (rc);
}

void
select_current_profile_menu_item (struct ConnectionTab *p_ct)
{
  struct Profile *p = profile_get_by_id (&g_profile_list, p_ct->profile_id);
  
  if (p)
    {
      char tmp_s[256];
      sprintf (tmp_s, "/MainMenu/TerminalMenu/ProfileMenu/profile-%d", p->id);
      GtkAction *toggleAction = gtk_ui_manager_get_action (ui_manager, tmp_s);
      
      if (toggleAction)
        gtk_radio_action_set_current_value (GTK_RADIO_ACTION(toggleAction), p_ct->profile_id);
    }
}

void
update_by_tab(struct ConnectionTab *pTab)
{
  update_screen_info ();
  
  //connection_tab_set_status (pTab, TAB_STATUS_NORMAL);
  refreshTabStatus (pTab);

  select_current_profile_menu_item (pTab);

  log_debug ("Refresh directory\n");
  refresh_sftp_panel (&p_current_connection_tab->ssh_info);

  log_debug ("Refresh history\n");
  refresh_panel_history ();
  
  log_debug ("Completed\n");
}

void
notebook_switch_page_cb (GtkNotebook *notebook, GtkWidget *page, gint page_num, gpointer user_data)
{
  GtkWidget *child;

  if (!switch_tab_enabled)
    return;
    
  switch_tab_enabled = FALSE;

  log_write ("Switched to page id: %d\n", page_num);

  //child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), page_num);
  child = gtk_notebook_get_nth_page (notebook, page_num);

  p_current_connection_tab = get_connection_tab_from_child (child); /* try with g_list_find () */

  log_write ("Page name: %s\n", p_current_connection_tab->connection.name);
/*
  update_screen_info ();
  
  connection_tab_set_status (p_current_connection_tab, TAB_STATUS_NORMAL);

  select_current_profile_menu_item (p_current_connection_tab);

  log_debug ("Refresh directory for page %d\n", page_num);
  refresh_sftp_panel (&p_current_connection_tab->ssh_info);
  refresh_panel_history ();
  
  log_debug ("Completed for page %d\n", page_num);
*/
  update_by_tab(p_current_connection_tab);
  switch_tab_enabled = TRUE;
}

void
terminal_focus_cb (GtkWidget *widget,
                   gpointer user_data)
{
  struct ConnectionTab *newTab = (struct ConnectionTab *)user_data;

  if (p_current_connection_tab == newTab)
    return;

  log_debug("%s\n", vte_terminal_get_window_title (VTE_TERMINAL(newTab->vte)));

  p_current_connection_tab = newTab;
  update_by_tab(p_current_connection_tab);
}

void
notebook_page_reordered_cb (GtkNotebook *notebook, GtkWidget *child, guint page_num, gpointer user_data)
{
  log_write ("Page reordered: %d\n", page_num);
}
/*
gboolean
notebook_reorder_tab_cb (GtkNotebook *notebook, GtkDirectionType arg1, gboolean arg2, gpointer user_data)
{
  log_debug ("\n");
  
  return (FALSE);
}
*/
void
apply_preferences ()
{
  int i;
  char word_chars[1024];
  GdkColor terminal_fore_color;
  GdkColor terminal_back_color;
  GList *item;
  GtkWidget *vte;

  struct ConnectionTab *p_ct = NULL;

  log_debug ("\n");

  for (i=0; i<g_list_length (connection_tab_list); i++)
    {
      item = g_list_nth (connection_tab_list, i);
      p_ct = (struct ConnectionTab *) item->data;
      vte = p_ct->vte;

      //gdk_color_parse (prefs.fg_color, &terminal_fore_color);
      //gdk_color_parse (prefs.bg_color, &terminal_back_color);
      //vte_terminal_set_colors (VTE_TERMINAL (vte), &terminal_fore_color, &terminal_back_color, NULL, 0);
      vte_terminal_set_mouse_autohide (VTE_TERMINAL (vte), prefs.mouse_autohide);

      vte_terminal_set_scrollback_lines (VTE_TERMINAL (vte), prefs.scrollback_lines);
      vte_terminal_set_scroll_on_keystroke (VTE_TERMINAL (vte), prefs.scroll_on_keystroke);
      vte_terminal_set_scroll_on_output (VTE_TERMINAL (vte), prefs.scroll_on_output);

      if (prefs.character_encoding[0] != 0)
        //vte_terminal_set_encoding (VTE_TERMINAL (vte), prefs.character_encoding);
        terminal_set_encoding (p_ct, prefs.character_encoding);
      else
        strcpy (prefs.character_encoding, vte_terminal_get_encoding (VTE_TERMINAL (vte)));

      // Set words for vte < 0.40
#if (VTE_CHECK_VERSION(0,38,3) == 0)
      strcpy (word_chars, DEFAULT_WORD_CHARS);
      strcat (word_chars, prefs.extra_word_chars);
      vte_terminal_set_word_chars (VTE_TERMINAL (vte), word_chars);
#endif
      ////////vte_terminal_set_size (VTE_TERMINAL (vte), prefs.columns, prefs.rows);

      //gtk_window_resize (GTK_WINDOW (main_window), prefs.w, prefs.h);
      //gtk_window_set_position (GTK_WINDOW (main_window), GTK_WIN_POS_CENTER);
      //gtk_window_move (GTK_WINDOW (main_window), prefs.x, prefs.y);
      gtk_window_set_resizable (GTK_WINDOW (main_window), TRUE);
    }

  log_debug ("set font for qlw : %s\n", prefs.font_quick_launch_window);

  PangoFontDescription *font_desc;

  font_desc = pango_font_description_from_string (prefs.font_quick_launch_window);
  gtk_widget_modify_font (g_quick_launch_window.tree_view, font_desc);
  pango_font_description_free (font_desc);
  //gtk_widget_queue_draw (g_quick_launch_window.tree_view);
    
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), prefs.tabs_position);
}
  
void
apply_profile_terminal (GtkWidget *terminal, struct Profile *p_profile)
{
  GObject *object;
  
/*
  \* this is needed for opacity to have effect *\
  GdkRGBA white={255, 255, 255, 1};
  vte_terminal_set_color_background_rgba (VTE_TERMINAL (terminal), &white);

  //vte_terminal_set_background_saturation (VTE_TERMINAL (terminal), 1.0 - p_profile->alpha);
  vte_terminal_set_opacity (VTE_TERMINAL (terminal), (int) 65535 * p_profile->alpha);
*/

#if (GTK_MAJOR_VERSION == 2)
  GdkColor terminal_fore_color;
  GdkColor terminal_back_color;
  
  gdk_color_parse (p_profile->fg_color, &terminal_fore_color);
  gdk_color_parse (p_profile->bg_color, &terminal_back_color);
  vte_terminal_set_colors (VTE_TERMINAL (terminal), &terminal_fore_color, &terminal_back_color, NULL, 0);
#else
  GdkRGBA fg, bg;
  gdk_rgba_parse (&fg, p_profile->fg_color);
  gdk_rgba_parse (&bg, p_profile->bg_color);
  //GdkRGBA fg={0, 255, 0, 1}, bg={0, 0, 0, 1};
  //bg.alpha = p_profile->alpha;
#if (VTE_CHECK_VERSION(0,38,3) == 1)
  vte_terminal_set_color_foreground (VTE_TERMINAL (terminal), &fg);
  vte_terminal_set_color_background (VTE_TERMINAL (terminal), &bg);
#else
  vte_terminal_set_colors_rgba (VTE_TERMINAL (terminal), &fg, &bg, NULL, 0);
#endif

#endif



  if (p_profile->font_use_system)
    {
      //vte_terminal_set_font_from_string (VTE_TERMINAL (terminal), globals.system_font);
      terminal_set_font_from_string (VTE_TERMINAL (terminal), globals.system_font);
    }
  else if (p_profile->font[0] != 0)
    {
      //vte_terminal_set_font_from_string (VTE_TERMINAL (terminal), p_profile->font);
      terminal_set_font_from_string (VTE_TERMINAL (terminal), p_profile->font);
    }

  if (p_profile->cursor_shape >= 0 && p_profile->cursor_shape <= 2)
    vte_terminal_set_cursor_shape (VTE_TERMINAL (terminal), p_profile->cursor_shape);
  else
    p_profile->cursor_shape = vte_terminal_get_cursor_shape (VTE_TERMINAL (terminal));

  vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (terminal), p_profile->cursor_blinking ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF);

  vte_terminal_set_audible_bell (VTE_TERMINAL (terminal), p_profile->bell_audible);

#if (VTE_CHECK_VERSION(0,38,3) == 0)
  vte_terminal_set_visible_bell (VTE_TERMINAL (terminal), p_profile->bell_visible);
#endif
  
//  while (gtk_events_pending ())
//    gtk_main_iteration ();
}
  
void
apply_profile (struct ConnectionTab *p_ct, int profile_id)
{
  struct Profile *p_profile;

  if (profile_id > 0)
    p_profile = profile_get_by_id (&g_profile_list, profile_id);
  else
    p_profile = profile_get_default (&g_profile_list);
  
  if (p_profile == NULL)
    {
      log_write ("[%s] ***default profile not found\n", __func__);
      return;
    }

  apply_profile_terminal (p_ct->vte, p_profile);
  
  p_ct->profile_id = p_profile->id;
}

void
update_all_profiles ()
{
  int i;
  GList *item;

  struct ConnectionTab *p_ct = NULL;

  for (i=0; i<g_list_length (connection_tab_list); i++)
    {
      item = g_list_nth (connection_tab_list, i);
      p_ct = (struct ConnectionTab *) item->data;

      apply_profile (p_ct, p_ct->profile_id);
    }
}

int
open_connection (char *connection)
{
  int rc;
  char *pc;
  char connection_string[256];
  struct Connection c;
  
  if (!memcmp (connection, "conn:", 5))
    {
      pc = (char *) &connection[5];
      strcpy (connection_string, pc);
      
      log_debug ("connection_string = %s\n", connection_string);

      rc = connection_fill_from_string (&c, connection_string);
      
      if (rc == 0)
        connection_log_on_param (&c);
      else
        msgbox_error ("can't open connection %s\nError id: %d", connection_string, rc);
    }
  else
    load_session_file (connection);
}

#ifdef MAC_INTEGRATION
static gboolean osx_open_file_cb(GtkosxApplication *app, gchar *path, gpointer user_data) {
  
  //GFile *file;
  //Tbfwin *bfwin = BFWIN(g_list_last(main_v->bfwinlist)->data);
  //g_print("osx_open_file_cb, open %s\n",path);
  //doc_new_from_input(bfwin,path,FALSE,FALSE,-1);
  
  
  
  return TRUE;
}
#endif

void
check_resize_cb (GtkPaned *widget)
{
  //log_debug ("\n");
  
  /* Set resize flag for all the tabs. Will be checked in contents_changed_cb() */
  set_window_resized_all (1);
}

gboolean
configure_event_cb (GtkWindow *window, GdkEvent *event, gpointer data)
{
  //log_debug ("\n");
  
  /* Set resize flag for all the tabs. Will be checked in contents_changed_cb() */
  set_window_resized_all (1);
  
  return (FALSE);
}

/*
  Function: start_gtk
  Creates the main user interface
*/
void
start_gtk (int argc, char **argv)
{
  int i;
  char s_tmp[256];
  GtkWidget *vbox;          /* main vbox */
  GtkWidget *hbox_terminal; /* vte + scrollbar */
  GtkWidget *scrollbar;
  PangoFontDescription *font_desc;
  int font_size;
  struct Iteration_Function_Request ifr_function;

  signal (SIGCHLD, child_exit); /* a child process ends */
  signal (SIGSEGV, segv_handler); /* Segmentation fault */

  /* Initialize i18n support */

  //gtk_set_locale ();

  connection_tab_list = NULL;
  p_current_connection_tab = NULL;
  
  g_recent_connections_list = NULL;
  g_recent_sessions_list = NULL;

  gtk_init (&argc, &argv);

  log_write ("Creating main window...\n");

  main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_icon_from_file (GTK_WINDOW (main_window), g_strconcat (globals.img_dir, "/main_icon.png", NULL), NULL);

  g_screen = gtk_widget_get_screen (GTK_WIDGET (main_window));
  
  /* check rgba capabilities */
/*
  \*This should be the preferred method but causes some strange problems moving notebook tabs *\
  GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (main_window));
  GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
  
  if (visual != NULL && gdk_screen_is_composited (screen))
    {
      gtk_widget_set_visual (GTK_WIDGET (main_window), visual);
      log_write ("RGBA capabilities OK\n");
    } 
  else
    {
      log_write ("can't get visual, no rgba capabilities!\n");
    }
*/
  set_title (0);

  if (prefs.maximize)
    gtk_window_maximize (GTK_WINDOW (main_window));

  /* Create new stock objects */
  log_write ("Creating stock objects...\n");
  create_stock_objects ();

  /* Main vbox */
#if (GTK_MAJOR_VERSION == 2)
  vbox = gtk_vbox_new (FALSE, 0);
#else
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif

  gtk_container_add (GTK_CONTAINER (main_window), vbox);
  gtk_widget_show_all (vbox);
       
  /* Menu */
  log_write ("Creating menu...\n");
  get_main_menu ();
  gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, TRUE, 0);
  gtk_widget_show (menubar);
  
  log_write ("Creating accelerators...\n");
  create_accelerators ();
  
  /* Load recent connections and update menu */
  log_write ("Getting recent manager...\n");
  g_recent_manager = gtk_recent_manager_get_default ();
  
  log_write ("Loading recent connections...\n");
  load_recent_connections ();
  
  recents_handler_id = g_signal_connect (g_recent_manager, "changed", G_CALLBACK (recent_manager_changed), NULL);
  
  log_write ("Updating recent sessions menu...\n");
  update_recent_sessions_menu ();

  refresh_profile_menu ();

  /* Toolbar */
  log_write ("Creating toolbar...\n");
  add_toolbar (vbox);
  
  /* Paned window */
#if (GTK_MAJOR_VERSION == 2)
  hpaned = gtk_hpaned_new ();
#else
  hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
#endif
  //hbox_workspace = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  
  g_signal_connect (G_OBJECT(hpaned), "notify::position", G_CALLBACK (check_resize_cb), NULL);

  /* list store for connetions */
  connection_init_stuff ();
  //create_connections_list_store ();

  /* Create the sidebar */
  notebook_sidebar = gtk_notebook_new ();
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook_sidebar), TRUE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook_sidebar), FALSE);

  /* quick launch window */
  log_write ("Creating quick launch window...\n");
  create_quick_launch_window (&g_quick_launch_window);

  /* sftp panel */
  log_write ("Creating sftp panel...\n");
  GtkWidget *vbox_sftp = create_sftp_panel ();

  gtk_notebook_append_page (GTK_NOTEBOOK(notebook_sidebar), g_quick_launch_window.vbox, 
                            gtk_widget_new (GTK_TYPE_LABEL, "label", "Connections", "xalign", 0.0, NULL));

  gtk_notebook_append_page (GTK_NOTEBOOK(notebook_sidebar), vbox_sftp, 
                            gtk_widget_new (GTK_TYPE_LABEL, "label", "SFTP", "xalign", 0.0, NULL));
  

  //gtk_box_pack_start (GTK_BOX (hbox_workspace), g_quick_launch_window.vbox, FALSE, FALSE, 0);
/*
  align_sidebar = gtk_alignment_new (0, 0, 0, 1);
  gtk_widget_show (align_sidebar);
  gtk_container_add (GTK_CONTAINER (align_sidebar), notebook_sidebar);
*/
  log_write ("Adding sftp panel...\n");
  gtk_paned_add1 (GTK_PANED (hpaned), notebook_sidebar);
  //gtk_box_pack_start (GTK_BOX (hbox_workspace), align_sidebar, TRUE, TRUE, 0);
  //gtk_box_set_homogeneous (GTK_BOX (hbox_workspace), FALSE);
    
  //show_sidebar ((gboolean) prefs.show_sidebar);  

  GtkWidget *toggle = gtk_ui_manager_get_widget (ui_manager, N_("/MainMenu/ViewMenu/Sidebar"));

  if (prefs.show_sidebar)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (toggle), TRUE); 
  
  /* Notebook */

  notebook = gtk_notebook_new ();
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gtk_notebook_popup_enable (GTK_NOTEBOOK (notebook));
  gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), TRUE);

#if (GTK_MAJOR_VERSION == 2)
  gtk_notebook_set_tab_border (GTK_NOTEBOOK (notebook), 0);
  g_signal_connect (G_OBJECT (notebook), "switch-page", G_CALLBACK (notebook_switch_page_cb), 0);
  g_signal_connect (G_OBJECT (notebook), "page-reordered", G_CALLBACK (notebook_page_reordered_cb), 0);
#else
  g_signal_connect (notebook, "switch-page", G_CALLBACK (notebook_switch_page_cb), 0);
  g_signal_connect (notebook, "page-reordered", G_CALLBACK (notebook_page_reordered_cb), 0);
  //g_signal_connect (notebook, "reorder-tab", G_CALLBACK (notebook_reorder_tab_cb), 0);
#endif


  gtk_widget_show (notebook);
  gtk_paned_add2 (GTK_PANED (hpaned), notebook);
  //gtk_box_pack_start (GTK_BOX (hbox_workspace), notebook, TRUE, TRUE, 0);


  gtk_box_pack_start (GTK_BOX (vbox), hpaned, TRUE, TRUE, 0);
  //gtk_box_pack_start (GTK_BOX (vbox), hbox_workspace, TRUE, TRUE, 0);
  gtk_widget_show (hpaned);
  //gtk_widget_show (hbox_workspace);

  /* statusbar */
  log_write ("Creating statusbar...\n");
  create_statusbar ();
  gtk_box_pack_end (GTK_BOX (vbox), statusbar, FALSE, TRUE, 0);

#if (GTK_MAJOR_VERSION == 2)
  g_signal_connect (GTK_OBJECT (main_window), "delete_event", GTK_SIGNAL_FUNC (delete_event_cb), NULL);
  g_signal_connect (GTK_OBJECT (main_window), "size-allocate", G_CALLBACK (size_allocate_cb), NULL);
  g_signal_connect (GTK_OBJECT (main_window), "key-press-event", G_CALLBACK (key_press_event_cb), NULL);
#else
  g_signal_connect (main_window, "delete_event", G_CALLBACK (delete_event_cb), NULL);
  g_signal_connect (main_window, "size-allocate", G_CALLBACK (size_allocate_cb), NULL);
  //g_signal_connect (main_window, "window-state-event", G_CALLBACK (window_state_event_cb), NULL);
  g_signal_connect (main_window, "key-press-event", G_CALLBACK (key_press_event_cb), NULL);
#endif

  //g_signal_connect (G_OBJECT(main_window), "window-state-event", G_CALLBACK(window_state_event_cb), NULL);
  g_signal_connect (G_OBJECT (main_window), "window-state-event", G_CALLBACK (window_state_event_cb), NULL);
  g_signal_connect (G_OBJECT (main_window), "configure-event", G_CALLBACK(configure_event_cb), NULL);

  gtk_window_set_default_size (GTK_WINDOW (main_window), prefs.w, prefs.h); /* keep this before gtk_widget_show() */

  gtk_widget_show (main_window);
  //gtk_window_present (GTK_WINDOW (main_window));

  if (globals.upgraded)
    {
      clean_recents ();
      profile_modify_string (PROFILE_SAVE, globals.conf_file, "general", "package_version", VERSION);
      msgbox_info ("Congratulations, you just upgraded to version %s", VERSION);

      if (!cmpver (VERSION, "1.4.1"))
        {
          if (prefs.sftp_buffer < 128*1024)
            prefs.sftp_buffer = 128*1024;
        }
    }

  /* init list of connections for the first time, needed by terminal popup menu */

  log_write("Loading connections...\n");
  load_connections ();

  log_debug("Refreshing completion list...\n");
  refresh_search_completion ();

  update_statusbar ();

  /* Ensure that buttons images will be shown */

  GtkSettings *default_settings = gtk_settings_get_default ();
  g_object_set (default_settings, "gtk-button-images", TRUE, NULL); 

  GError *error = NULL;
  //url_regexpr = g_regex_new ("(?:www|ftp)" HOSTCHARS_CLASS "*\\." HOST PORT URLPATH, G_REGEX_OPTIMIZE, 0, &error);
  ip_regexpr = g_regex_new ("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}", G_REGEX_OPTIMIZE, 0, &error);

  if (error)
    {
      log_write ("%s", error->message);
      g_error_free (error);
    }
    
  sftp_spinner_stop ();
  sftp_end ();

        
#ifdef MAC_INTEGRATION
  log_debug("MAC-INTEGRATION\n");
  
  gtk_widget_hide (menubar);
  
  gtk_accel_map_foreach_unfiltered (NULL, osx_accel_map_foreach_controltometa_lcb);
  gtk_accel_map_foreach_unfiltered (NULL, osx_accel_map_foreach_mod1tocontrol_lcb);

  GtkosxApplication *theApp = g_object_new (GTKOSX_TYPE_APPLICATION, NULL);

  g_signal_connect (theApp, "NSApplicationOpenFile", osx_open_file_cb, NULL);
  g_signal_connect (theApp, "NSApplicationBlockTermination", application_quit, NULL);
  
  gtkosx_application_ready (theApp);
#endif
}
