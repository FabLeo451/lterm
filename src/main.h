
/*
  Header: main.h
  Global structures
*/

#ifndef _MAIN_H
#define _MAIN_H

#include "ssh.h"
#include "gtk/gtk.h"

/* #define DEBUG */

#ifdef DEBUG
void setCurrentFunction (char *);
char * getCurrentFunction ();
#define log_debug(format, ...) { log_write ("[%s] "format, __func__, ##__VA_ARGS__); }
#define ENTER_FUNC() { setCurrentFunction (__func__); }
#else
//#define _DBG_printf(format, ...)
//#define _DBG_fn_start
//#define _DBG_fn_end
#define log_debug(format, ...)
#define ENTER_FUNC()
#endif

//#define UI_DIR "/home/fabio/src/lterm-0.5.x/data"
//#define UI_DIR "/dati/Source/lterm-0.5.x/data"

#include "config.h"

#ifdef ENABLE_NLS

#include <libintl.h>
#define  _(x)  gettext(x)
#define N_(x)  x

#else

#define _(x)   x
#define N_(x)  x

#endif

#define KEY "You may say I'm a dreamer"

#define HOME_PAGE "lterm.sourceforge.net"
#define HTTP_HOME_PAGE "http://lterm.sourceforge.net"

#define LOG_MSG printf
/* g_print */

/* Version of configuration files */
#define CFG_VERSION 3
#define CFG_XML_VERSION 6

/* types of connection */

#define CONNECTION_NONE 0
#define CONNECTION_LOCAL 1
#define CONNECTION_REMOTE 2

#define MAX_RECENT 30

  
#ifdef __APPLE__
# define DEFAULT_FIXED_FONT "Monaco 11"
#else
# define DEFAULT_FIXED_FONT "Monospace 9"
#endif

/*
  Struct: _globals
  Structure containing global data such as home and work directory, etc.
*/
struct _globals
{
  int running;
  char home_dir[256];
  char app_dir[256];
  char img_dir[256];
  char data_dir[256];
  char serverlist[256];             /* Server list file (deprecated) */
  char connections_xml[256];    /* Server list file (xml format)*/
  char conf_file[256];
  char session_file[256];
  char log_file[256];
  char profiles_file[256];
  char protocols_file[256];
  int connected;
  int original_font_size;
  char system_font[256];
  int upgraded; /* 1 if just upgraded package */
  char start_connections[256]; /* list of files and connections to be opened at startup */
  char recent_connections_file[256];
  char recent_sessions_file[256];
  struct SSH_List ssh_list;
  char find_expr[256];
  int inotifyFd; // For Inotify and FSEvents
  fd_set rfds;
};

typedef struct _globals Globals;

/*
  Struct: _prefs
  Structure containing the user preferences
 */
struct _prefs
{
  int startup_show_connections;
  int startup_local_shell;
  int check_connections;        /* detect warnings */
  char warnings_color[128];
  char warnings_error_color[128];
  char label_local[128];
  char local_start_directory[1024];
  int maximize;
  int x, y, w, h;               /* Window position and dimension */
  int toolbar;                  /* server toolbar on/off */
  int statusbar;                /* statusbar on/off */
  int fullscreen;               /* fullscreen on/off */
  int rows;
  int columns;
  char extra_word_chars[256];
  char character_encoding[64];
  int scrollback_lines;
  int scroll_on_keystroke;
  int scroll_on_output;
  int mouse_autohide;
  int mouse_copy_on_select;
  int mouse_paste_on_right_button;
  //char emulation_list[1024];
  int tabs_position;
  int tab_alerts;
  char tab_status_changed_color [32];
  //char tab_status_connecting_color [32];
  char tab_status_disconnected_color [32];
  char tab_status_disconnected_alert_color [32];
  int show_sidebar;
  char font_quick_launch_window [128];
  char font_fixed [128];
  int search_by;
  int save_session;
  int max_recent_connections;
  int max_recent_sessions;
  int checkpoint_interval;

  int sftp_buffer;
  int flag_ask_download;
  char download_dir[512];
  char text_editor[128];
  char sftp_open_file_uri[1024];
  int ssh_keepalive;
  int ssh_timeout;
  char sftp_panel_background[64];
  //char last_upload_dir[256];
  //char last_download_dir[256];
  
  int hyperlink_tooltip_enabled;
  int hyperlink_click_enabled;

  char tempDir[1024];
};

typedef struct _prefs Prefs;

void log_reset ();
void log_write (const char *fmt,...);
gboolean doGTKMainIteration ();
void notifyMessage (char *message);
void timerStart (int seconds);
void timerStop ();
int timedOut ();
void threadRequestAlarm ();
void threadResetAlarm ();
void addIdleGTKMainIteration ();
void update_main_window_title ();
void update_statusbar ();
int cmpver (char *v1, char *v2);

//void recent_add (char *);


#endif
