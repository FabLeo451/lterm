
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
 * @file async.c
 * @brief Background operations
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include "gui.h"
#include "sftp-panel.h"
#include "main.h"

extern Globals globals;
extern Prefs prefs;

// Access to ssh operations
pthread_mutex_t mutexSSH = PTHREAD_MUTEX_INITIALIZER;

// Access to sftp queue
pthread_mutex_t mutexSFTPQueue = PTHREAD_MUTEX_INITIALIZER;
//GMutex mutexSFTPQueue;

time_t g_last_checkpoint_time;
GPtrArray *transferArray;

gboolean gIsTransferring;

void
lockSSH (char *caller, gboolean flagLock)
{
  if (flagLock) {
    log_debug ("[%s] locking SSH mutex...\n", caller);
    pthread_mutex_lock (&mutexSSH);
  }
  else {
    log_debug ("[%s] unlocking SSH mutex...\n", caller);
    pthread_mutex_unlock (&mutexSSH);
  }
}

void
lockSFTPQueue (char *caller, gboolean flagLock)
{
  if (flagLock) {
    //log_debug ("[%s] locking SFTP queue mutex...\n", caller);
    pthread_mutex_lock (&mutexSFTPQueue);
    //g_mutex_lock (&mutexSFTPQueue);
    //log_debug ("[%s] queue locked\n", caller);
  }
  else {
    //log_debug ("[%s] unlocking SFTP queue mutex...\n", caller);
    pthread_mutex_unlock (&mutexSFTPQueue);
    //g_mutex_unlock (&mutexSFTPQueue);
    //log_debug ("%s unlocked\n", caller);
  }
}

time_t 
checkpoint_get_last () { return (g_last_checkpoint_time); }

void 
checkpoint_update () { g_last_checkpoint_time = time (NULL); }

void
asyncInit()
{  
  // Init checkpoint
  checkpoint_update ();

  transferArray = g_ptr_array_new ();
  gIsTransferring = FALSE;
}

gpointer
async_lterm_loop(gpointer data)
{
  gint i;

  log_write ("%s BEGIN THREAD 0x%08x\n", __func__, pthread_self ());

  while (globals.running) {

    /* checkpoint */

    if (time (NULL) > checkpoint_get_last () + prefs.checkpoint_interval)
      {
        log_debug ("Checkpoint\n");
/*
#ifdef DEBUG
        log_debug ("Current function: %s\n", getCurrentFunction ());
#endif
*/
        checkpoint_update ();
      }

    //log_debug ("Async iteration\n");

    // Check remote open files
    sftp_panel_check_inotify ();

    g_usleep (G_USEC_PER_SEC);
  }

  log_write ("%s END\n", __func__);

  return (NULL);
}

gboolean
async_is_transferring ()
{
  return gIsTransferring;
}

int
async_sftp_transfer (gpointer userdata)
{
  int i, rc;
  int nUp, nDown;
  struct Directory_Entry *e;
  struct stat info;
  //gchar *gfile;
  STransferInfo *pTi;
  //GList *queue = (GList *) userdata;
  gboolean found;

  gIsTransferring = TRUE;

  log_write ("TRANSFER THREAD STARTED: 0x%08x\n", pthread_self());

  char transferReport[256];

  while (sftp_queue_count (NULL, NULL)) {

    // Get next to be started
    i = 0;
    found = FALSE;

    log_write ("Checking ready to start items...\n");

    lockSFTPQueue (__func__, TRUE);

    while (i < sftp_queue_length ()) {
      pTi = sftp_queue_nth (i);

      log_debug ("%d %s (%d)\n", i, pTi->shortenedFilename, pTi->state);

      if (pTi->state == TR_READY) {
        log_write ("Found %s ready to start.\n", pTi->shortenedFilename);
        found = TRUE;
        break;
      }

      i ++;
    }

    lockSFTPQueue (__func__, FALSE);

    if (found) {
      log_write ("Starting %s for %s\n", pTi->action == SFTP_ACTION_UPLOAD ? "upload" : "download", pTi->filename);

      time (&(pTi->start_time));
      pTi->state = TR_IN_PROGRESS;

      if (pTi->action == SFTP_ACTION_UPLOAD)
        {
          stat (pTi->source, &info);
          
          if (S_ISDIR (info.st_mode))
            {
              log_write ("Upload directory %s\n", pTi->source);

              pTi->sourceIsDir = TRUE;
              upload_directory (pTi->p_ssh, pTi->source, pTi->destDir, pTi);
            }
          else
            {
              log_write ("Uploading to %s: %s\n", pTi->p_ssh->ssh_node->host, pTi->filename);

              rc = sftp_copy_file_upload (pTi->p_ssh->ssh_node->sftp, pTi);
              
              log_write ("Uploaded %d bytes\n", pTi->worked);
            }
        }
      else
        {
          e = dl_search_by_name (&pTi->p_ssh->dirlist, pTi->filename);
          
          if (is_directory (e))
            {
              log_write ("Download directory %s\n", e->name);
              
              pTi->sourceIsDir = TRUE;
              
              download_directory (pTi->p_ssh, pTi->source, pTi->destDir, pTi);
            }
          else
            {
              log_write ("Downloading from %s: %s\n",  pTi->p_ssh->ssh_node->host, pTi->filename);
              
              //pTi->sourceIsDir = TRUE;
              
              rc = sftp_copy_file_download (pTi->p_ssh->ssh_node->sftp, pTi);

              log_write ("Downloaded %d bytes\n", pTi->worked);
            }
        }

      if (pTi->result)
        log_write ("%s %s\n", pTi->shortenedFilename, pTi->errorDesc);

      // Desktop notification

      char message[2048];
      sprintf (message, "%s\n%s", pTi->filename, pTi->result ? pTi->errorDesc : "successfully transferred");
      notifyMessage (message);

      log_debug ("Ask for uploads/downloads statusbar refresh\n");

      gdk_threads_add_idle (update_statusbar_upload_download, NULL);
    } // found
  } // main while

  gIsTransferring = FALSE;

  log_write ("TRANSFER THREAD FINISHED: 0x%08x\n", pthread_self());

  pthread_exit(NULL);
  //g_thread_exit (0);
}


