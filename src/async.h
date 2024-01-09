
#ifndef _ASYNC_H
#define _ASYNC_H

#include <gtk/gtk.h>
#include "sftp-panel.h"

#define LOCK_SSH lockSSH(__func__, TRUE);
#define UNLOCK_SSH lockSSH(__func__, FALSE);

void lockSSH (char *caller, gboolean flagLock);
void lockSFTPQueue (char *caller, gboolean flagLock);

void asyncInit();
gpointer async_lterm_loop(gpointer data);

gboolean async_is_transferring ();
void *async_sftp_transfer (void *userdata);

void lockSSH (char *caller, gboolean flagLock);

#endif
