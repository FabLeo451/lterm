
/*
 * GUI.H
 */

#ifndef __GUI_H
#define __GUI_H

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <unistd.h>
#include "connection_list.h"
#include "ssh.h"

#define QUERY_USER 1
#define QUERY_PASSWORD 2
#define QUERY_RENAME 3
#define QUERY_FILE_NEW 4
#define QUERY_FOLDER_NEW 5

// Tab status for alerts
/*
#define TAB_STATUS_NORMAL 0
#define TAB_STATUS_CHANGED 2
#define TAB_STATUS_DISCONNECTED 3
*/

// Tab connection status
/*
#define TAB_CONN_STATUS_DISCONNECTED 0
#define TAB_CONN_STATUS_CONNECTING 1
#define TAB_CONN_STATUS_CONNECTED 2
#define TAB_CONN_STATUS_LOGGED 3
*/
enum { TAB_CONN_STATUS_DISCONNECTED = 0, TAB_CONN_STATUS_CONNECTING, TAB_CONN_STATUS_CONNECTED };

#define AUTH_STATE_NOT_LOGGED 0
#define AUTH_STATE_GOT_USER 1
#define AUTH_STATE_GOT_PASSWORD 2
#define AUTH_STATE_LOGGED 3

// Flags
#define TAB_CHANGED 1
#define TAB_LOGGED 2

//#define GET_UI_ELEMENT(TYPE, ELEMENT) TYPE *ELEMENT = (TYPE *) gtk_builder_get_object (builder, #ELEMENT);

typedef struct ConnectionTab
  {
    struct Connection connection;
    struct Connection last_connection;
    struct SSH_Info ssh_info;

    //int connected; // DEPRECATED: use connectionStatus
    //int logged; // DEPRECATED: use flags
    int connectionStatus;
    int enter_key_relogging;
    unsigned int auth_state;
    int changes_count;
    int auth_attempt;
    int type;
    //int status; // DEPRECATED: use flags
    unsigned int flags; // logged, changed
    char *buffer;
    char md5Buffer[1024];
    int cx, cy; /* cursor position */
    int window_resized;
    int profile_id;

    GtkWidget *hbox_terminal; /* vte + scrollbar */
    GtkWidget *vte; 
    GtkWidget *scrollbar;

    GtkWidget *label; // Text
    GtkWidget *notebook; // Notebook containing the terminal

    pid_t pid;
  } SConnectionTab;

struct QuickLaunchWindow
  {
    GtkTreeModel *tree_model;
    GtkWidget *tree_view;
    GtkWidget *scrolled_window;
    GtkWidget *search_by_combo;
    GtkWidget *copy_button;
    GtkWidget *vbox;
    gulong row_inserted_handler;
    gulong row_deleted_handler;
  };

/* stock objects */

#define MY_STOCK_MAIN_ICON "main_icon"
#define MY_STOCK_TERMINAL "terminal_32"
#define MY_STOCK_DUPLICATE "duplicate"
#define MY_STOCK_PLUS "plus"
#define MY_STOCK_LESS "less"
#define MY_STOCK_PENCIL "pencil"
#define MY_STOCK_COPY "copy"
#define MY_STOCK_FOLDER "folder"
#define MY_STOCK_UPLOAD "upload"
#define MY_STOCK_DOWNLOAD "download"
#define MY_STOCK_SIDEBAR "sidebar"
#define MY_STOCK_SPLIT_H "splitH"
#define MY_STOCK_SPLIT_V "splitV"
#define MY_STOCK_REGROUP "regroup"
#define MY_STOCK_CLUSTER "cluster"
#define MY_STOCK_PREFERENCES "preferences"
#define MY_STOCK_TRANSFERS "transfers"
#define MY_STOCK_FOLDER_UP "folder_up"
#define MY_STOCK_FOLDER_NEW "folder_new"
#define MY_STOCK_HOME "home"
#define MY_STOCK_FILE_NEW "file_new"


/* requested operations within gtk iteration */

#define ITERATION_MAX 20
#define ITERATION_REBUILD_TREE_STORE 1
#define ITERATION_REFRESH_TREE_VIEW 2
#define ITERATION_REFRESH_QUICK_LAUCH_TREE_VIEW 3
#define ITERATION_REFRESH_SFTP_PANEL 4
#define ITERATION_CLOSE_TAB 5

struct Iteration_Function_Request
  {
    int id;
    void *user_data;
  };

void ifr_add (int function_id, void *user_data);

struct Match
  {
    int tag;
    char *matched_string;
  };

