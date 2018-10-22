
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
 * @file sftp-panel.c
 * @brief SFTP functions
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <fcntl.h>
#include <fnmatch.h>

#include "main.h"
#include "gui.h"
#include "utils.h"
#include "ssh.h"
#include "sftp-panel.h"
#include "xml.h"
#include "terminal.h"
#include "async.h"

#ifdef __linux____DONT_USE
#include <sys/inotify.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#endif

extern Globals globals;
extern Prefs prefs;
extern GtkWidget *main_window;
extern struct Connection_List conn_list;

struct SFTP_Panel sftp_panel;
struct SSH_Info *p_ssh_current;
//GtkWidget *entry_sftp_position;
GdkPixbuf *pixbuf_file, *pixbuf_dir;
GtkTreeSelection *g_sftp_selection;
GSList *g_selected_files = NULL;
GList *g_filetypes;

char *transferStatusDesc[] = { "Ready", "In progress", "Paused", "Cancelled by user", "Cancelled for errors", "Completed", 0 };

GtkWidget *transfer_window=NULL;
GtkWidget *label_status;
GtkWidget *label_from;
GtkWidget *label_to;
GtkWidget *label_speed;
GtkWidget *label_time_left;
GtkWidget *progress_transfer;
int g_transfer;

GArray *mirrorFiles = NULL;

char transfer_error[512];

enum { COLUMN_FILE_ICON, COLUMN_FILE_NAME, COLUMN_FILE_SIZE, COLUMN_FILE_DATE, N_FILE_COLUMNS };
enum { SORTID_ICON = 0, SORTID_NAME, SORTID_SIZE, SORTID_DATE };
  
GtkListStore *list_store_fs;
GtkTreeModel *tree_model_fs;

GtkActionEntry sftp_popup_menu_items [] = {
  { "CreateFolder", "folder", N_("_Create folder"), "", NULL, G_CALLBACK (sftp_panel_create_folder) },
  { "CreateFile", "document-new", N_("_Create file"), "", NULL, G_CALLBACK (sftp_panel_create_file) },
  { "EditFile", "gtk-edit", N_("_Edit"), "", NULL, G_CALLBACK (sftp_panel_open) },
  { "Rename", NULL, N_("_Rename"), "", NULL, G_CALLBACK (sftp_panel_rename) },
  { "ChangeTime", "Change _Time", N_("Change _Time"), "", NULL, G_CALLBACK (sftp_panel_change_time) },
  { "CopyPathToClipboard", "edit-copy", N_("Copy _path to clipboard"), "", NULL, G_CALLBACK (sftp_panel_copy_path_clipboard) },
  { "Delete", "_Delete", N_("_Delete"), "", NULL, G_CALLBACK (sftp_panel_delete) },
  //{ "CopyNameTerminal", NULL, N_("C_opy name to terminal"), "", NULL, G_CALLBACK (sftp_panel_copy_name_terminal) },
  //{ "CopyPathTerminal", NULL, N_("C_opy file path to terminal"), "", NULL, G_CALLBACK (sftp_panel_copy_path_terminal) },
  { "Upload", NULL, N_("_Upload"), "", NULL, G_CALLBACK (sftp_upload_files) },
  { "Download", NULL, N_("_Download"), "", NULL, G_CALLBACK (sftp_download_files) }
};

GtkActionGroup *action_group_ssh_adds = NULL;
int ssh_adds_ui_id;

gchar *ui_sftp_popup_desc =
  "<ui>"
  "  <popup name='SFTPPopupMenu' accelerators='true'>"
  "    <menuitem action='CreateFolder'/>"
  "    <menuitem action='CreateFile'/>"
  "    <menuitem action='EditFile'/>"
  "    <menuitem action='Rename'/>"
  "    <menuitem action='ChangeTime'/>"
  "    <menuitem action='CopyPathToClipboard'/>"
  "    <menuitem action='Delete'/>"
  //"    <menuitem action='CopyNameTerminal'/>"
  //"    <menuitem action='CopyPathTerminal'/>"
  "    <separator />"
  "    <placeholder name='Additionals' />"
  "    <menuitem action='Upload'/>"
  "    <menuitem action='Download'/>"
  "  </popup>"
  "</ui>";
  
char ui_sftp_popup_desc_adds[4000];
  
struct CustomMenuItem SSHMenuItems[MAX_SSH_MENU_ITEMS];
int n_ssh_menu_items = 0;

void
vsftp_set_status_mode (int mode, const char *fmt, va_list ap)
{
  char text[4096];

  vsprintf (text, fmt, ap);
  
  gtk_label_set_text (GTK_LABEL(sftp_panel.label_sftp_status), text);

  log_debug ("[thread 0x%08x] mode=%d %s\n", pthread_self(), mode, text);

  if (mode == SFTP_STATUS_IMMEDIATE)  
    doGTKMainIteration (); // Freezes the program on eof (since threads)
  else
    addIdleGTKMainIteration ();

  log_debug ("[thread 0x%08x] done\n", pthread_self());
}

void
sftp_set_status_mode (int mode, const char *fmt,...)
{
  va_list ap;
  
  va_start (ap, fmt);
  vsftp_set_status_mode (mode, fmt, ap);
  va_end (ap);
}

void
sftp_set_status (const char *fmt,...)
{
  char text[4096];
  va_list ap;
  
  va_start (ap, fmt);
  vsftp_set_status_mode (SFTP_STATUS_IMMEDIATE, fmt, ap);
  va_end (ap);
}

/*
void
sftp_clear_status_mode (int mode)
{
  sftp_set_status_mode (mode, "");
}
*/
void
sftp_clear_status ()
{
  sftp_set_status_mode (SFTP_STATUS_IDLE, "");
}

void
sftp_spinner_start ()
{
  gtk_widget_show (sftp_panel.spinner);
  gtk_spinner_start (GTK_SPINNER(sftp_panel.spinner));
}

void
sftp_spinner_stop ()
{
  gtk_spinner_stop (GTK_SPINNER(sftp_panel.spinner));
  gtk_widget_hide (sftp_panel.spinner);
}

void
sftp_spinner_refresh ()
{
  int i;

  for (i=0;i<500;i++)
    { 
      g_usleep (1);
      gtk_main_iteration_do (FALSE);
      //lterm_iteration ();
    }
}

int
transfer_set_error (STransferInfo *pTi, int code, char *fmt, ...)
{
  pTi->result = code;
  pTi->state = TR_CANCELLED_ERRORS;

  va_list ap;
  va_start (ap, fmt);
  vsprintf (pTi->errorDesc, fmt, ap);
  va_end (ap);

  log_write ("%s\n", pTi->errorDesc);

  return pTi->result;
}

char *
transfer_get_error (STransferInfo *pTi)
{
  return (pTi->errorDesc);
}

char *
getTransferStatusDesc (int i)
{
  return transferStatusDesc[i];
}

/**
 * sftp_copy_file_upload() - upload a file using an sftp session
 */
