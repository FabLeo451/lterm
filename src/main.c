
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
 * @file main.c
 * @brief The main file
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <libgen.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <libssh/libssh.h> 
#include <libssh/callbacks.h>
#include <pwd.h>
#include "main.h"
#include "gui.h"
#include "profile.h"
#include "connection.h"
#include "protocol.h"
#include "ssh.h"
#include "sftp-panel.h"
#include "utils.h"
#include "config.h"
#include "async.h"

#ifdef __APPLE__
#include <sys/event.h>
#include <signal.h>
#include <unistd.h>
#endif

int switch_local = 0;     /* start with local shell */

Globals globals;
Prefs prefs;
struct Protocol_List g_prot_list;
struct ProfileList g_profile_list;
GApplication *application;

char app_name[256];

void load_settings ();
void save_settings ();
void show_version ();
void help ();

#ifdef DEBUG
char gCurrentFunction[512];

void setCurrentFunction (char *f)
{
  strcpy (gCurrentFunction, f);
}

char * getCurrentFunction () { return &gCurrentFunction[0]; }
#endif

void *malloc ();

/* Allocate an N-byte block of memory from the heap.
   If N is zero, allocate a 1-byte block.  */
   
void* rpl_malloc (size_t n)
{
 if (n == 0)
    n = 1;
 return malloc (n);
}

void
log_reset ()
{
  FILE *log_fp;

  log_fp = fopen (globals.log_file, "w");

  if (log_fp == NULL)
    return;

  fclose (log_fp);
}

void
log_write (const char *fmt,...)
{
  FILE *log_fp;
  char time_s[64];
  char log_file[256];
  char line[2048];
  char msg[2048];

  time_t tmx;
  struct tm *tml;

  va_list ap;

  tmx = time (NULL);
  tml = localtime (&tmx);

  log_fp = fopen (globals.log_file, "a");

  if (log_fp == NULL)
    return;

  strftime (time_s, sizeof (time_s), "%Y-%m-%d %H:%M:%S", tml);
    
  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);

  sprintf (line, "%s: %s", time_s, msg);
  fprintf (log_fp, "%s", line);

#ifdef DEBUG
  printf ("%s", line);
#endif
  
  fflush (log_fp);

  fclose (log_fp);
}

#ifndef WIN32
static void
sigterm_handler (int signalnum, siginfo_t *si, void *data)
{
    printf ("Caught %d signal, exiting...\n", signalnum);
    application_quit ();
}