void msgbox_error (const char *fmt, ...);
void msgbox_info (const char *fmt, ...);
gint msgbox_yes_no (const char *fmt, ...);
int query_value (char *title, char *labeltext, char *default_value, char *buffer, int type);
int expand_args (struct Connection *p_conn, char *args, char *prefix, char *dest);
int show_login_mask (struct ConnectionTab *p_conn_tab, struct SSH_Auth_Data *p_auth);

void tabInitConnection(SConnectionTab *pConn);
char *tabGetConnectionStatusDesc(int status);
void tabSetConnectionStatus(SConnectionTab *pConn, int status);
int tabGetConnectionStatus(SConnectionTab *pConn);
int tabIsConnected(SConnectionTab *pConn);
void tabSetFlag (SConnectionTab *pConn, unsigned int bitmask);
void tabResetFlag (SConnectionTab *pConn, unsigned int bitmask);
unsigned int tabGetFlag (SConnectionTab *pConn, unsigned int bitmask);

void resize_window_cb (VteTerminal *terminal, guint width, guint height, gpointer user_data);
void maximize_window_cb (VteTerminal *terminal, gpointer user_data);
void char_size_changed_cb (VteTerminal *terminal, guint width, guint height, gpointer user_data);
void increase_font_size_cb (GtkWidget *widget, gpointer user_data);
void decrease_font_size_cb (GtkWidget *widget, gpointer user_data);
void adjust_font_size (GtkWidget *widget, /*gpointer data,*/ gint delta);
//gboolean commit_cb (VteTerminal *vteterminal, gchar *text, guint size, gpointer userdata);
void status_line_changed_cb (VteTerminal *vteterminal, gpointer user_data);
void eof_cb (VteTerminal *vteterminal, gpointer user_data);

#if (VTE_CHECK_VERSION(0,38,2) == 1)
void child_exited_cb (VteTerminal *vteterminal, gint status, gpointer user_data);
#else
void child_exited_cb (VteTerminal *vteterminal, gpointer user_data);
#endif

void size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
gint delete_event_cb (GtkWidget *window, GdkEventAny *e, gpointer data);
void terminal_popup_menu (GdkEventButton *event);
gboolean button_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
void window_title_changed_cb (VteTerminal *vteterminal, gpointer user_data);
void selection_changed_cb (VteTerminal *vteterminal, gpointer user_data);
void contents_changed_cb (VteTerminal *vteterminal, gpointer user_data);
void
terminal_focus_cb (GtkWidget       *widget,
                gpointer         user_data);

void connection_log_on_param (struct Connection *p_conn);
void connection_log_on ();
void connection_log_off ();
void connection_duplicate ();
void connection_edit_protocols ();
void connection_new_terminal_dir (char *directory);
void connection_new_terminal ();
void connection_close_tab ();
void application_quit ();

void edit_copy ();
void edit_paste ();
void edit_copy_and_paste ();

void edit_find ();

void edit_current_profile ();
void edit_select_all ();
void show_preferences ();

//void view_transfer_window ();
void view_toolbar ();
void view_statusbar ();
void view_sidebar ();
void view_fullscreen ();
void view_go_back ();
void view_go_forward ();
void zoom_in ();
void zoom_out ();
void zoom_100 ();

void hyperlink_connect_host ();
void hyperlink_edit_host ();
void hyperlink_add_host ();

void terminal_reset ();
void terminal_detach_right();
void terminal_detach_down();
void terminal_attach_to_main (struct ConnectionTab *connectionTab);
void terminal_attach_current_to_main ();
void terminal_regroup_all ();
void terminal_cluster ();

void apply_preferences ();
void apply_profile ();

void session_load ();
void session_save ();

void sftp_upload_files ();
void sftp_download_files ();

void help_home_page ();
void Info ();
void Debug ();

void statusbar_push (const char *fmt, ...);
void statusbar_pop ();
void statusbar_msg (const char *fmt, ...);
void update_statusbar ();
gboolean update_statusbar_upload_download ();
void update_screen_info ();
int save_session_file (char *filename);

void select_current_profile_menu_item (struct ConnectionTab *p_ct);
void refresh_profile_menu ();
struct ConnectionTab *get_connection_tab_from_child (GtkWidget *child);
//void connection_tab_set_status (struct ConnectionTab *connection_tab, int status);
void refreshTabStatus (SConnectionTab *pTab);
struct ConnectionTab *connection_tab_new ();
struct ConnectionTab *get_current_connection_tab ();
char *get_remote_directory ();
int connection_tab_getcwd (struct ConnectionTab *p_ct, char *directory);

void apply_profile (struct ConnectionTab *p_ct, int profile_id);
void update_all_profiles ();

void add_recent_session (char *filename);
void add_recent_connection (struct Connection *p_conn);

void sftp_upload_files ();
void sftp_download_files ();

void sb_msg_push (char *);

#endif