int
sftp_copy_file_upload (sftp_session sftp, struct TransferInfo *p_ti)
{
  int access_type = O_WRONLY | O_CREAT | O_TRUNC;
  sftp_file file;
  int nread, nwritten;
  unsigned int buffer[prefs.sftp_buffer];
  struct stat fileStat;
  //int rc = 0;

  log_debug ("From: %s\n", p_ti->source);
  log_debug ("To: %s\n", p_ti->destination);

  /* get source file size */
  if (stat (p_ti->source, &fileStat) < 0) 
    return (transfer_set_error (p_ti, 1, "Can't open file for reading:\n%s", p_ti->source));

  p_ti->size = fileStat.st_size;

  /* open local file for reading */
  int fd = open (p_ti->source, O_RDONLY);

  if (fd < 0)
    return (transfer_set_error (p_ti, 1, "Can't open file for reading:\n%s", p_ti->source));

  //////////////////////////////
  lockSSH (__func__, TRUE);

  log_write ("Opening remote file for writing: %s\n", p_ti->destination);

  // TODO: alarm is catched by thread but sftp_open() is not stopped
  threadRequestAlarm ();
  timerStart (2);  

  file = sftp_open (sftp, p_ti->destination, access_type, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

  threadResetAlarm ();
  timerStop ();

  lockSSH (__func__, FALSE);
  //////////////////////////////
  
  if (file == NULL)
    {
      close (fd);
      return (transfer_set_error (p_ti, 2, "Can't open file for writing: %s", p_ti->destination));
    }
  
  //transfer_window_update (p_ti);

  /* copy file */

  log_write ("Uploading %s on %s (%d bytes)\n", p_ti->source, p_ti->host, p_ti->size);

  while (((nread = read (fd, buffer, prefs.sftp_buffer)) != 0) && (p_ti->state == TR_IN_PROGRESS))
    {
      //////////////////////////////
      lockSSH (__func__, TRUE);

      //log_debug ("Read %d bytes\n", nread);

      nwritten = sftp_write (file, buffer, nread);

      //log_debug ("Written %d bytes\n", nwritten);

      lockSSH (__func__, FALSE);
      //////////////////////////////

      if (nwritten != nread)
        {
          transfer_set_error (p_ti, 3, "error while writing:\n%s", p_ti->destination);
          break;
        }

      p_ti->worked += nwritten;
    }

  //////////////////////////////
  lockSSH (__func__, TRUE);

  sftp_close (file);

  lockSSH (__func__, FALSE);
  //////////////////////////////

  close (fd);
  
  //transfer_window_update (p_ti);

  if (p_ti->result == 0)
    p_ti->state = TR_COMPLETED;

  return (p_ti->result);
}

/**
 * sftp_copy_file_download() - download a file using an sftp session
 */
int
sftp_copy_file_download (sftp_session sftp, struct TransferInfo *p_ti)
{
  int access_type = O_RDONLY;
  sftp_file file;
  char buffer[prefs.sftp_buffer];
  int nbytes, nwritten, rc;
  int fd;
  sftp_attributes attr;

  log_debug ("From: %s\n", p_ti->source);
  log_debug ("To: %s\n", p_ti->destination);

  ////////////////////////////////////
  lockSSH (__func__, TRUE);

  log_write ("Opening remote file for reading: %s\n", p_ti->source);

  // TODO: alarm is catched by thread but sftp_open() is not stopped
  threadRequestAlarm ();
  timerStart (2);

  file = sftp_open (sftp, p_ti->source, access_type, 0);

  lockSSH (__func__, FALSE);
  ////////////////////////////////////

  if (timedOut ()) {
    log_write ("Timeout!\n");
  }

  threadResetAlarm ();
  timerStop();

  if (file == NULL) {
    return (transfer_set_error (p_ti, 1, "Can't open remote file:\n%s", p_ti->source));
  }

  attr = sftp_fstat (file);

  if (attr == NULL) {
    return (transfer_set_error (p_ti, 1, "Can't stat remote file:\n%s", p_ti->source));
  }

  p_ti->size = attr->size;

  ////////////////////////////////////
  lockSSH (__func__, TRUE);

  sftp_attributes_free (attr);

  lockSSH (__func__, FALSE);
  ////////////////////////////////////

  log_debug ("Size: %lld\n", p_ti->size);

  fd = open (p_ti->destination, O_WRONLY|O_CREAT|O_TRUNC,
             S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH
            );

  if (fd < 0) 
    {
      return (transfer_set_error (p_ti, 2, "Can't open file for writing:\n%s", p_ti->destination));
    }

  //while (g_transfer)
  while (p_ti->state == TR_IN_PROGRESS)
    {
      ////////////////////////////////////
      lockSSH (__func__, TRUE);

      //log_debug ("reading (buffer size=%d %d)...\n", prefs.sftp_buffer, (int) sizeof (buffer));
      nbytes = sftp_read (file, buffer, sizeof (buffer));
      //log_debug ("nbytes=%d\n", nbytes);

      lockSSH (__func__, FALSE);
      ////////////////////////////////////

      if (nbytes == 0) 
        {
          break; // EOF
        } 
      else if (nbytes < 0) 
        {
          return (transfer_set_error (p_ti, 2, "Error while reading\n%s", p_ti->source));
        }

      nwritten = write (fd, buffer, nbytes);

      if (nwritten != nbytes)
        {
          return (transfer_set_error (p_ti, 3, "Error while writing\n%s", p_ti->destination));
        }

      p_ti->worked += nwritten;

      //log_debug ("%d / %d\n", p_ti->worked, p_ti->size);
/*
      if (p_ti->worked % SFTP_PROGRESS == 0)
        transfer_window_update (p_ti);
*/
//usleep (1000);
    }

  //////////////////////////////
  lockSSH (__func__, TRUE);

  sftp_close (file);

  lockSSH (__func__, FALSE);
  //////////////////////////////

  close (fd);
  
  //transfer_window_update (p_ti);

  if (p_ti->result == 0)
    p_ti->state = TR_COMPLETED;

  log_debug ("Donwloaded: %d bytes\n", p_ti->worked);

  return (p_ti->result);
}

/**
 * upload_directory() - upload recursively a directory tree
 */
int
upload_directory (struct SSH_Info *p_ssh, char *rootdir, char *destdir, STransferInfo *p_ti)
{
  struct TransferInfo ti;
  char *pc, rootdir_new[2048], destdir_new[2048], tmp_s[2048];
  DIR *dir;
  struct dirent *entry;
  struct stat info;
  int rc;

  if (!lt_ssh_is_connected (p_ssh))
    return (transfer_set_error (p_ti, 1, "Not connected"));

  memset (&ti, 0, sizeof (struct TransferInfo));

  log_debug ("Opening direcotry %s\n", rootdir);
  
  dir = opendir (rootdir);

  if (dir == NULL) {
    return (transfer_set_error (p_ti, 1, "Can't open directory\n%s", rootdir));
  }
    
  if (pc = (char *) strrchr (rootdir, '/'))
    {
      sprintf (destdir_new, "%s/%s", destdir, &pc[1]);
    }
  else
    {
      strcpy (destdir_new, destdir);
    }   

  ////////////////////////////////
  lockSSH (__func__, TRUE);

  log_debug ("Creating remote direcotry %s\n", destdir_new);

  rc = sftp_mkdir (p_ssh->ssh_node->sftp, destdir_new, S_IRWXU);

  lockSSH (__func__, FALSE);
  ////////////////////////////////

  if (rc != 0) 
    return (transfer_set_error (p_ti, rc, "Can't create remote directory:\n%s", destdir_new));

  log_debug ("Reading local files...\n");
  
  while ((entry = readdir (dir)) != NULL)
    {
      if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
        continue;
        
      sprintf (tmp_s, "%s/%s", destdir_new, entry->d_name);
      
      log_debug ("%s\n", tmp_s);
      
      stat (tmp_s, &info);
      
      if (S_ISDIR (info.st_mode))
        {
          sprintf (rootdir_new, "%s/%s", rootdir, entry->d_name);
          rc = upload_directory (p_ssh, rootdir_new, destdir_new, p_ti);
        }
      else
        {
          //strcpy (ti.status, "uploading...");
          ti.state = TR_IN_PROGRESS;
          sprintf (ti.source, "%s/%s", rootdir, entry->d_name);
          sprintf (ti.destination, "%s/%s", destdir_new, entry->d_name);

          rc = sftp_copy_file_upload (p_ssh->ssh_node->sftp, &ti);
        }

      if (rc != 0 || ti.state == TR_CANCELLED_USER)
        break;
    }

  closedir (dir);

  if (p_ti && !strcmp(p_ti->source, rootdir))
    p_ti->state = TR_COMPLETED;
   
  return (0);
}

/**
 * download_directory() - download recursively a directory tree
 */
int
download_directory (struct SSH_Info *p_ssh, char *rootdir, char *destdir, STransferInfo *p_ti)
{
  struct TransferInfo backupTi; // Temporary
  struct Directory_Entry entry;
  char *pc, rootdir_new[2048], destdir_new[2048];
  sftp_dir dir;
  sftp_attributes attributes;
  int rc;

  log_debug ("\n rootdir = %s\n destdir = %s\n", rootdir, destdir);

  if (!lt_ssh_is_connected (p_ssh)) {
    return (transfer_set_error (p_ti, 1, "Not connected"));
  }

  //memset (&ti, 0, sizeof (struct TransferInfo));

  log_debug ("Opening direcotry %s\n", rootdir);

  ////////////////////////////////
  lockSSH (__func__, TRUE);

  dir = sftp_opendir (p_ssh->ssh_node->sftp, rootdir);

  lockSSH (__func__, FALSE);
  ////////////////////////////////

  if (!dir) {
    return (transfer_set_error (p_ti, 1, "Can't open remote dir: %s\n", rootdir));
  }
    
  if (pc = (char *) strrchr (rootdir, '/'))
    {
      sprintf (destdir_new, "%s/%s", destdir, &pc[1]);
    }
  else
    {
      strcpy (destdir_new, destdir);
    }   
          
  log_debug ("Creating local direcotry %s\n", destdir_new);
  
  //rc = mkdir (destdir_new, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  rc = g_mkdir_with_parents (destdir_new, 0775);

  if (rc != 0) 
    return (transfer_set_error (p_ti, rc, "Can't create local directory:\n%s", destdir_new));
         
  log_debug ("Reading directory...\n");
 
  ////////////////////////////////
  lockSSH (__func__, TRUE);
  
  attributes = sftp_readdir (p_ssh->ssh_node->sftp, dir);

  lockSSH (__func__, FALSE);
  ////////////////////////////////

  log_debug ("Reading files...\n");

  while (attributes != NULL)
    {
      if (!strcmp (attributes->name, ".") || !strcmp (attributes->name, ".."))
        goto READ_NEXT_FILE;
        
      log_debug ("%s\n", attributes->name);
      
      //memset (&ti, 0, sizeof (struct TransferInfo));
      memset (&entry, 0, sizeof (struct Directory_Entry));

      entry.type = attributes->type;
      
      if (is_directory (&entry))
        {
          sprintf (rootdir_new, "%s/%s", rootdir, attributes->name);
          //sprintf (p_ti->source, "%s/%s", rootdir, attributes->name);
          //sprintf (p_ti->destination, "%s/%s", destdir_new, attributes->name);
          rc = download_directory (p_ssh, rootdir_new, destdir_new, p_ti);
          
          //memcpy (&ti, p_ti, sizeof (STransferInfo));
        }
      else
        {
          // Store current struct
          memcpy (&backupTi, p_ti, sizeof (STransferInfo));

          // Change some info (the pointer must remain the same)
          //strcpy (p_ti->status, "downloading...");
          p_ti->state = TR_IN_PROGRESS;
          sprintf (p_ti->source, "%s/%s", rootdir, attributes->name);
          sprintf (p_ti->destination, "%s/%s", destdir_new, attributes->name);
          //p_ti->sourceIsDir = FALSE;

          log_debug ("Downloading %s\n", p_ti->source);

          rc = sftp_copy_file_download (p_ssh->ssh_node->sftp, p_ti);

          // Store returned struct
          STransferInfo tmpTi;
          memcpy (&tmpTi, p_ti, sizeof (STransferInfo));

          // Restore original structure and update the current status
          memcpy (p_ti, &backupTi, sizeof (STransferInfo));

          p_ti->state = tmpTi.state;
          p_ti->result = tmpTi.result;
          strcpy (p_ti->errorDesc, tmpTi.errorDesc);
        }
  
      log_debug ("rc = %d\n", rc);

READ_NEXT_FILE:

      ////////////////////////////////
      lockSSH (__func__, TRUE);

      sftp_attributes_free (attributes);

      // Check for error o cancel
      if (rc != 0 || p_ti->state == TR_CANCELLED_USER) {
        log_write ("Error downloading directory: %s\n", p_ti->errorDesc);
        //transfer_set_error (p_ti, ti.result, ti.errorDesc);
        //p_ti->state = ti.state;
        lockSSH (__func__, FALSE);
        ////////////////////////////////
        break;
      }

      attributes = sftp_readdir (p_ssh->ssh_node->sftp, dir);

      lockSSH (__func__, FALSE);
      ////////////////////////////////
    }

  ////////////////////////////////
  lockSSH (__func__, TRUE);

  sftp_closedir (dir);

  lockSSH (__func__, FALSE);
  ////////////////////////////////

  //log_debug ("\np_ti->source = %s\nrootdir = %s\n", p_ti->source, rootdir);

  if (p_ti && !strcmp(p_ti->source, rootdir)) {
    log_debug ("Dir download cancelled %s\n", p_ti->source);
    //p_ti->worked = p_ti->size = 1;
    //p_ti->state = TR_COMPLETED;
  }

  return (p_ti->result);
}

int
remove_mirror_file (SMirrorFile *mf)
{
  int rc;
    
#ifdef __linux____DONT_USE
  inotify_rm_watch (globals.inotifyFd, mf->wd);
#else
  close(mf->wd);
  mf->wd = -1;
#endif
    
  rc = unlink (mf->localFile);

  // Try to remove direcotry if empty
  rmdir (mf->localDir);

  return rc;
}

int
sftp_panel_mirror_file_clear (struct SSH_Node *p_ssh_node, int flagRemoveAll)
{
  int i, nDel = 0;

  if (mirrorFiles == NULL)
    return 0;

  for (i=0; i<mirrorFiles->len; i++) {
    SMirrorFile *mf = &g_array_index (mirrorFiles, SMirrorFile, i);

    if (flagRemoveAll || (mf->sshNode == p_ssh_node)) {
      if (remove_mirror_file (mf) == 0)
        nDel ++;
    }
  }
  
  return nDel;
}

void
sftp_panel_mirror_dump ()
{
  int i;

  for (i=0; mirrorFiles && i<mirrorFiles->len; i++) {
    SMirrorFile *mf = &g_array_index (mirrorFiles, SMirrorFile, i);
    printf ("%s -> %s\n", mf->localFile, mf->remoteFile);
  }
}

SMirrorFile *
sftp_panel_get_mirror_file_by_wd (int wd)
{
  int i;
    
  for (i=0; i<mirrorFiles->len; i++) {
    SMirrorFile *mf = &g_array_index (mirrorFiles, SMirrorFile, i);
    //log_debug("wd=%d name=%s\n", mf->wd, mf->localFile);

    if (mf->wd == wd) {
      return mf;
    }
  }

  return 0;
}

int
saveMirrorFile (SMirrorFile *mf)
{
  int rc = 0;
  
  log_write ("Saving %s\n", mf->localFile);
  //sftp_upload (mf->pSSH, mf->localFile, mf->remoteFile);
  struct TransferInfo ti;
  
  //log_debug ("Creating transfer window\n");
  
  memset (&ti, 0, sizeof (struct TransferInfo));
  //transfer_window_create (&ti);
  ti.state = TR_IN_PROGRESS;
  time (&ti.start_time);
  strcpy (ti.host, mf->sshNode->host);
  //strcpy (ti.status, "uploading...");
  
  strcpy (ti.source, mf->localFile);
  strcpy (ti.destination, mf->remoteFile);
  
  log_write ("Uploading to %s: %s\n", mf->sshNode->host, mf->localFile);

  rc = sftp_copy_file_upload (mf->sshNode->sftp, &ti);
  
  if (rc == 0) {
    log_write ("Uploaded %d bytes\n", ti.worked);
    mf->lastSaved = time(NULL);
  }
  else
    msgbox_error ("Unable to save file:\n%s", mf->localFile);
      
  //transfer_window_close ();
  
  return rc;
}

#define EVENT_SIZE    (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

void
sftp_panel_check_inotify ()
{

  int rc;
  SMirrorFile *mf;
  gboolean isModified = FALSE;

#ifdef __linux__DONT_USE
/*
  int length, i = 0;
  char buffer[EVENT_BUF_LEN];
  struct timeval timeout;

  FD_ZERO (&globals.rfds);
  FD_SET (globals.inotifyFd, &globals.rfds);

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
 
  //log_write ("Checking...\n");
  rc = select (globals.inotifyFd + 1, &globals.rfds, NULL, NULL, &timeout);

  if (rc <= 0)
    return;

  if (FD_ISSET (globals.inotifyFd, &globals.rfds)) {
    //log_debug ("Reading from Inotify...\n");
    length = read (globals.inotifyFd, buffer, EVENT_BUF_LEN);

    //log_write ("length = %d\n", length);

    while (i < length) {
      struct inotify_event *event = (struct inotify_event *) &buffer[i];

      log_write ("Changed file descriptor: %d\n", event->wd);

      if (mf = sftp_panel_get_mirror_file_by_wd (event->wd)) {
        //log_debug ("event->mask = %d (IN_MODIFY=%d)\n", event->mask, IN_MODIFY);
        if (event->mask & IN_MODIFY) {
          if (saveMirrorFile (mf) != 0)
              msgbox_error ("Can't save remote file\n%s", mf->remoteFile);
        }
      }

      i += EVENT_SIZE + event->len;
    }
  }
*/
#else
  int i;
  struct timeval timeout;
    
  if (!mirrorFiles || mirrorFiles->len == 0)
    return;
/*
  // kevent returns error 35 Resource temporarily unavailable, so don't use it
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
    
  struct kevent triggeredEvent;
    
  i = kevent (globals.inotifyFd, NULL, 0, &triggeredEvent, 1, &timeout);
    
  if (i > 0) {
    log_debug ("Events detected: %d\n", i);
      
    if (triggeredEvent.udata != NULL) {
        // udata is non-null, so it's the name of the directory
        log_debug ("%d %d\n", triggeredEvent.flags, triggeredEvent.fflags);
        
        if (triggeredEvent.fflags == NOTE_WRITE) {
          mf = (SMirrorFile *) triggeredEvent.udata;
          log_debug ("Modified: %s\n", mf->localFile);
        }
    }
  }
*/
 struct stat attrib;
  
  for (i=0; i<mirrorFiles->len; i++) {
    mf = &g_array_index (mirrorFiles, SMirrorFile, i);
    rc = stat(mf->localFile, &attrib);
    
    if (rc == 0) {
      if (mf->lastSaved < attrib.st_mtime) {
          log_debug ("mf->lastSaved=%d attrib.st_mtime=%d\n", mf->lastSaved, attrib.st_mtime);

          if (saveMirrorFile (mf) != 0)
              msgbox_error ("Can't save remote file\n%s", mf->remoteFile);
      }
    }
    else {
      //log_write ("Can't stat %s: %s", mf->localFile, strerror(errno));
    }
  }
#endif

}

/**
 * sftp_panel_create_mirror_file()
 * Crate a local copy of a remote file and watch for updates
 */
int
sftp_panel_create_mirror_file(struct SSH_Info *pSSH, char *filename)
{
  int result;
  char mirrorDir[1024];
  char mirrorFile[2048];
  char command[2048];
  gboolean success;

  //int terminalPID = get_current_connection_tab ()->pid;

  // Create temporary dir
  sprintf (mirrorDir, "%s/%s/%s", prefs.tempDir, get_current_connection_tab ()->connection.host, pSSH->directory);

  log_debug ("Creating %s\n", mirrorDir);

  if (g_mkdir_with_parents (mirrorDir, 0777) < 0) {
    log_write("Can't create temporary dir %s\n", mirrorDir);
    return 1;
  }

  // Download file
/*  GSList *list = NULL;
  list = g_slist_append (list, filename);

  //if (transfer_sftp (SFTP_ACTION_DOWNLOAD, list, pSSH, mirrorDir) != 0) {
  if (sftp_queue_add (SFTP_ACTION_DOWNLOAD, list, pSSH, mirrorDir) != 0) {
    log_write("Can't download %s\n", filename);
    return 2;
  }
*/
  struct TransferInfo ti;

  memset (&ti, 0, sizeof (struct TransferInfo));
  ti.state = TR_IN_PROGRESS;
  time (&ti.start_time);
  strcpy (ti.host, pSSH->ssh_node->host);
  //strcpy (ti.status, "downloading...");

  sprintf (ti.source, "%s/%s", pSSH->directory, filename);
  sprintf (ti.destination, "%s/%s", mirrorDir, filename);
  
  log_write ("Downloading from %s: %s\n", pSSH->ssh_node->host, ti.source);
  
  result = sftp_copy_file_download (pSSH->ssh_node->sftp, &ti);

  // Add to watched file list
  if (mirrorFiles == NULL) {
    mirrorFiles = g_array_new(FALSE, TRUE, sizeof (SMirrorFile));
  }

  //SMirrorFile *pMirror;
  SMirrorFile pMirror;

  //pMirror = (SMirrorFile *) malloc (sizeof(SMirrorFile));

  pMirror.sshNode = pSSH->ssh_node;
  sprintf (pMirror.localDir, "%s", mirrorDir);
  sprintf (pMirror.localFile, "%s/%s", mirrorDir, filename);
  sprintf (pMirror.remoteFile, "%s/%s", pSSH->directory, filename);

#ifdef __linux__DONT_USE
/*
  pMirror.wd = inotify_add_watch(globals.inotifyFd, pMirror.localFile, IN_MODIFY|IN_CLOSE_WRITE|IN_CLOSE_NOWRITE);

  if (pMirror.wd == -1) {
    log_write("Can't add watch for %s\n", pMirror.localFile);
    return 4;
  }

  log_write("Added watch for %s using descriptor %d\n", pMirror.localFile, pMirror.wd);
#else
    pMirror.wd = open (pMirror.localFile, O_RDONLY);

    if (pMirror.wd <= 0) {
      log_write("Can't open %s\n", pMirror.localFile);
      return 4;
    }
    \*
    int flags;
    flags = fcntl(pMirror.wd, F_GETFL);
    fcntl(pMirror.wd, F_SETFL, flags | O_NONBLOCK); //opts & (~O_NONBLOCK)
    *\

\*
    struct kevent event; // Event we want to monitor
    
    // Initialize kevent structure
    EV_SET (&event, pMirror.wd, EVFILT_VNODE, EV_ADD | EV_CLEAR | EV_ENABLE, NOTE_WRITE, 0, (void *)&pMirror);
    //EV_SET (&event, pMirror.wd, EVFILT_READ, EV_ADD | EV_CLEAR | EV_ENABLE, NOTE_WRITE, 0, (void *)&pMirror);
    
    //Attach event to the kqueue.
    result = kevent(globals.inotifyFd, &event, 1, NULL, 0, NULL);
    // !!! returns error 35 Resource temporarily unavailable, so don't use it
    
    if (result <= 0)
      log_write ("Can't add event to queue: %d %s\n", errno, strerror(errno));
*\
*/
  
#endif
    
  //pMirror.lastSaved = time(NULL);

  struct stat attrib;

  int rc = stat (pMirror.localFile, &attrib);
    
  if (rc == 0)
    pMirror.lastSaved = attrib.st_mtime;
    
  g_array_append_val(mirrorFiles, pMirror);

  // Launch editor and open local file
#ifdef __APPLE__
  sprintf (command, "open -a \"%s\" \"%s\"", prefs.text_editor, pMirror.localFile);
#else
  sprintf (command, "\"%s\" \"%s\"", prefs.text_editor, pMirror.localFile);
#endif
    
  log_debug ("command = %s\n", command);
  
  int argc;
  gchar** argv;
  g_shell_parse_argv (command, &argc, &argv, NULL);
  
  GError *error = NULL;
  success = g_spawn_async (NULL, argv, NULL,
                           G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_SEARCH_PATH,
                           NULL, NULL, NULL, &error);
                          
  if (!success) {
    msgbox_error ("Can't open remote file %s:\n%s", filename, error->message);
    g_error_free (error);
  }

  g_strfreev (argv);

  return 0;
}

void
sftp_panel_open ()
{
  int create, rc, i;
  char folder_name[1024];
  gchar *filename, *uri, filepath[2048], command[2048];
  gboolean success;
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;
    
  log_debug ("Editor: %s\n", prefs.text_editor);
  
#ifndef __APPLE__
  if (!check_command (prefs.text_editor))
    {
      msgbox_error ("Can't find %s", prefs.text_editor);
      return;
    }
#endif

  log_debug ("Getting selected files...\n");

  GSList *filelist = sftp_panel_get_selected_files ();
    
  while (filelist)
    {
      filename = (gchar *) filelist->data;
      sprintf (filepath, "%s/%s", p_ssh_current->directory, filename);
      log_debug ("%s\n", filepath);

      //setuid (0);

      //char error[512];
      //int result = lt_mount(p_ssh_current, filepath, "/var/tmp", error);
      //int result = mount_sshfs(p_ssh_current, filepath, "/var/tmp", error);
      int result = sftp_panel_create_mirror_file(p_ssh_current, filename);

      if (result != 0) {
        //log_write ("%d: %s\n", result, error);
        msgbox_error ("Can't create temporary file for:\n%s", filepath);
      }
      
      filelist = g_slist_next (filelist);
    }
}

/**
 * sftp_panel_create_folder() - Create a new directory on the server
 */
void
sftp_panel_create_folder ()
{
  int create, rc;
  char folder_name[1024];
  char folder_abs_path[2048];
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;
    
  create = query_value (_("Create folder"), _("Enter a name for the new folder"), "", folder_name, QUERY_FOLDER_NEW);
  
  if (create && folder_name[0])
    {
      sprintf (folder_abs_path, "%s/%s", p_ssh_current->directory, folder_name);

      ////////////////////////////////
      lockSSH (__func__, TRUE);
     
      log_write ("Creating folder %s\n", folder_abs_path);
      
      rc = sftp_mkdir (p_ssh_current->ssh_node->sftp, folder_abs_path, S_IRWXU);

      lockSSH (__func__, FALSE);
      ////////////////////////////////
 
      if (rc != SSH_OK)
        {
          if (sftp_get_error (p_ssh_current->ssh_node->sftp) == SSH_FX_FILE_ALREADY_EXISTS)
            msgbox_error ("Directory %s already existing", folder_abs_path);
          else
            msgbox_error ("%s", ssh_get_error (p_ssh_current->ssh_node->session));
        }
      else
        {
          sftp_refresh_directory_list (p_ssh_current);
          refresh_sftp_panel (p_ssh_current);
        }
    }
}

/**
 * sftp_panel_create_file() - Create a new file on the server
 */
void
sftp_panel_create_file ()
{
  int access_type = O_WRONLY | O_CREAT | O_TRUNC;
  int create, rc = 0;
  char file_name[1024];
  char file_abs_path[2048];
  sftp_file file;
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;
    
  create = query_value (_("Create file"), _("Enter a name for the new file"), "", file_name, QUERY_FILE_NEW);
  
  if (create && file_name[0])
    {
      sprintf (file_abs_path, "%s/%s", p_ssh_current->directory, file_name);

      ////////////////////////////////
      lockSSH (__func__, TRUE);
     
      log_write ("Creating file %s\n", file_abs_path);
      
      file = sftp_open (p_ssh_current->ssh_node->sftp, file_abs_path, access_type, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

      lockSSH (__func__, FALSE);
      ////////////////////////////////
 
      if (file == NULL)
        {
          msgbox_error ("Can't create file:\n%s", file_abs_path);
        }
      else
        {
          sftp_refresh_directory_list (p_ssh_current);
          refresh_sftp_panel (p_ssh_current);
        }
    }
}
/**
 * sftp_panel_rename() - Rename file or directory
 */
void
sftp_panel_rename ()
{
  struct Directory_Entry *e;
  int create, rc;
  char folder_name[1024];
  char *filename;
  char filename_new[1024];
  char file_abs_path_old[2048];
  char file_abs_path_new[2048];
  int n_worked = 0;
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  GSList *filelist = sftp_panel_get_selected_files ();
    
  while (filelist)
    {
      filename = (gchar *) filelist->data;
      
      if (e = dl_search_by_name (&p_ssh_current->dirlist, filename))
        {
          sprintf (file_abs_path_old, "%s/%s", p_ssh_current->directory, e->name);
          
          if (!query_value (_("Rename"), _("Enter new name"), e->name, filename_new, QUERY_RENAME))
            goto l_ren_nex_file;
            
          if (!filename_new[0])
            goto l_ren_nex_file;

          sprintf (file_abs_path_new, "%s/%s", p_ssh_current->directory, filename_new);

          ////////////////////////////////
          lockSSH (__func__, TRUE);

          log_write ("renaming %s to %s\n", file_abs_path_old, file_abs_path_new);
          
          rc = sftp_rename (p_ssh_current->ssh_node->sftp, file_abs_path_old, file_abs_path_new);

          lockSSH (__func__, FALSE);
          ////////////////////////////////

          if (rc != SSH_OK)
            msgbox_error ("Can't rename %s:\n%s", e->name, ssh_get_error (p_ssh_current->ssh_node->session));

          n_worked ++;
        }

l_ren_nex_file:
      filelist = g_slist_next (filelist);
    }
    
  if (n_worked) {
    sftp_refresh_directory_list (p_ssh_current);
    refresh_sftp_panel (p_ssh_current);
  }
}

void
file_renamed_callback (GtkCellRendererText *cell,
                      gchar *path_string,
                      gchar *new_text,
                      gpointer user_data)
{
  //GtkTreeModel *model = (GtkTreeModel *)user_data;
  GtkTreeModel *model = tree_model_fs;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;
  gchar *old_text;
  char file_abs_path_old[2048];
  char file_abs_path_new[2048];
  int rc;
  
  if (new_text[0] == 0)
    return;
  
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, COLUMN_FILE_NAME, &old_text, -1);
  
  log_debug ("old name=%s new name=%s\n", old_text, new_text);
  
  sprintf (file_abs_path_old, "%s/%s", p_ssh_current->directory, old_text);
  sprintf (file_abs_path_new, "%s/%s", p_ssh_current->directory, new_text);
    
  log_write ("renaming %s to %s\n", file_abs_path_old, file_abs_path_new);
  
  rc = sftp_rename (p_ssh_current->ssh_node->sftp, file_abs_path_old, file_abs_path_new);

  if (rc != SSH_OK)
    msgbox_error ("Can't rename %s:\n%s", old_text, ssh_get_error (p_ssh_current->ssh_node->session));
  
  sftp_refresh_directory_list (p_ssh_current);
  refresh_sftp_panel (p_ssh_current);
}

/**
 * sftp_panel_delete() - Delete file or directory
 */
void
sftp_panel_delete ()
{
  struct Directory_Entry *e;
  int create, rc;
  char folder_name[1024];
  char *filename;
  char file_abs_path[2048];
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  GSList *filelist = sftp_panel_get_selected_files ();
  
  if (msgbox_yes_no ("Delete selected files?") != GTK_RESPONSE_YES)
    return;
    
  sftp_begin ();

  while (filelist && !sftp_stoped_by_user ())
    {
      filename = (gchar *) filelist->data;
      
      if (e = dl_search_by_name (&p_ssh_current->dirlist, filename))
        {
          sprintf (file_abs_path, "%s/%s", p_ssh_current->directory, e->name);

          ////////////////////////////////
          lockSSH (__func__, TRUE);

          //log_write ("deleting %s on %s\n", file_abs_path, p_ssh_current->ssh_node->host);
          sftp_set_status ("Deleting %s...", e->name);
          
          if (is_directory (e))
            {
              rc = sftp_rmdir (p_ssh_current->ssh_node->sftp, file_abs_path);
            }
          else
            {
              rc = sftp_unlink (p_ssh_current->ssh_node->sftp, file_abs_path);
            }

          lockSSH (__func__, FALSE);
          ////////////////////////////////
       
          if (rc != SSH_OK)
            msgbox_error ("Can't delete %s:\n%s", e->name, ssh_get_error (p_ssh_current->ssh_node->session));
        }

      filelist = g_slist_next (filelist);
    }

  sftp_end ();
    
  sftp_refresh_directory_list (p_ssh_current);
  refresh_sftp_panel (p_ssh_current);
}


/**
 * sftp_panel_change_time() - Change the last modification and access time of a file 
 */
void
sftp_panel_change_time ()
{
  GtkWidget *dialog;
  GtkBuilder *builder;
  GError *error=NULL;
  char ui[256];
  int result, rc = 0, i;
  struct Directory_Entry *e;
  char folder_name[1024];
  char *filename;
  char file_abs_path[2048];
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/time.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return;
    }

  GtkWidget *vbox_main = GTK_WIDGET (gtk_builder_get_object (builder, "vbox_main"));

  /* Create dialog */

  dialog = gtk_dialog_new_with_buttons
                 (_("Change time"), NULL,
                  GTK_DIALOG_MODAL, 
                  "_Cancel", GTK_RESPONSE_CANCEL,
                  "_Ok", GTK_RESPONSE_OK,
                  NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  //gtk_entry_set_activates_default (GTK_ENTRY (entry_expr), TRUE);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (GTK_DIALOG (dialog)), GTK_WINDOW (main_window));

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox_main);
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  GtkWidget *calendarDate = GTK_WIDGET (gtk_builder_get_object (builder, "calendar_date"));
  GtkWidget *spin_h = GTK_WIDGET (gtk_builder_get_object (builder, "spin_h"));
  GtkWidget *spin_m = GTK_WIDGET (gtk_builder_get_object (builder, "spin_m"));
  GtkWidget *spin_s = GTK_WIDGET (gtk_builder_get_object (builder, "spin_s"));

  // Set date
  struct tm dateTime;
  //time_t today = time(NULL); // Today
  //localtime_r(&today, &dateTime); // Gives the correct tm_isdst parameter

  GSList *filelist = sftp_panel_get_selected_files ();
  filename = (gchar *) filelist->data;

  e = dl_search_by_name (&p_ssh_current->dirlist, filename);
  
  localtime_r(&e->mtime, &dateTime);

  gtk_calendar_select_month (GTK_CALENDAR(calendarDate), dateTime.tm_mon, dateTime.tm_year + 1900);
  gtk_calendar_select_day (GTK_CALENDAR(calendarDate), dateTime.tm_mday);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_h), dateTime.tm_hour);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_m), dateTime.tm_min);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_s), dateTime.tm_sec);
  