static void handle_signals (void)
{
  struct sigaction sa;

  sa.sa_sigaction = sigterm_handler;
  sigemptyset (&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
    
  sigaction (SIGTERM, &sa, NULL);
  //sigaction (SIGINT,  &sa, 0);
}
#endif

//int gtkRefreshing = 0;

gboolean
doGTKMainIteration ()
{
  //log_debug ("[thread 0x%08x] Start\n", pthread_self());

  while (gtk_events_pending ())
    gtk_main_iteration ();

  //log_debug ("[thread 0x%08x] End\n", pthread_self());

  return (G_SOURCE_REMOVE);
}

void
addIdleGTKMainIteration ()
{
  gdk_threads_add_idle (doGTKMainIteration, NULL);
  //g_usleep (5000);
/*
gdk_threads_add_idle_full (G_PRIORITY_HIGH_IDLE,
                           doGTKMainIteration,
                           NULL,
                           NULL);
*/
}

void
lterm_iteration ()
{
  struct Iteration_Function_Request ifr_function;
  struct ConnectionTab *lterminal;

  //log_debug ("Start\n");

  //while (gtk_events_pending ())
  //  gtk_main_iteration ();

  doGTKMainIteration ();

  while (ifr_get (&ifr_function))
    {
      log_debug ("Requested iteration function: %d\n", ifr_function.id);

      switch (ifr_function.id) {
        case ITERATION_REBUILD_TREE_STORE:
          rebuild_tree_store ();
          break;

        case ITERATION_REFRESH_TREE_VIEW:
          refresh_connection_tree_view ((struct GtkTreeView *) ifr_function.user_data);
          break;

        case ITERATION_REFRESH_SFTP_PANEL:
          refresh_current_sftp_panel ();
          break;

        case ITERATION_CLOSE_TAB:
          connection_tab_close ((SConnectionTab *) ifr_function.user_data);
          break;
/*
        case ITERATION_REFRESH_QUICK_LAUCH_TREE_VIEW:
          refresh_connection_tree_view (GTK_TREE_VIEW (g_quick_launch_window.tree_view));
          break;

        case ITERATION_SSH_KEEPALIVE:
          ssh_list_keepalive (&globals.ssh_list);
          break;
*/

        default:
          break;

      } /* switch */
    }
/*
  if (lterminal = get_current_connection_tab ())
    {
      if (lterminal->ssh_info.ssh_node)
        {
          if (lterminal->ssh_info.ssh_node->valid && difftime (time (NULL), lterminal->ssh_info.ssh_node->last) > 60)
            {
              ssh_node_keepalive (lterminal->ssh_info.ssh_node);
              ssh_node_update_time (lterminal->ssh_info.ssh_node);
            }
        }
    }
*/
   
  /* prevent from 100% cpu usage */
  g_usleep (1000);

  //log_debug ("End\n");
}

void
notifyMessage (char *message)
{
  log_debug ("%s\n", message);

#ifdef __APPLE__
  char cmd[4096];
  int exit_code;

  sprintf (cmd, "osascript -e 'display notification \"%s\" with title \"lterm\"'", message);

  log_write ("Executing: %s\n", cmd);
  exit_code = system (cmd);
  log_write ("exit_code = %d\n", exit_code);
#else  
	GNotification *notification = g_notification_new ("lterm");
	g_notification_set_body (notification, message);
	//GIcon *icon = g_themed_icon_new ("dialog-information");
	//g_notification_set_icon (notification, icon);
	g_application_send_notification (application, NULL, notification);
	//g_object_unref (icon);
	g_object_unref (notification);
#endif
}


int sTimeout = 0;
pthread_t tidAlarm = 0; // Thread that must receive alarms

// A thread requests receive alarm
void
threadRequestAlarm ()
{
  tidAlarm = pthread_self ();
  log_debug ("0x%08x\n", tidAlarm);
}

void
threadResetAlarm ()
{
  tidAlarm = 0;
}

void 
AlarmHandler (int sig) 
{
  log_debug ("[thread 0x%08x] %d\n", pthread_self (), sig);
  sTimeout = 1;

  if (tidAlarm && tidAlarm != pthread_self ()) {
    log_debug ("Send alarm to thread 0x%08x\n", tidAlarm);
    pthread_kill (tidAlarm, sig);
  }
} 

void
timerStart (int seconds)
{
  signal (SIGALRM, AlarmHandler); 
  sTimeout = 0; 
  alarm (seconds); 
  log_debug ("Timer started: %d\n", seconds);
}

void
timerStop ()
{
  signal (SIGALRM, SIG_DFL);
  sTimeout = 0; 
  alarm (0);
  log_debug ("Timer stopped\n");
}

int timedOut () { return (sTimeout == 1); }

int
main (int argc, char *argv[])
{
  char argv_0[256];
  int c, n, rc;
  int digit_optind;
  int opt;
  int i;

//printf ("%s\n", shortenString ("1234567890ABCDEFGHILMNOPQRSTUVZ", 25));
//return 0;

/*
char **a = NULL;
char s[2048];
strcpy (s, "ssh -l user   -i \"identity file\"  bye");
a = splitString (s, " ", FALSE, "\"", TRUE, &n);
\*
strcpy (s, "fabio@fabio-HP:/dati/Source/lterm$ ls\n"
  "acconfig.h           config.guess      connections.xml     install-sh          missing             risultato.txt\n"
  "aclocal.m4           config.h.in       COPYING             l368154_lterm.sql   myterm.conf         sqlnet.log\n"
  "AUTHORS              config.log        data                l368154_lterm.sql~  NEWS                src\n"
  "autom4te.cache       config.status     debian-package.txt  libtool             NEWS~               stamp-h1\n"
  "autoscan.log         config.sub        depcomp             longstring.txt      nohup.out           TODO\n"
  "auto_xyz_howto.txt   configure         en                  longstring.txt~     notes.txt\n"
  "auto_xyz_howto.txt~  configure.ac      err                 lterm.conf          notes.txt~\n"
  "ChangeLog            configure.ac~     errori.txt          Makefile            prova.xml\n"
  "ChangeLog~           configure.in~     img                 Makefile.am         README\n"
  "compile              configure.lineno  INSTALL             Makefile.in         recent_connections\n"
  "fabio@fabio-HP:/dati/Source/lterm$ \n"
  "fabio@fabio-HP:/dati/Source/lterm$ \n");

a = splitString (s, "\n", FALSE, NULL, FALSE, &n);
*\
printf ("n = %d\n", n);
for (i = 0; i < n; i++)
  printf ("a[%d] = %s\n", i, a[i]);
free (a);
return (0);
*/


 
  setlocale (LC_ALL, "");
  textdomain (PACKAGE);
  bindtextdomain (PACKAGE, LOCALEDIR);

  sprintf (argv_0, "%s", argv[0]);
  sprintf (app_name, "%s", basename (argv_0));


  /* command line options */

  if (argc > 1)
    {
      if (!strcmp (argv[1], "--version"))
        {
          show_version ();
          exit (0);
        }
      else if (!strcmp (argv[1], "--help"))
        {
          help ();
          exit (0);
        }
    }

  memset (&globals, 0x00, sizeof (globals));

  while ((opt = getopt (argc, argv, "vh")) != -1)
    {
      switch (opt)
        {
        case 'v':
          show_version ();
          exit (0);
          break;

        case 'h':
          help ();
          exit (0);
          break;

        default:
          exit (1);
        }
    }
/*    
  i = optind;

  if (i < argc)
    {
      strcpy (globals.start_session_file, argv[i]);
    }
*/

  for (i=optind; i<argc; i++)
    {
      if (i > optind)
        strcat (globals.start_connections, "#");
      
      strcat (globals.start_connections, argv[i]);
    }

  /* get user's home directory */
    
  const char *homeDir = getenv ("HOME");
    
  if (!homeDir)
    {
      struct passwd* pwd = getpwuid (getuid ());
        
      if (pwd)
        homeDir = pwd->pw_dir;
    }

  strcpy (globals.home_dir, homeDir);
    
  sprintf (globals.app_dir, "%s/.%s", globals.home_dir, PACKAGE_NAME);
  sprintf (globals.serverlist, "%s/serverlist", globals.app_dir); /* deprecated */
  sprintf (globals.connections_xml, "%s/connections.xml", globals.app_dir);
  sprintf (globals.session_file, "%s/session.xml", globals.app_dir);
  sprintf (globals.log_file, "%s/lterm.log", globals.app_dir);
  sprintf (globals.profiles_file, "%s/profiles.xml", globals.app_dir);
  sprintf (globals.protocols_file, "%s/protocols.xml", globals.app_dir);
  //sprintf (globals.recent_connections_file, "%s/recent_connections", globals.app_dir);
  sprintf (globals.recent_connections_file, "%s/recents.xml", globals.app_dir);
  sprintf (globals.recent_sessions_file, "%s/recent_sessions", globals.app_dir);
  sprintf (globals.conf_file, "%s/%s.conf", globals.app_dir, PACKAGE_NAME);
  globals.connected = CONNECTION_NONE;
  globals.upgraded = 0;

  strcpy (globals.img_dir, IMGDIR);
  strcpy (globals.data_dir, PKGDATADIR);

  log_reset ();
  
  log_write ("Starting %s %s\n", PACKAGE, VERSION);
  log_write ("GTK version: %d.%d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
  log_write ("libssh version %s\n", ssh_version (0));
  log_write ("Desktop environment: %s\n", get_desktop_environment_name (get_desktop_environment ()));
  
  log_debug ("globals.home_dir=%s\n", globals.home_dir);
  
#ifdef __APPLE__
  log_debug ("Checking if running in a bundle\n");
  
  char *_xdg_data_dirs = (char *) getenv ("XDG_DATA_DIRS");

  log_debug ("_xdg_data_dirs=%s\n", _xdg_data_dirs);

  if (_xdg_data_dirs)
    {
      if (strstr (_xdg_data_dirs, "Contents/Resources/"))
        {
          log_debug ("Modify directories\n");
          
          sprintf (globals.img_dir, "%s/lterm/img", _xdg_data_dirs);
          sprintf (globals.data_dir, "%s/lterm/data", _xdg_data_dirs);
        }
    }
#endif

  log_debug ("globals.img_dir=%s\n", globals.img_dir);
  log_debug ("globals.data_dir=%s\n", globals.data_dir);

  log_write ("Loading settings...\n");
  load_settings ();

  mkdir (globals.app_dir, S_IRWXU|S_IRWXG|S_IRWXO);

  log_write ("Loading protocols...\n");
  pl_init (&g_prot_list);
  //n = load_protocols_from_file (globals.conf_file, &g_prot_list);
  n = load_protocols_from_file_xml (globals.protocols_file, &g_prot_list);
  log_write ("Loaded protocols: %d\n", n);

  /* check basic protocols have been loaded and create if not */
  log_write ("Checking standard protocols ...\n");
  check_standard_protocols (&g_prot_list);
  
  log_write ("Loading profiles...\n");
  profile_list_init (&g_profile_list);
  
  rc = load_profiles (&g_profile_list, globals.profiles_file);
  
  if (rc != 0 || profile_count (&g_profile_list) == 0)
    {
      if (profile_count (&g_profile_list) == 0)
        log_write ("No profile loaded\n");
      else
        log_write ("Profiles file not found: %s\n", globals.profiles_file);
      
      log_write ("Creating default profile...\n");
      profile_create_default (&g_profile_list);
    }
    
  ssh_list_init (&globals.ssh_list);

  log_write ("Initializing threads...\n");

  XInitThreads ();

  /* Secure glib */
  if (!g_thread_supported())
      g_thread_init(NULL);

  /* Secure gtk */
  gdk_threads_init();

  ssh_threads_set_callbacks(ssh_threads_get_pthread());

  log_write ("Building gui...\n");
  start_gtk (argc, argv);

  // Register application. Needed for desktop notifications
  application = g_application_new ("lterm.application", G_APPLICATION_FLAGS_NONE);
	g_application_register (application, NULL, NULL);

  log_write ("Initializing iteration function requests\n");
  ifr_init ();

  log_write ("Initializing SFTP\n");
  ssh_init ();

#ifdef __linux__DONT_USE
  log_write ("Initializing INotify\n");

  globals.inotifyFd = inotify_init();

  if (globals.inotifyFd == -1) {
    log_write("Can't init inotify\n");
  }
#else
    /*
    // Not currently used for kevent returns error 35 Resource temporarily unavailable
    // So check file modification time
    log_write ("Initializing FSEvents\n");

    globals.inotifyFd = kqueue ();
    log_write ("globals.inotifyFd = %d\n", globals.inotifyFd);
    */
#endif

  /* command line connections */

  char s_tmp[256];

  if (list_count (globals.start_connections, '#'))
    {
      for (i=1; i<=list_count (globals.start_connections, '#'); i++)
        {
          list_get_nth (globals.start_connections, i, '#', s_tmp);
          open_connection (s_tmp);
        }
    }
  else
    {
      if (prefs.save_session)
        load_session_file (NULL);
    }

  /* local shell */

  if (prefs.startup_local_shell)
    connection_new_terminal ();

  /* log on window */
    
  if (prefs.startup_show_connections)
    connection_log_on ();

  /* start main loop */
    
#ifndef WIN32
  //handle_signals ();
#endif

  globals.running = 1;

  // Init asyncronous section
  asyncInit();

  // Start async loop
  GThread   *thread;
  //GError    *error = NULL;

  //thread = g_thread_create (async_lterm_loop, NULL, FALSE, &error );
  thread = g_thread_new ("async-loop", async_lterm_loop, NULL);

  if (!thread) {
    //msgbox_error ("Can't start async loop:\n%s\n", error->message );
    msgbox_error ("Can't start async loop\n");
    exit (1);
  }

  log_write ("Starting main loop\n");

  while (globals.running)
    {
      lterm_iteration ();
    }

  log_write ("Saving session...\n");
  save_session_file (NULL); /* update session file */
  
  log_write ("Saving recent connections...\n");
  save_recent_connections ();
  
  log_write ("Saving connections...\n");
  save_connections_to_file_xml (globals.connections_xml);  
  
  log_write ("Saving protocols...\n");
  //save_protocols_to_file (globals.conf_file, &g_prot_list);
  save_protocols_to_file_xml (globals.protocols_file, &g_prot_list);

  pl_release (&g_prot_list);

  log_write ("Saving settings...\n");
  save_settings ();
  
  log_write ("Saving profiles...\n");
  save_profiles (&g_profile_list, globals.profiles_file);
  profile_list_release (&g_profile_list);

  log_write ("Removing all watch file descriptors...\n");
  sftp_panel_mirror_file_clear (NULL, 1);

  g_object_unref (application);
  
  log_write ("End\n");

  return 0;
}

void
load_settings ()
{
  int rc;
  char last_package_version[16];

  rc = profile_load_string (globals.conf_file, "general", "package_version", last_package_version, "0.0.0");

  /* no profile found means this is the first installation */

  if (rc == PROFILE_FILE_NOT_FOUND)
    {
      strcpy (last_package_version, "100.100.100");
    }

  if (cmpver (last_package_version, VERSION) < 0)
    globals.upgraded = 1;
  else
    globals.upgraded = 0;

  log_debug ("Actual version is %s, last version was %s %d %s\n", 
             VERSION, last_package_version, cmpver (last_package_version, VERSION), globals.upgraded ? "(upgraded)" : "");

  /* if this is an upgrade delete old settings and reset modified ones with new default */

  if (globals.upgraded)
    {
      log_debug ("last-version=%s current-version=%s\n", last_package_version, VERSION);

      profile_modify_int (PROFILE_DELETE, globals.conf_file, "terminal", "mouse_autohide", 0);
      profile_modify_int (PROFILE_DELETE, globals.conf_file, "general", "mouse_autohide", 0);
      profile_modify_int (PROFILE_DELETE, globals.conf_file, "protocols", "samba", 0);
    }

  /* load settings */

  /* emulation_list is not saved on exit, actually */
  //profile_load_string (globals.conf_file, "general", "emulation_list", prefs.emulation_list, "xterm:vt100:vt220:vt320:vt440");
  
  prefs.tabs_position = profile_load_int (globals.conf_file, "general", "tabs_position", GTK_POS_TOP);
  prefs.search_by = profile_load_int (globals.conf_file, "general", "search_by", 0);
  prefs.check_connections = profile_load_int (globals.conf_file, "general", "check_connections", 1);
  profile_load_string (globals.conf_file, "general", "warnings_color", prefs.warnings_color, "orange");
  profile_load_string (globals.conf_file, "general", "warnings_error_color", prefs.warnings_error_color, "red");
  profile_load_string (globals.conf_file, "general", "local_start_directory", prefs.local_start_directory, "");
  prefs.save_session = profile_load_int (globals.conf_file, "general", "save_session", 0);
  prefs.max_recent_connections = profile_load_int (globals.conf_file, "general", "max_recent_connections", 10);
  prefs.max_recent_sessions = profile_load_int (globals.conf_file, "general", "max_recent_sessions", 10);
  prefs.checkpoint_interval = profile_load_int (globals.conf_file, "general", "checkpoint_interval", 5);
  profile_load_string (globals.conf_file, "general", "font_fixed", prefs.font_fixed, DEFAULT_FIXED_FONT);
  prefs.hyperlink_tooltip_enabled = profile_load_int (globals.conf_file, "general", "hyperlink_tooltip_enabled", 1);
  prefs.hyperlink_click_enabled = profile_load_int (globals.conf_file, "general", "hyperlink_click_enabled", 1);
  profile_load_string (globals.conf_file, "general", "tempDir", prefs.tempDir, globals.app_dir/*"/tmp"*/);

  prefs.startup_show_connections = profile_load_int (globals.conf_file, "TERMINAL", "startup_show_connections", 0);
  prefs.startup_local_shell = switch_local ? 1 : profile_load_int (globals.conf_file, "TERMINAL", "startup_local_shell", 1);
  
  profile_load_string (globals.conf_file, "TERMINAL", "extra_word_chars", prefs.extra_word_chars, ":@-./_~?&=%+#");
  
  prefs.rows = profile_load_int (globals.conf_file, "TERMINAL", "rows", 80);
  prefs.columns = profile_load_int (globals.conf_file, "TERMINAL", "columns", 25);
  
  profile_load_string (globals.conf_file, "TERMINAL", "character_encoding", prefs.character_encoding, "");
  prefs.scrollback_lines = profile_load_int (globals.conf_file, "TERMINAL", "scrollback_lines", 512);
  prefs.scroll_on_keystroke = profile_load_int (globals.conf_file, "TERMINAL", "scroll_on_keystroke", 1);
  prefs.scroll_on_output = profile_load_int (globals.conf_file, "TERMINAL", "scroll_on_output", 1);

  prefs.mouse_autohide = profile_load_int (globals.conf_file, "MOUSE", "autohide", 1);
  prefs.mouse_copy_on_select = profile_load_int (globals.conf_file, "MOUSE", "copy_on_select", 0);
  prefs.mouse_paste_on_right_button = profile_load_int (globals.conf_file, "MOUSE", "paste_on_right_button", 0);

  /*prefs.x = profile_load_int (globals.conf_file, "GUI", "x", 0);
  prefs.y = profile_load_int (globals.conf_file, "GUI", "y", 0);*/
  prefs.w = profile_load_int (globals.conf_file, "GUI", "w", 640);
  prefs.h = profile_load_int (globals.conf_file, "GUI", "h", 480);
  prefs.maximize = profile_load_int (globals.conf_file, "GUI", "maximize", 0);
  prefs.toolbar = profile_load_int (globals.conf_file, "GUI", "toolbar", 1);
  prefs.statusbar = profile_load_int (globals.conf_file, "GUI", "statusbar", 1);
  prefs.fullscreen = profile_load_int (globals.conf_file, "GUI", "fullscreen", 0);
  
  prefs.tab_alerts = profile_load_int (globals.conf_file, "GUI", "tab_alerts", 1);
  profile_load_string (globals.conf_file, "GUI", "tab_status_changed_color", prefs.tab_status_changed_color, "blue");
  //profile_load_string (globals.conf_file, "GUI", "tab_status_connecting_color", prefs.tab_status_connecting_color, "#707000");
  profile_load_string (globals.conf_file, "GUI", "tab_status_disconnected_color", prefs.tab_status_disconnected_color, "#707070");
  profile_load_string (globals.conf_file, "GUI", "tab_status_disconnected_alert_color", prefs.tab_status_disconnected_alert_color, "darkred");
  prefs.show_sidebar = profile_load_int (globals.conf_file, "GUI", "show_sidebar", 1);
  profile_load_string (globals.conf_file, "GUI", "font_quick_launch_window", prefs.font_quick_launch_window, "Sans 9");

  prefs.sftp_buffer = profile_load_int (globals.conf_file, "SFTP", "sftp_buffer", 128*1024);
  prefs.flag_ask_download = profile_load_int (globals.conf_file, "SFTP", "flag_ask_download", 1);
  profile_load_string (globals.conf_file, "SFTP", "download_directory", prefs.download_dir, "");
  
  profile_load_string (globals.conf_file, "SFTP", "text_editor", prefs.text_editor, "");
  
  if (prefs.text_editor[0] == 0)
    {
#ifdef __APPLE__
      strcpy (prefs.text_editor, "TextEdit");
#else
      switch (get_desktop_environment ()) {
        case DE_GNOME: 
        case DE_CINNAMON: 
          strcpy (prefs.text_editor, "gedit"); 
          break;
        case DE_KDE: strcpy (prefs.text_editor, "kate"); break;
        case DE_XFCE: strcpy (prefs.text_editor, "mousepad"); break;
        default: strcpy (prefs.text_editor, "gedit"); break;
      }
#endif
    }
    
  profile_load_string (globals.conf_file, "SFTP", "sftp_open_file_uri", prefs.sftp_open_file_uri, "sftp://%u@%h/%f");
  prefs.ssh_keepalive = profile_load_int (globals.conf_file, "SFTP", "ssh_keepalive", 10);
  prefs.ssh_timeout = profile_load_int (globals.conf_file, "SFTP", "ssh_timeout", 3);
  profile_load_string (globals.conf_file, "SFTP", "sftp_panel_background", prefs.sftp_panel_background, "white");
  //profile_load_string (globals.conf_file, "SFTP", "last_upload_dir", prefs.last_upload_dir, "");
  //profile_load_string (globals.conf_file, "SFTP", "last_download_dir", prefs.last_download_dir, "");
}

void
save_settings ()
{
  int err;

  /* store the version of program witch saved this profile */

  profile_modify_string (PROFILE_SAVE, globals.conf_file, "general", "package_version", VERSION);

  //profile_modify_string (globals.conf_file, "general", "emulation_list", prefs.emulation_list);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "tabs_position", prefs.tabs_position);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "search_by", prefs.search_by);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "check_connections", prefs.check_connections);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "general", "warnings_color", prefs.warnings_color);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "general", "local_start_directory", prefs.local_start_directory);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "save_session", prefs.save_session);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "max_recent_connections", prefs.max_recent_connections);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "max_recent_sessions", prefs.max_recent_sessions);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "checkpoint_interval", prefs.checkpoint_interval);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "general", "font_fixed", prefs.font_fixed);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "hyperlink_tooltip_enabled", prefs.hyperlink_tooltip_enabled);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "general", "hyperlink_click_enabled", prefs.hyperlink_click_enabled);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "general", "tempDir", prefs.tempDir);
  
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "startup_show_connections", prefs.startup_show_connections);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "startup_local_shell", prefs.startup_local_shell);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "scrollback_lines", prefs.scrollback_lines);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "scroll_on_keystroke", prefs.scroll_on_keystroke);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "scroll_on_output", prefs.scroll_on_output);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "rows", prefs.rows);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "TERMINAL", "columns", prefs.columns);

  profile_modify_int (PROFILE_SAVE, globals.conf_file, "MOUSE", "autohide", prefs.mouse_autohide);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "MOUSE", "copy_on_select", prefs.mouse_copy_on_select);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "MOUSE", "paste_on_right_button", prefs.mouse_paste_on_right_button);

  profile_modify_int (PROFILE_SAVE, globals.conf_file, "GUI", "maximize", prefs.maximize);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "GUI", "w", prefs.w);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "GUI", "h", prefs.h);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "GUI", "tab_alerts", prefs.tab_alerts);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "GUI", "show_sidebar", prefs.show_sidebar);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "GUI", "font_quick_launch_window", prefs.font_quick_launch_window);

  profile_modify_int (PROFILE_SAVE, globals.conf_file, "SFTP", "sftp_buffer", prefs.sftp_buffer);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "SFTP", "flag_ask_download", prefs.flag_ask_download);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "SFTP", "download_directory", prefs.download_dir);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "SFTP", "text_editor", prefs.text_editor);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "SFTP", "sftp_open_file_uri", prefs.sftp_open_file_uri);
  profile_modify_int (PROFILE_SAVE, globals.conf_file, "SFTP", "ssh_timeout", prefs.ssh_timeout);
  profile_modify_string (PROFILE_SAVE, globals.conf_file, "SFTP", "sftp_panel_background", prefs.sftp_panel_background);
  //profile_modify_string (PROFILE_SAVE, globals.conf_file, "SFTP", "last_upload_dir", prefs.last_upload_dir);
  //profile_modify_string (PROFILE_SAVE, globals.conf_file, "SFTP", "last_download_dir", prefs.last_download_dir);
}