run_dialog:

  result = gtk_dialog_run (GTK_DIALOG (dialog));

  // Get user date and time
  int day, month, year;

  gtk_calendar_get_date (GTK_CALENDAR(calendarDate), &year, &month, &day);
  int h = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin_h));
  int m = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin_m));
  int s = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin_s));

  // Convert to time_t type
  dateTime.tm_sec = s; //tm_sec   seconds [0,61]
  dateTime.tm_min = m; //tm_min   minutes [0,59]
  dateTime.tm_hour = h; //tm_hour  hour [0,23]
  dateTime.tm_mday = day; //tm_mday  day of month [1,31]
  dateTime.tm_mon = month; //tm_mon   month of year [0,11]
  dateTime.tm_year = year-1900; //tm_year  years since 1900
  //  0, //int    tm_wday  day of week [0,6] (Sunday = 0)
  //  0, //int    tm_yday  day of year [0,365]
  //dateTime.tm_isdst =     1; //int    tm_isdst daylight savings flag

  time_t totalSeconds = mktime(&dateTime);

  //log_debug ("totalSeconds = %d\n", totalSeconds);
  //log_debug ("today        = %d\n", today);

  // Convert to timeval structure (atime and mtime)
  struct timeval newFileTime[2];

  newFileTime[0].tv_sec = totalSeconds;
  newFileTime[0].tv_usec = 0;
  newFileTime[1].tv_sec = totalSeconds;
  newFileTime[1].tv_usec = 0;

  gtk_widget_destroy (dialog);
  g_object_unref (G_OBJECT (builder));

  if (result == GTK_RESPONSE_OK) {

    GSList *filelist = sftp_panel_get_selected_files ();

    while (filelist && !sftp_stoped_by_user ()) {
      filename = (gchar *) filelist->data;
      
      if (e = dl_search_by_name (&p_ssh_current->dirlist, filename))
        {
          //if (!is_directory (e)) {
            //rc = sftp_unlink (p_ssh_current->ssh_node->sftp, file_abs_path);
           
            sprintf (file_abs_path, "%s/%s", p_ssh_current->directory, e->name);
            
            //log_write ("deleting %s on %s\n", file_abs_path, p_ssh_current->ssh_node->host);
            sftp_set_status ("Changing time for %s to %d-%02d-%02d %02d:%02d:%02d...", e->name, year, month+1, day, h, m, s);

            ////////////////////////////////
            lockSSH (__func__, TRUE);

            rc = sftp_utimes (p_ssh_current->ssh_node->sftp, file_abs_path, &newFileTime);

            lockSSH (__func__, FALSE);
            ////////////////////////////////
      
            if (rc != SSH_OK)
              msgbox_error ("Can't change time for %s:\n%s", e->name, ssh_get_error (p_ssh_current->ssh_node->session));
          //}
        }

      filelist = g_slist_next (filelist);
    }

    sftp_refresh_directory_list (p_ssh_current);
    refresh_sftp_panel (p_ssh_current);
  }
}

/**
 * sftp_panel_copy_name_terminal() - Copy selected file names to terminal
 */
/*
void sftp_panel_copy_name_terminal ()
{
  char *filename;
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  GSList *filelist = sftp_panel_get_selected_files ();
  
  while (filelist)
    {
      filename = (gchar *) filelist->data;
      
      terminal_write (filename);
      terminal_write (" ");

      filelist = g_slist_next (filelist);
    }
}
*/
/**
 * sftp_panel_copy_name_terminal() - Copy selected file names to terminal
 */
void sftp_panel_copy_path_terminal ()
{
  char *filename, path[1024];
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  GSList *filelist = sftp_panel_get_selected_files ();
  
  while (filelist)
    {
      filename = (gchar *) filelist->data;
      sprintf (path, "%s/%s", p_ssh_current->directory, filename);
      
      terminal_write_child (path);
      terminal_write_child (" ");

      filelist = g_slist_next (filelist);
    }
}

/**
 * sftp_panel_copy_path_clipboard() - Copy selected file names to clipboard
 */
void sftp_panel_copy_path_clipboard ()
{
  char *filename, currentPath[1024];
  char *buffer;
  int bufferSize, n = 0;
  const int extent = 512; // Initial extent
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  buffer = (char *) malloc (extent);
  //memset (buffer, 0, extent);
  strcpy (buffer, "");
  bufferSize = extent;

  GtkClipboard* clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  GSList *filelist = sftp_panel_get_selected_files ();
  
  while (filelist)
    {
      filename = (gchar *) filelist->data;
      sprintf (currentPath, "%s/%s", p_ssh_current->directory, filename);

      //log_debug ("strlen (buffer) = %d, strlen (currentPath) = %d, bufferSize = %d\n", strlen (buffer), strlen (currentPath), bufferSize);
      
      if (strlen (buffer) + strlen (currentPath) > bufferSize) {
        bufferSize += strlen (currentPath) + 1;
        buffer = (char *) realloc (buffer, bufferSize);
      }

      if (n > 0)
        strcat (buffer, "\n");

      strcat (buffer, currentPath);

      n ++;
      filelist = g_slist_next (filelist);
    }

  gtk_clipboard_set_text (clipboard, buffer, -1);
  gtk_clipboard_store (clipboard);

  free (buffer);
}

/**
 * sftp_panel_order_invert()
 */
/*void
sftp_panel_order_invert ()
{
}*/

void
refresh_panel_history ()
{
  struct Connection *c;
  struct Bookmark *b;
  int i, n_items;
  gchar *s;
  
  /* empty list */
  
  //n_items = count_bookmarks (&c->history);
  
#if (GTK_MAJOR_VERSION == 2)
  for (i=0; i<MAX_BOOKMARKS; i++)
    gtk_combo_box_remove_text (GTK_COMBO_BOX (sftp_panel.combo_position), 0);
#else
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (sftp_panel.combo_position));
#endif
  
  /* recreate list */
  
  if (get_current_connection_tab () == NULL)
    return;
    
  if ((c = cl_get_by_name (&conn_list, get_current_connection_tab ()->connection.name)) == NULL)
    return;

  /* add bookmarks in reverse order */