void 
get_version (char *version)
{
  strcpy (version, "");

#ifdef CREATIONDATE
  sprintf (version, "%s-%s", VERSION, CREATIONDATE);
#else
  sprintf (version, "%s", VERSION);
#endif

#ifdef SO
  strcat (version, " ");
  strcat (version, SO);
#endif
}


void
show_version ()
{
  char version[64];

  get_version (version);
  printf ("%s\n", version);
}

int
cmpver (char *_v1, char *_v2)
{
  int version1, revision1, release1;
  int version2, revision2, release2;
  int cmp1, cmp2, cmp3;
  char v1[16], v2[16];
  char tmp[8];
  char *pc;

  strcpy (v1, _v1);
  strcpy (v2, _v2);

  log_debug ("%s vs %s\n", v1, v2);

  pc = (char *) strpbrk (v1, "-_abcdefghijklmnopqrstuvwxyz");

  if (pc)
    *pc = 0;

  pc = (char *) strpbrk (v2, "-_abcdefghijklmnopqrstuvwxyz");

  if (pc)
    *pc = 0;

  log_debug ("%s vs %s\n", v1, v2);

  list_get_nth (v1, 1, '.', tmp); version1 = atoi (tmp);
  list_get_nth (v1, 2, '.', tmp); revision1 = atoi (tmp);
  list_get_nth (v1, 3, '.', tmp); release1 = atoi (tmp);

  list_get_nth (v2, 1, '.', tmp); version2 = atoi (tmp);
  list_get_nth (v2, 2, '.', tmp); revision2 = atoi (tmp);
  list_get_nth (v2, 3, '.', tmp); release2 = atoi (tmp);

  if (version1 > version2)
    return 1;
  else if (version1 < version2)
    return -1;
  else
    {
      if (revision1 > revision2)
        return 1;
      else if (revision1 < revision2)
        return -1;
      else
        {
          if (release1 > release2)
            return 1;
          else if (release1 < release2)
            return -1;
          else
            return 0;
        }
    }

  return 0;
}

void
help ()
{
  char version[64];

  /* printf ("\n%s\n", PACKAGE); */

  get_version (version);
  printf ("\n%s version %s\n", PACKAGE, version);

  printf (/* "\n" */
          "Usage : %s [options] [session-file]\n"
          "        %s [options] conn:[user[/password]]@connection-name\n"
          "Options:\n"
          "  -v            : show version\n"
          "  -h --help     : help\n",
          app_name, app_name
         );
}