/*
  b = c->history.head;

  while (b)
    {
#if (GTK_MAJOR_VERSION == 2)
      gtk_combo_box_prepend_text (GTK_COMBO_BOX (sftp_panel.combo_position), b->item);
#else
      gtk_combo_box_text_prepend_text (GTK_COMBO_BOX_TEXT(sftp_panel.combo_position), b->item);
#endif

      b = b->next;
    }
*/ 

  if (c->directories) {
    gchar *dir;

    for (i=0; i<c->directories->len; i++) {
      dir = (gchar *) g_ptr_array_index (c->directories, i);

  #if (GTK_MAJOR_VERSION == 2)
      gtk_combo_box_prepend_text (GTK_COMBO_BOX (sftp_panel.combo_position), dir);
  #else
      gtk_combo_box_text_prepend_text (GTK_COMBO_BOX_TEXT(sftp_panel.combo_position), dir);
  #endif
    }
  }
}
/*
void
sftp_connect_cb (GtkButton *button, gpointer user_data)
{
  int rc;
  struct ConnectionTab *p_conn_tab;
  struct SSH_Auth_Data auth;
  
  if ((p_conn_tab = get_current_connection_tab ()) == NULL)
    return;
    
  if (lt_ssh_is_connected (&p_conn_tab->ssh_info))
    return;
\*
  lt_ssh_init (&p_conn_tab->ssh_info);

  strcpy (auth.user, p_conn_tab->connection.user);
  strcpy (auth.password, p_conn_tab->connection.password);
  strcpy (auth.host, p_conn_tab->connection.host);
  auth.port = 22;

  log_write ("ssh connecting to %s@%s:%d\n", auth.user, auth.host, auth.port);

  if (lt_ssh_connect (&p_conn_tab->ssh_info, &globals.ssh_list, &auth) != 0)
    {
      msgbox_error (p_conn_tab->ssh_info.error_s);
      return;
    }
  
  if (lt_sftp_create (&p_conn_tab->ssh_info) != 0)
    {
      msgbox_error (p_conn_tab->ssh_info.error_s);
      return;
    }
    
  log_write ("connected\n");
  
  if (sftp_refresh_directory_list (p_ssh_current) != 0)
    msgbox_error ("can't read directory\n%s", p_ssh_current->directory);
    
  refresh_sftp_panel (&p_conn_tab->ssh_info);
*\
}
*/
void
sftp_panel_change_directory (char *path)
{
  int rc;
  char dirbackup[2048];
  
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  if (path == NULL)
    return;
/* 
  trim (path);

  if (strcmp (path, "/") != 0)
    {
      if (path[strlen (path)-1] == '/')
        path[strlen (path)-1] = 0;
    }
*/  
  sftp_normalize_directory (p_ssh_current, path);

  //log_debug ("path='%s'\n", path);
  //log_debug ("p_ssh_current->directory='%s'\n", p_ssh_current->directory);

  if (!strcmp (path, p_ssh_current->directory))
    return;
    
  log_debug ("Moving to '%s'\n", path);
  
  strcpy (dirbackup, p_ssh_current->directory);
  strcpy (p_ssh_current->directory, path);

  if (sftp_refresh_directory_list (p_ssh_current) != 0)
    {
      msgbox_error ("Can't access %s", p_ssh_current->directory);
      sftp_set_status ("Can't access %s", p_ssh_current->directory);
      strcpy (p_ssh_current->directory, dirbackup);
    }
  else
    { 
      struct Connection *c;
      
      log_debug ("Connection: %s\n", get_current_connection_tab ()->connection.name);

      c = cl_get_by_name (&conn_list, get_current_connection_tab ()->connection.name);

      if (c) {
        //add_bookmark (&(c->history), path); // deprecated
        add_directory (c, path);
      }
      else {
        log_debug ("Not found: %s\n", get_current_connection_tab ()->connection.name);
      }
        
      log_debug ("Current tab is '%s'\n", c->name);

      refresh_panel_history ();
    }
    
  //refresh_panel_history ();
  
  refresh_sftp_panel (p_ssh_current);
}

/* row_activated_sftp_cb() - callback function when a row has been double-clicked in the sftp panel */
void
row_activated_sftp_cb (GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
  struct Directory_Entry *e;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gboolean have_iter;
  int retcode;
  char position[1024];
  
  model = gtk_tree_view_get_model(tree_view);
 
  if (gtk_tree_model_get_iter (model, &iter, path))
    {
      gchar *name;
      gtk_tree_model_get (model, &iter, COLUMN_FILE_NAME, &name, -1);

      log_debug ("Double-clicked row contains name %s\n", name);

      if (e = dl_search_by_name (&p_ssh_current->dirlist, name))
        {
          if (is_directory (e))
            {
              sprintf (position, "%s/%s", p_ssh_current->directory, name);
              sftp_panel_change_directory (position);
            }
          else
            {
            }
        }

      g_free(name);
    }
}

void
sftp_go_home_cb (GtkButton *button, gpointer user_data)
{
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  sftp_panel_change_directory (p_ssh_current->home);
}

void
sftp_go_up_cb (GtkButton *button, gpointer user_data)
{
  char newpath[1024], *pc;

  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  strcpy (newpath, p_ssh_current->directory);
  
  if (pc = (char *) strrchr (newpath, '/'))
    {
      *pc = 0;
      if (newpath[0] == 0)
        strcpy (newpath, "/");
    }
  
  sftp_panel_change_directory (newpath);
}

void
sftp_begin ()
{
  sftp_panel.stop = FALSE;
  gtk_widget_show (sftp_panel.button_stop);
}

void
sftp_end ()
{
  gtk_widget_hide (sftp_panel.button_stop);
}

void
sftp_stop_cb (GtkButton *button, gpointer user_data)
{
  log_write ("SFTP process stopped by user.\n");

  sftp_panel.stop = TRUE;
}

gboolean
sftp_stoped_by_user ()
{
  return sftp_panel.stop;
}

int g_toggle_follow_lock = 0;

void
follow_terminal_folder (gboolean active)
{
  char *path;
  gboolean flag;
  
  if (g_toggle_follow_lock)
    return;
  
  g_toggle_follow_lock = 1;
  
  if (p_ssh_current)
    {
      if (lt_ssh_is_connected (p_ssh_current))
        {
          flag = active;
        }
      else
        flag = FALSE;
        
      p_ssh_current->follow_terminal_folder = (int) flag;
    }
  else
    flag = FALSE;
  
  log_debug ("active = %d\n", active);
  log_debug ("flag = %d\n", flag);
  
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(sftp_panel.check_follow), flag);
  p_ssh_current->follow_terminal_folder = (int) flag;
  
  if (flag)
    {
      if (path = get_remote_directory ())
        {
          sftp_panel_change_directory (path);
        }
    }
    
  g_toggle_follow_lock = 0;
}

void
toggle_follow_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
  if (g_toggle_follow_lock)
    return;
  
  log_debug ("toggled\n");
  
  if (!lt_ssh_is_connected (p_ssh_current))
    {
      follow_terminal_folder (FALSE);
    }
  else
    {
      //log_debug ("%s\n", get_remote_directory ());
      if (get_remote_directory ())
        follow_terminal_folder (p_ssh_current->follow_terminal_folder == 0 ? TRUE : FALSE);
      else
        {
          follow_terminal_folder (FALSE);
          msgbox_info ("Sorry, can't detect working directory\non this server.");
        }
    }
}

void
sftp_refresh_cb (GtkButton *button, gpointer user_data)
{
  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  if (sftp_refresh_directory_list (p_ssh_current) != 0)
    msgbox_error ("can't read directory\n%s", p_ssh_current->directory);
    
  refresh_sftp_panel (p_ssh_current);
}

void
sftp_go_cb (GtkButton *button, gpointer user_data)
{
  char *path;

  if (!lt_ssh_is_connected (p_ssh_current))
    return;

  path = (char *) gtk_entry_get_text (GTK_ENTRY (sftp_panel.entry_sftp_position));
  sftp_panel_change_directory (path);
}

void
sftp_panel_show_filter (gboolean show)
{
  gboolean flag;
  int enabling_just_now = 0;
  
  if (p_ssh_current && lt_ssh_is_connected (p_ssh_current))
    {
      if (p_ssh_current->ssh_node->sftp)
        {
          flag = show;
          enabling_just_now = !p_ssh_current->filter;
        }
      else
        flag = FALSE;
        
      p_ssh_current->filter = (int) flag;
    }
  else
    flag = FALSE;
    
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(sftp_panel.toggle_filter), flag);
  
  if (flag)
    {
      if (p_ssh_current->match_string[0] && enabling_just_now)
        gtk_entry_set_text (GTK_ENTRY (sftp_panel.entry_sftp_filter), p_ssh_current->match_string);
        
      gtk_widget_show_all (sftp_panel.entry_sftp_filter);
      
      if (enabling_just_now)
        gtk_widget_grab_focus (sftp_panel.entry_sftp_filter);
    }
  else
    {
      gtk_widget_hide (sftp_panel.entry_sftp_filter);
    }
}

void
toggle_filter_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
  gboolean active = gtk_toggle_button_get_active (togglebutton);
  
  sftp_panel_show_filter (active);
  refresh_sftp_panel (p_ssh_current);
}

void 
entry_filter_changed_cb (GtkWidget *widget, gpointer data) 
{
  if (p_ssh_current)
    {
      sprintf (p_ssh_current->match_string, "%s", gtk_entry_get_text (GTK_ENTRY (widget)));
      
      log_debug ("refresh panel\n");
  
      //refresh_sftp_panel (p_ssh_current);
      ifr_add (ITERATION_REFRESH_SFTP_PANEL, NULL);
    }
}

int
sort_name_compare_func (GtkTreeModel *model, 
                        GtkTreeIter *a,
                        GtkTreeIter *b,
                        gpointer userdata)
{
  int sortcol = GPOINTER_TO_INT(userdata);
  struct Directory_Entry *e1, *e2;
  int ret = 0;
  gchar *name1, *name2;
  int size1, size2;

  gtk_tree_model_get (model, a, COLUMN_FILE_NAME, &name1, -1);
  gtk_tree_model_get (model, b, COLUMN_FILE_NAME, &name2, -1);

  switch (sortcol) {
    case SORTID_ICON:
    case SORTID_NAME:
      
      ret = g_utf8_collate (name1, name2);
      break;
      
    case SORTID_SIZE:
      e1 = dl_search_by_name (&p_ssh_current->dirlist, name1);
      e2 = dl_search_by_name (&p_ssh_current->dirlist, name2);
      
      if (e1 && e2)
        {
          if (e1->size != e2->size)
            {
              ret = (e1->size > e2->size) ? 1 : -1;
            }
        }
      else
        ret = 0;
        
      break;
      
    case SORTID_DATE:
      e1 = dl_search_by_name (&p_ssh_current->dirlist, name1);
      e2 = dl_search_by_name (&p_ssh_current->dirlist, name2);
      
      if (e1 && e2)
        {
          if (e1->mtime != e2->mtime)
            {
              ret = (e1->mtime > e2->mtime) ? 1 : -1;
            }
        }
      else
        ret = 0;
        
      break;
      
    default:
      ret = 0;
  }
  
  g_free (name1);
  g_free (name2);
  
  return ret;
}

/**
 * sftp_cell_tooltip_cb() - Shows a tooltip whenever the user puts the mouse over a row
 */
gboolean
sftp_cell_tooltip_cb (GtkWidget *widget, gint x, gint y,
                      gboolean keyboard_tip, GtkTooltip *tooltip, gpointer user_data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeViewColumn *column;
  GtkTreePath *path;
  struct Directory_Entry *e;
  char text[1024], tmp_s[32], perms[32];
  gchar *name;

  if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget), &x, &y, keyboard_tip, &model, 0, &iter))
    {
      return FALSE;
    }

  if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), x, y, 0, &column, 0, 0))
    {
      return FALSE;
    }
    
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

  if (gtk_tree_model_get_iter (model, &iter, path))
    {
      gtk_tree_model_get (model, &iter, COLUMN_FILE_NAME, &name, -1);

      if (e = dl_search_by_name (&p_ssh_current->dirlist, name))
        {
          sprintf (text, _("<b>File:</b> %s\n"
                           "<b>Owner:</b> %s\n"
                           "<b>Group:</b> %s\n"
                           "<b>Permissions:</b> %s\n"
                           //"<b>Size:</b> %llu bytes\n"
                           "<b>Size:</b> %s\n"
                           "<b>Modified:</b> %s"),
                   g_markup_escape_text (e->name, strlen (e->name)), e->owner, e->group, 
                   permissions_octal_to_string (e->permissions, perms),
                   bytes_to_human_readable (e->size, tmp_s), 
                   timestamp_to_date (DATE_FORMAT, e->mtime));
                   
          gtk_tooltip_set_markup (tooltip, text);
        }

      g_free (name);
      return TRUE;
    }

  return FALSE;
}

void
sftp_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  GtkUIManager *ui_manager;
  GtkActionGroup *action_group;
  GtkWidget *popup;

  log_debug ("\n");

  action_group = gtk_action_group_new ("SFTPPopupActions");
  gtk_action_group_add_actions (action_group, sftp_popup_menu_items, G_N_ELEMENTS (sftp_popup_menu_items), treeview);

  ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
  gtk_ui_manager_insert_action_group (ui_manager, action_group_ssh_adds, 0);

  gtk_ui_manager_add_ui_from_string (ui_manager, ui_sftp_popup_desc, -1, NULL);
  gtk_ui_manager_add_ui_from_string (ui_manager, ui_sftp_popup_desc_adds, -1, NULL);

  popup = gtk_ui_manager_get_widget (ui_manager, "/SFTPPopupMenu");

  /* Note: event can be NULL here when called from view_onPopupMenu;
   *  gdk_event_get_time() accepts a NULL argument */
  gtk_menu_popup (GTK_MENU(popup), NULL, NULL, NULL, NULL,
                 (event != NULL) ? event->button : 0,
                 gdk_event_get_time((GdkEvent*)event));
}

gboolean
sftp_onButtonPressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  GtkTreeSelection *selection;

  log_debug ("\n");

  /* single click with the right mouse button? */
  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
    {
      log_debug ("Single right click on the tree view.\n");

      /* select row if no row is selected */

      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(treeview));

      if (sftp_panel_count_selected_rows ()  <= 1)
        {
           GtkTreePath *path;

           /* Get tree path for row that was clicked */
            if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(treeview),  (gint) event->x, (gint) event->y, &path, NULL, NULL, NULL))
              {
                gtk_tree_selection_unselect_all (selection);
                gtk_tree_selection_select_path (selection, path);
                gtk_tree_path_free (path);
              }
            else
              {
                return FALSE;
              }
        }

      sftp_popup_menu (treeview, event, userdata);

      log_debug ("handled\n");
      return TRUE; /* we handled this */
    }

  log_debug ("not handled\n");
  return FALSE; /* we did not handle this */
}

gboolean
sftp_onPopupMenu (GtkWidget *treeview, gpointer userdata)
{
  log_debug ("\n");
  
  sftp_popup_menu (treeview, NULL, userdata);

  return TRUE; /* we handled this */
}

static void
combo_change_position_cb (GtkWidget *entry, gpointer user_data)
{
  log_debug ("position_selected_tearoff=%d\n", (int) sftp_panel.position_selected_tearoff);
  
  if (sftp_panel.position_selected_tearoff == FALSE)
    return;
  
#if (GTK_MAJOR_VERSION == 2)
  sftp_panel_change_directory ((char *) gtk_combo_box_get_active_text (GTK_COMBO_BOX (entry)));
#else
  sftp_panel_change_directory ((char *) gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (entry)));
#endif
  
  sftp_panel.position_selected_tearoff = FALSE;
}

void
ssh_additionals_cb (GtkAction *action, gpointer user_data)
{
  int id, rc;
  char *expanded, *filename;
  char output[4096], error[1024];

  gchar *name = (gchar *) gtk_action_get_name (GTK_ACTION (action));
  
  sscanf (name, "add-item-%d", &id);
  
  GSList *filelist = sftp_panel_get_selected_files ();
  
  while (filelist)
    {
      filename = (gchar *) filelist->data;
      expanded = replace_str (SSHMenuItems[id].command, "@{f}", filename);
      expanded = replace_str (expanded, "@{d}", p_ssh_current->directory);
      
      log_debug ("%s\n", expanded);
      
      switch (SSHMenuItems[id].action) {
      
        case ACTION_PASTE:
          terminal_write_child (expanded);
          break;
          
        case ACTION_EXECUTE:
          //log_debug ("Executing on %s: %s\n", p_ssh_current->ssh_node->host, expanded);
          sftp_set_status ("Executing on %s: %s", p_ssh_current->ssh_node->host, expanded);
          
          rc = lt_ssh_exec (p_ssh_current, expanded, output, sizeof (output), error, sizeof (error));
          
          sftp_clear_status ();
          
          if (rc)
            {
              msgbox_error ("Unable to execute command.");
              break;
            }
          
          if (error[0])
            {
              msgbox_error (g_strconcat ("Execution error:\n", error, NULL));
              break;
            }

//log_debug ("output=\n%s\n", output);         
          if (SSHMenuItems[id].flags & OUTPUT)
            show_output ("Output", output);
            
          break;
          
        default:
          break;
      }

      if (SSHMenuItems[id].flags & ENTER) 
        terminal_write_child ("\n");

      if ((SSHMenuItems[id].flags & MULTIPLE) == 0) 
        break;
        
      filelist = g_slist_next (filelist);
    }
        
  if (SSHMenuItems[id].flags & REFRESH)
    {
      sftp_refresh_directory_list (p_ssh_current);
      refresh_sftp_panel (p_ssh_current);
    }
}

void
position_typing_cb (GtkEditable *editable,
               gchar       *new_text,
               gint         new_text_length,
               gpointer     position,
               gpointer     user_data)
{
  //log_debug ("new_text_length=%d\n", new_text_length);
  
  if (new_text_length < 0)
    sftp_panel.position_selected_tearoff = TRUE;
}

GtkWidget *
create_sftp_panel ()
{
  GtkBuilder *builder;
  GError *error = NULL;
  char ui[256], filename[256];
  int i;

  builder = gtk_builder_new ();
  
  sprintf (ui, "%s/sftp.glade", globals.data_dir);
  
#if (GTK_MAJOR_VERSION == 2)
  strcat (ui, ".gtk2");
#endif

  if (gtk_builder_add_from_file (builder, ui, &error) == 0)
    {
      msgbox_error ("Can't load user interface file:\n%s", error->message);
      return (NULL);
    }
    
  memset (&sftp_panel, 0, sizeof (struct SFTP_Panel));

  /* Buttons */

  sftp_panel.button_home = GTK_WIDGET (gtk_builder_get_object (builder, "button_home"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_home), gtk_image_new_from_stock (MY_STOCK_HOME, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_home), "clicked", G_CALLBACK (sftp_go_home_cb), NULL);

  sftp_panel.button_go_up = GTK_WIDGET (gtk_builder_get_object (builder, "button_go_up"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_go_up), gtk_image_new_from_stock (MY_STOCK_FOLDER_UP, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_go_up), "clicked", G_CALLBACK (sftp_go_up_cb), NULL);

  sftp_panel.button_file_new = GTK_WIDGET (gtk_builder_get_object (builder, "button_file_new"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_file_new), gtk_image_new_from_stock (MY_STOCK_FILE_NEW, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_file_new), "clicked", G_CALLBACK (sftp_panel_create_file), NULL);

  sftp_panel.button_folder_new = GTK_WIDGET (gtk_builder_get_object (builder, "button_folder_new"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_folder_new), gtk_image_new_from_stock (MY_STOCK_FOLDER_NEW, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_folder_new), "clicked", G_CALLBACK (sftp_panel_create_folder), NULL);

  sftp_panel.button_upload = GTK_WIDGET (gtk_builder_get_object (builder, "button_upload"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_upload), gtk_image_new_from_stock (MY_STOCK_UPLOAD, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_upload), "clicked", G_CALLBACK (sftp_upload_files), NULL);

  sftp_panel.button_download = GTK_WIDGET (gtk_builder_get_object (builder, "button_download"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_download), gtk_image_new_from_stock (MY_STOCK_DOWNLOAD, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_download), "clicked", G_CALLBACK (sftp_download_files), NULL);

  sftp_panel.check_follow = GTK_WIDGET (gtk_builder_get_object (builder, "check_follow"));
  //gtk_button_set_image (GTK_BUTTON(sftp_panel.toggle_follow), gtk_image_new_from_stock (MY_STOCK_TERMINAL, GTK_ICON_SIZE_BUTTON));
  //g_signal_connect (G_OBJECT (sftp_panel.toggle_follow), "toggled", G_CALLBACK (toggle_follow_cb), NULL);
  g_signal_connect (sftp_panel.check_follow, "toggled", G_CALLBACK (toggle_follow_cb), NULL);
  
  sftp_panel.button_refresh = GTK_WIDGET (gtk_builder_get_object (builder, "button_refresh"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_refresh), gtk_image_new_from_icon_name ("view-refresh", GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_refresh), "clicked", G_CALLBACK (sftp_refresh_cb), NULL);

  sftp_panel.button_go = GTK_WIDGET (gtk_builder_get_object (builder, "button_go"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_go), gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_go), "clicked", G_CALLBACK (sftp_go_cb), NULL);

  sftp_panel.toggle_filter = GTK_WIDGET (gtk_builder_get_object (builder, "toggle_filter"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.toggle_filter), gtk_image_new_from_icon_name ("edit-find", GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.toggle_filter), "toggled", G_CALLBACK (toggle_filter_cb), NULL);

  /* combo and entry position */

  GtkWidget *hbox_position = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_position"));

#if (GTK_MAJOR_VERSION == 2)
  sftp_panel.combo_position = gtk_combo_box_entry_new_text ();
#else
  sftp_panel.combo_position = gtk_combo_box_text_new_with_entry ();
#endif
  gtk_box_pack_start (GTK_BOX (hbox_position), sftp_panel.combo_position, TRUE, TRUE, 0);
  
  //sftp_panel.entry_sftp_position = GTK_WIDGET (gtk_builder_get_object (builder, "entry_position"));
  sftp_panel.entry_sftp_position = gtk_bin_get_child (GTK_BIN(sftp_panel.combo_position));
  
  g_signal_connect (G_OBJECT(sftp_panel.entry_sftp_position), "insert-text", G_CALLBACK(position_typing_cb), NULL);
  g_signal_connect (G_OBJECT(sftp_panel.entry_sftp_position), "delete-text", G_CALLBACK(position_typing_cb), NULL);
  g_signal_connect (G_OBJECT(sftp_panel.entry_sftp_position), "activate", G_CALLBACK(sftp_go_cb), NULL);
  g_signal_connect (G_OBJECT(GTK_COMBO_BOX (sftp_panel.combo_position)), "changed", G_CALLBACK (combo_change_position_cb), NULL);

  /* entry filter */
  sftp_panel.entry_sftp_filter = GTK_WIDGET (gtk_builder_get_object (builder, "entry_filter"));
  gtk_widget_hide (sftp_panel.entry_sftp_filter);
  g_signal_connect (G_OBJECT (GTK_ENTRY (sftp_panel.entry_sftp_filter)), "changed", G_CALLBACK (entry_filter_changed_cb), NULL);

  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  //GtkTreeModel *tree_model;
  GtkTreeIter iter;
  GtkTreeSortable *sortable;

  GtkWidget *tree_view = gtk_tree_view_new ();
  gtk_tree_view_set_headers_clickable (GTK_TREE_VIEW(tree_view), TRUE);
  
  g_signal_connect (tree_view, "button-press-event", (GCallback) sftp_onButtonPressed, NULL);
  g_signal_connect (tree_view, "popup-menu", (GCallback) sftp_onPopupMenu, NULL);
  g_object_set (tree_view, "has-tooltip", TRUE, (char *) 0);
  g_signal_connect (tree_view, "query-tooltip", (GCallback) sftp_cell_tooltip_cb, 0);
  
  /* name column */
  
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Name"));
  gtk_tree_view_column_set_sort_column_id (column, SORTID_NAME);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_min_width (column, 200);
  gtk_tree_view_column_set_max_width (column, -1);

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column, renderer, "pixbuf", COLUMN_FILE_ICON, NULL);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  //g_object_set (renderer, "editable", TRUE, NULL);
  //g_signal_connect (renderer, "edited", G_CALLBACK (file_renamed_callback), NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes(column, renderer, "text", COLUMN_FILE_NAME, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  //gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(tree_view), FALSE);
  
  /* size column */
  
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  gtk_cell_renderer_set_alignment (renderer, 1.0, 0.0);
  column = gtk_tree_view_column_new_with_attributes (_("Size"), renderer, "text", COLUMN_FILE_SIZE, NULL);
  //gtk_tree_view_column_set_alignment (column, 1.0);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_sort_column_id (column, SORTID_SIZE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  
  /* date column */
  
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "cell-background", prefs.sftp_panel_background, "cell-background-set", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_("Modified"), renderer, "text", COLUMN_FILE_DATE, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_sort_column_id (column, SORTID_DATE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), GTK_TREE_VIEW_COLUMN (column));
  
  /* create list store */
  
  list_store_fs = gtk_list_store_new (N_FILE_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  
  sortable = GTK_TREE_SORTABLE (list_store_fs);
 
  gtk_tree_sortable_set_sort_func (sortable, SORTID_NAME, sort_name_compare_func, GINT_TO_POINTER(SORTID_NAME), NULL);
  gtk_tree_sortable_set_sort_func (sortable, SORTID_SIZE, sort_name_compare_func, GINT_TO_POINTER(SORTID_SIZE), NULL);
  gtk_tree_sortable_set_sort_func (sortable, SORTID_DATE, sort_name_compare_func, GINT_TO_POINTER(SORTID_DATE), NULL);
  
  gtk_tree_sortable_set_sort_column_id (sortable, SORTID_NAME, GTK_SORT_ASCENDING);

  gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), GTK_TREE_MODEL (list_store_fs));

  /* selection */
  
  g_sftp_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  gtk_tree_selection_set_mode (g_sftp_selection, GTK_SELECTION_MULTIPLE);
  tree_model_fs = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));

  g_signal_connect (G_OBJECT (tree_view), "row-activated", G_CALLBACK (row_activated_sftp_cb), g_sftp_selection);
  
  /* scrolled window */
  
  GtkWidget *scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "scrolled_files"));
  gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

  GtkIconTheme *icon_theme;
/*
  icon_theme = gtk_icon_theme_get_default ();
  pixbuf_file = gtk_icon_theme_load_icon (icon_theme, GTK_STOCK_FILE, 16, 0, &error);
  pixbuf_dir = gtk_icon_theme_load_icon (icon_theme, GTK_STOCK_DIRECTORY, 16, 0, &error);
*/
  
  sprintf (filename, "%s/file_16.png", globals.img_dir);
  pixbuf_file = gdk_pixbuf_new_from_file (filename, &error);
  
  sprintf (filename, "%s/directory_16.png", globals.img_dir);
  pixbuf_dir = gdk_pixbuf_new_from_file (filename, &error);
 
  /* Status label and spinner */
  
  sftp_panel.label_sftp_status = GTK_WIDGET (gtk_builder_get_object (builder, "label_sftp_status"));
  sftp_panel.spinner = GTK_WIDGET (gtk_builder_get_object (builder, "spinner_sftp"));
  
  sftp_spinner_stop ();

  // Stop button
  sftp_panel.button_stop = GTK_WIDGET (gtk_builder_get_object (builder, "button_stop"));
  gtk_button_set_image (GTK_BUTTON(sftp_panel.button_stop), gtk_image_new_from_icon_name ("process-stop", GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (sftp_panel.button_stop), "clicked", G_CALLBACK (sftp_stop_cb), NULL);
   
  /* Load additional popup menu items */
  
  sprintf (filename, "%s/additionals.xml", globals.data_dir);
  
  if (!file_exists (filename))
    sprintf (filename, "%s/additionals.xml", globals.app_dir);
    
  load_additional_ssh_menu (filename);
  
  log_write ("Sidebar additional items: %d from %s\n", count_load_additional_ssh_menu (), filename);
  
  /* create action group and menu for adds */
  
  char action_name[32], tmp_s[256], current_menu[64];
  GtkAction *action;
  
  if (count_load_additional_ssh_menu ())
    {
	    action_group_ssh_adds = gtk_action_group_new ("SSHAdds");
	    
	    strcpy (current_menu, "");
      strcpy (ui_sftp_popup_desc_adds, "<ui>"
                                       "  <popup name='SFTPPopupMenu'>"
                                       "    <placeholder name='Additionals'>");
                        
	    for (i=0; i<count_load_additional_ssh_menu (); i++)
        {
          sprintf (action_name, "add-item-%d", i);

          action = gtk_action_new (action_name, SSHMenuItems[i].label, NULL, NULL);
          g_signal_connect (action, "activate", G_CALLBACK (ssh_additionals_cb), NULL);
          gtk_action_group_add_action (action_group_ssh_adds, action);
          g_object_unref (action);
          
          if (strcmp (SSHMenuItems[i].parent_menu, current_menu) != 0)
            {
              if (i > 0)
                strcat (ui_sftp_popup_desc_adds, "</menu>");
                
              action = gtk_action_new (SSHMenuItems[i].parent_menu, SSHMenuItems[i].parent_menu, NULL, NULL);
              gtk_action_group_add_action (action_group_ssh_adds, action);
              g_object_unref (action);
              
              sprintf (tmp_s, "<menu action='%s'>", SSHMenuItems[i].parent_menu);
              strcat (ui_sftp_popup_desc_adds, tmp_s);
            }
            
          sprintf (tmp_s, "  <menuitem action='%s'/>\n", action_name);
          strcat (ui_sftp_popup_desc_adds, tmp_s);
          
          strcpy (current_menu, SSHMenuItems[i].parent_menu);
        }
	    
      strcat (ui_sftp_popup_desc_adds, "</menu><separator/>"
                                       "    </placeholder>"
                                       "  </popup>"
                                       "</ui>");
	  }
//printf ("%s\n", ui_sftp_popup_desc_adds);

  /* Load file types */
  
  log_write ("Loading file type images...\n");
  load_types ();

  p_ssh_current = NULL;
  
  return (GTK_WIDGET (gtk_builder_get_object (builder, "vbox_main")));
}

void
refresh_sftp_list_store (struct Directory_List *p_dl)
{
  struct Directory_Entry *e;
  GtkTreeIter iter;
  GdkPixbuf *icon;
  char tmp_s[1024];
  int n=0;

  gtk_list_store_clear (list_store_fs);

  if (p_dl == NULL)
    return;

  sftp_set_status ("Refreshing...");
  sftp_spinner_start ();
  sftp_begin ();
  //addIdleGTKMainIteration ();
  //doGTKMainIteration ();

  e = p_dl->head;
  
  while (e && !sftp_stoped_by_user ())
    {
      if (!p_dl->show_hidden_files && is_hidden_file (e))
        goto l_continue;

      if (p_ssh_current && p_ssh_current->filter && p_ssh_current->match_string[0])
        {
          sprintf (tmp_s, "*%s*", p_ssh_current->match_string);
          //log_debug ("match string: '%s'\n", tmp_s);
          if (fnmatch (tmp_s, e->name, FNM_PATHNAME) != 0)
            goto l_continue;
        }

      //sprintf (tmp_s, "%llu", e->size);
      
      gtk_list_store_append (list_store_fs, &iter);

      icon = is_directory (e) ? pixbuf_dir : get_type_pixbuf (e->name);
      
      if (icon == NULL)
        icon = pixbuf_file;

      gtk_list_store_set (list_store_fs, &iter, 
                          COLUMN_FILE_ICON, icon, 
                          COLUMN_FILE_NAME, e->name, 
                          COLUMN_FILE_SIZE, bytes_to_human_readable (e->size, tmp_s), 
                          COLUMN_FILE_DATE, timestamp_to_date (DATE_FORMAT, e->mtime), 
                          -1);
      
      if (n % 500 == 0)
        {
          sftp_set_status ("Refreshing %d/%d (%d%%)...", n, p_dl->count, (int) (100 * (float)n / (float)p_dl->count));

          //while (gtk_events_pending ())
          //  gtk_main_iteration ();
          //addIdleGTKMainIteration ();
          //doGTKMainIteration ();
        }

      n ++;

l_continue:
      e = e->next;
    }
  
  sftp_set_status ("%d file%s", n, n == 1 ? "" : "s");
  sftp_spinner_stop ();
  sftp_end ();
}

/**
 * refresh_sftp_panel() - shows files of p_ssh
 * (does not read filesystem)
 */ 
void
refresh_sftp_panel (struct SSH_Info *p_ssh)
{
  gboolean sensitive;

  sftp_clear_status ();

  /*if (!lt_ssh_is_connected (p_ssh))
    return;*/
  
  if (lt_ssh_is_connected (p_ssh))
    {
      //log_debug ("Checking SSH connection host=%s@%s\n", p_ssh->ssh_node->user, p_ssh->ssh_node->host);
      sftp_panel.active = 1;
      
      sensitive = TRUE;
    }
  else
    {
      //log_debug ("non ssh connection or ssh disconnected\n");
      sftp_panel.active = 0;
      
      sensitive = FALSE;
    }

  gtk_widget_set_sensitive (sftp_panel.button_home, sensitive);
  gtk_widget_set_sensitive (sftp_panel.button_go_up, sensitive);
  gtk_widget_set_sensitive (sftp_panel.button_folder_new, sensitive);
  gtk_widget_set_sensitive (sftp_panel.button_upload, sensitive);
  gtk_widget_set_sensitive (sftp_panel.button_download, sensitive);
  gtk_widget_set_sensitive (sftp_panel.button_refresh, sensitive);
  gtk_widget_set_sensitive (sftp_panel.button_go, sensitive);
  gtk_widget_set_sensitive (sftp_panel.check_follow, sensitive);
  gtk_widget_set_sensitive (sftp_panel.toggle_filter, sensitive);
  gtk_widget_set_sensitive (sftp_panel.entry_sftp_filter, sensitive);
  gtk_widget_set_sensitive (sftp_panel.entry_sftp_position, sensitive);

  if (sftp_panel.active)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(sftp_panel.check_follow), 
                                    lt_ssh_is_connected (p_ssh) && p_ssh->follow_terminal_folder);
    }
    
  p_ssh_current = p_ssh;

  //log_debug ("Refresh list store\n");
  
  refresh_sftp_list_store (lt_ssh_is_connected (p_ssh) ? &p_ssh->dirlist : NULL);

  gtk_entry_set_text (GTK_ENTRY (sftp_panel.entry_sftp_position), lt_ssh_is_connected (p_ssh) ? p_ssh->directory : "");

  sftp_panel_show_filter (p_ssh->filter);

  //log_debug ("End\n");
}

void
refresh_current_sftp_panel ()
{
  if (p_ssh_current)
    refresh_sftp_panel (p_ssh_current);
}

static void
count_foreach_helper (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata)
{
  gint *p_count = (gint*) userdata;
  g_assert (p_count != NULL);
  *p_count = *p_count + 1;
}

int
sftp_panel_count_selected_rows ()
{
  gint count = 0;
  gtk_tree_selection_selected_foreach (g_sftp_selection, count_foreach_helper, &count);

  return (count);
}

static void
selected_rows_foreach_helper (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata)
{
  gchar *name;
 
  gtk_tree_model_get (model, iter, COLUMN_FILE_NAME, &name, -1);
  log_debug ("  selected: %s\n", name);
  g_selected_files = g_slist_append (g_selected_files, name);
}

GSList *
sftp_panel_get_selected_files ()
{
  if (g_selected_files)
    g_slist_free (g_selected_files);
    
  g_selected_files = NULL;
  
  gtk_tree_selection_selected_foreach (g_sftp_selection, selected_rows_foreach_helper, NULL);
  return (g_selected_files);
}

void
load_additional_ssh_menu (char *filename)
{
  char parent_menu[32], tmp_s[128], *pc;
  int action, i;
  XML xmldoc;
  XMLNode *node_sidebar;
  XMLNode *node_item, *node;
  
  if (xml_load (&xmldoc, filename) != 0)
    {
      log_write ("%s\n", xmldoc.error.message);
      return;
    } 

  if (xmldoc.cur_root)
    {
      if ((node_sidebar = xml_node_get_child (xmldoc.cur_root, "sidebar")) == NULL)
        return;
    }
  else
    return;
    
  n_ssh_menu_items = 0;
  node = node_sidebar->children;
  
  while (node)
    {
      if (!strcmp (node->name, "menu"))
        {
          strcpy (parent_menu, xml_node_get_attribute (node, "label"));
          strcpy (tmp_s, xml_node_get_attribute (node, "action"));

          if (!strcmp (tmp_s, "paste"))
            action = ACTION_PASTE;
          else if (!strcmp (tmp_s, "execute"))
            action = ACTION_EXECUTE;
          else
            action = 0;
          
          node_item = node->children;
          
          while (node_item)
            {
              SSHMenuItems[n_ssh_menu_items].flags = 0;
              
              if (!strcmp (node_item->name, "menuitem"))
                {
                  strcpy (SSHMenuItems[n_ssh_menu_items].parent_menu, parent_menu);
                  SSHMenuItems[n_ssh_menu_items].action = action;
                  strcpy (SSHMenuItems[n_ssh_menu_items].label, (char *) xml_node_get_attribute (node_item, "label"));
                  
                  pc = (char *) xml_node_get_attribute (node_item, "output");
                  if (pc && pc[0] == '1')
                    SSHMenuItems[n_ssh_menu_items].flags |= OUTPUT;
                  
                  pc = (char *) xml_node_get_attribute (node_item, "refresh");
                  if (pc && pc[0] == '1')
                    SSHMenuItems[n_ssh_menu_items].flags |= REFRESH;
                  
                  SSHMenuItems[n_ssh_menu_items].flags |= MULTIPLE;
                  
                  pc = (char *) xml_node_get_attribute (node_item, "multiple");
                  if (pc && pc[0] == '0')
                    SSHMenuItems[n_ssh_menu_items].flags &= ~MULTIPLE;
                  
                  pc = (char *) xml_node_get_attribute (node_item, "enter");
                  if (pc && pc[0] == '1')
                    SSHMenuItems[n_ssh_menu_items].flags |= ENTER;
                                        
                  strcpy (SSHMenuItems[n_ssh_menu_items].command, xml_node_get_value (node_item));
                  
                  n_ssh_menu_items ++;
                }
                
              node_item = node_item->next;
            }
        }
      
      node = node->next;
    }
/*
  for (i=0; i<n_ssh_menu_items; i++)
    printf ("[%d] %s %s %d %d %s\n", 
            i, SSHMenuItems[i].parent_menu, SSHMenuItems[i].label, SSHMenuItems[i].flags, SSHMenuItems[i].action, SSHMenuItems[i].command);
*/
}

int
count_load_additional_ssh_menu ()
{
  return (n_ssh_menu_items);
}

void
load_types ()
{
  char filename[256], tmp_s[128], *pc;
  struct Type *p_type;
  XML xmldoc;
  XMLNode *node;
  GError *error = NULL;
  
  g_filetypes = NULL;
  
  sprintf (filename, "%s/types.xml", globals.data_dir);
  
  //log_debug ("filename=%s\n", filename);
  
  if (xml_load (&xmldoc, filename) != 0)
    {
      log_write ("%s\n", xmldoc.error.message);
      return;
    } 

  node = xmldoc.cur_root->children;
  
  while (node)
    {
      if (!strcmp (node->name, "type"))
        {
          p_type = (struct Type *) malloc (sizeof (struct Type));
          
          strcpy (p_type->id, xml_node_get_attribute (node, "id"));
          sprintf (p_type->imagefile, "%s/types/%s", globals.img_dir, xml_node_get_value (node));
          
          //log_debug ("type.id=%s type.imagefile=%s\n", type.id, type.imagefile);
          
          error = NULL;
          p_type->image = gdk_pixbuf_new_from_file (p_type->imagefile, &error);
          
          if (p_type->image == NULL)
            log_write ("Can't load %s\n", p_type->imagefile);
          
          g_filetypes = g_list_append (g_filetypes, p_type);
        }
        
      node = node->next;
    }
}

GdkPixbuf *
get_type_pixbuf (char *filename)
{
  GList *item;
  struct Type *p_type;
  char *id;
  
  /* Get file extension */
  
  id = (char *) strrchr (filename, '.');
  
  if (id)
    id ++;
  else
    id = basename (filename);
  
  item = g_list_first (g_filetypes);

  while (item)
    {
      p_type = (struct Type *) item->data;
      
      //log_debug ("id=%s vs p_type->id=%s\n", id, p_type->id);

      if (!strcasecmp (id, p_type->id))
        return (p_type->image);

      item = g_list_next (item);
    }
    
  return (NULL);
}

void
show_output (char *title, char *text)
{
  GtkWidget *dialog;
  gsize bytes_read, bytes_written;
  GError *error = NULL;
  gchar *utf8;
  PangoFontDescription *font_desc;
    
  dialog = gtk_dialog_new_with_buttons (title, GTK_WINDOW(main_window), 0,
                                        GTK_STOCK_OK,
                                        GTK_RESPONSE_OK,
                                        NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  //gtk_box_set_spacing (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 10);
  //gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  GtkTextBuffer *text_buffer;
  GtkWidget *text_scrolwin, *text_view;

  //GtkWidget *text_vbox = gtk_vbox_new (FALSE, 0);
  //gtk_container_set_border_width (GTK_CONTAINER (text_vbox), 0);

  text_view = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin (GTK_TEXT_VIEW (text_view), 2);

  text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

  text_scrolwin = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (text_scrolwin),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (text_scrolwin), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (text_scrolwin), text_view);
  gtk_widget_show (text_view);
  gtk_widget_show_all (text_scrolwin);

  if (text != NULL && text[0] != 0)
    {
      //utf8 = g_convert (text, -1, "UTF8", "ISO-8859-1", &bytes_read, &bytes_written, &error);
      utf8 = g_locale_to_utf8 (text, -1, &bytes_read, &bytes_written, &error);
      gtk_text_buffer_insert_at_cursor (text_buffer, utf8, (int) bytes_written);
      g_free (utf8);
    }
 
  gtk_widget_show_all (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
  gtk_box_pack_start (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), text_scrolwin, TRUE, TRUE, 0);
  
  gint w_width, w_height;
  GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (main_window));
  //gtk_window_get_size (GTK_WIDGET (dialog_window), &w_width, &w_height);
  gtk_widget_set_size_request (GTK_WIDGET (dialog), gdk_screen_get_width (screen)/2, gdk_screen_get_height (screen)/2);
  
  if (font_desc = pango_font_description_from_string (prefs.font_fixed))
    {
      gtk_widget_modify_font (text_view, font_desc);
      pango_font_description_free (font_desc);
    }

  gint result = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

int
sftp_queue_length ()
{
  int l;

  //lockSFTPQueue (__func__, TRUE);

  l = g_list_length (sftp_panel.queue);

  //lockSFTPQueue (__func__, FALSE);

  return (l);
}

int
sftp_queue_count (int *nUp, int *nDown)
{
  int i, nTotal = 0;
  STransferInfo *pTi;

  if (nUp)
    (*nUp) = 0;

  if (nDown)
    (*nDown) = 0;

  if (!sftp_panel.queue)
    return;

  //pthread_mutex_lock (&sftp_panel.mutexQueue);
  lockSFTPQueue (__func__, TRUE);

  for (i=0; i<g_list_length (sftp_panel.queue); i++) {
    pTi = (STransferInfo *) g_list_nth (sftp_panel.queue, i)->data;

    if (pTi->state <= TR_PAUSED) {
      if (nUp && pTi->action == SFTP_ACTION_UPLOAD)
        (*nUp) ++;
      
      if (nDown && pTi->action == SFTP_ACTION_DOWNLOAD)
        (*nDown) ++;

      nTotal ++;
    }
  }

  log_debug ("nTotal = %d\n", nTotal);

  //pthread_mutex_unlock (&sftp_panel.mutexQueue);
  lockSFTPQueue (__func__, FALSE);

  return (nTotal);
}

STransferInfo *
sftp_queue_nth (int n)
{
  if (n < 0 || n > g_list_length (sftp_panel.queue) -1)
    return NULL;

  return ((STransferInfo *) g_list_nth (sftp_panel.queue, n)->data);
}

int
sftp_queue_add (int action, GSList *filelist, struct SSH_Info *p_ssh, char *local_directory)
{
  struct TransferInfo *pTi;
  struct Directory_Entry *e;
  struct stat info;
  //gchar *filename;
  gchar *gfile;
  int rc;

  while (filelist)
    {
      gfile = (gchar *) filelist->data;
              
      pTi = g_new0 (STransferInfo, 1);
      //time (&ti.start_time);

      pTi->action = action;
      pTi->state = TR_READY;
      pTi->p_ssh = p_ssh;
      strcpy (pTi->host, p_ssh->ssh_node->host);

      if (action == SFTP_ACTION_UPLOAD)
        {
          //strcpy (pTi->status, "uploading...");
          strcpy (pTi->filename, g_path_get_basename (g_filename_from_uri (gfile, NULL, NULL)));

          strcpy (pTi->source, g_filename_from_uri (gfile, NULL, NULL));
          sprintf (pTi->destination, "%s/%s", p_ssh->directory, (char *) basename (gfile));
          strcpy (pTi->destDir, p_ssh->directory);
        }
      else
        {
          //strcpy (pTi->status, "downloading...");

          //filename = gfile;
          strcpy (pTi->filename, gfile);
          sprintf (pTi->source, "%s/%s", p_ssh->directory, gfile);
          sprintf (pTi->destination, "%s/%s", local_directory, pTi->filename);
          strcpy (pTi->destDir, local_directory);
       }

      shortenString (pTi->filename, 30, pTi->shortenedFilename);

      //pthread_mutex_lock(&sftp_panel.mutexQueue);
      lockSFTPQueue (__func__, TRUE);

      log_write ("Enqueuing:\n"
                 " Status: %s\n"
                 " Host: %s\n"
                 " Source: %s\n"
                 " Destination: %s\n"
                 " Dest dir: %s\n",
                 getTransferStatusDesc(pTi->state),
                 pTi->host,
                 pTi->source,
                 pTi->destination,
                 pTi->destDir);

      sftp_panel.queue = g_list_append (sftp_panel.queue, pTi);

      //pthread_mutex_unlock(&sftp_panel.mutexQueue);
      lockSFTPQueue (__func__, FALSE);

      filelist = g_slist_next (filelist);
    }

  if (!async_is_transferring ()) {
    log_write ("Creating transfer thread...\n");

    pthread_t thread_transfer;
    //GThread *thread_transfer;

    rc = pthread_create (&thread_transfer, NULL, async_sftp_transfer, (void *)sftp_panel.queue);
    //thread_transfer = g_thread_new ("async-transfer", async_sftp_transfer, (gpointer) sftp_panel.queue);

    if (rc) {
    //if (!thread_transfer) {
      msgbox_error ("Can't start async transfer\n");
      return 1;
    } 

    log_write ("Thread created\n");

    //g_thread_join (thread_transfer);
  }
  else
    log_write ("Transfer thread already existing\n");

  //update_statusbar_upload_download ();
  gdk_threads_add_idle (update_statusbar_upload_download, NULL);

  return 0;
}

