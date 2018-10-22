
#ifndef _SSH_PANEL_H
#define _SSH_PANEL_H

#include <gtk/gtk.h>
#include <time.h>
#include "ssh.h"

#define SFTP_ACTION_UPLOAD 1
#define SFTP_ACTION_DOWNLOAD 2

//#define SFTP_BUFFER_SIZE 1*1024
#define SFTP_PROGRESS 1024*1024

#define DATE_FORMAT "%Y-%m-%d %H:%M:%S"

/* Additional SSH operations */

#define MAX_SSH_MENU_ITEMS 30

#define OUTPUT 1
#define REFRESH 2
#define MULTIPLE 4
#define ENTER 8

#define ACTION_PASTE 1
#define ACTION_EXECUTE 2

struct CustomMenuItem {
  //int id;
  char parent_menu[64];
  char label[128];
  unsigned int flags;
  int action;
  char command[1024];
  //struct SSHCommand *next;
};

/* File type */
struct Type {
  char id[32];
  char imagefile[256];
  GdkPixbuf *image;
};

/* Info for transfer window */

enum { TR_READY=0, TR_IN_PROGRESS, TR_PAUSED, TR_CANCELLED_USER, TR_CANCELLED_ERRORS, TR_COMPLETED };

typedef struct TransferInfo {
  struct SSH_Info *p_ssh;
  int action;
  gboolean sourceIsDir;
  char source[1024];
  char destination[1024];
  char shortenedFilename[1024];
  char filename[1024];
  char destDir[1024];
  char host[128];
  //char status[256];
  uint64_t size;
  uint64_t worked;
  time_t start_time;
  time_t last_update;
  int state;
  int result;
  char errorDesc[2048];

  //GtkTreeIter iter;
  
} STransferInfo;

/* SFTP Panel */

struct SFTP_Panel {
  gboolean active;
  gboolean position_selected_tearoff;
  gboolean stop;
  
  GtkWidget *button_home;
  GtkWidget *button_go_up;
  GtkWidget *button_file_new;
  GtkWidget *button_folder_new;
  GtkWidget *button_upload;
  GtkWidget *button_download;
  GtkWidget *button_refresh;
  GtkWidget *button_go;
  GtkWidget *check_follow;
  GtkWidget *toggle_filter;
  GtkWidget *entry_sftp_filter;
  
  GtkWidget *combo_position;
  GtkWidget *entry_sftp_position;
  
  GtkWidget *label_sftp_status;
  GtkWidget *spinner;
  GtkWidget *button_stop;

  //pthread_mutex_t mutexQueue;
  GList *queue;
};

typedef struct MirrorFile {
  struct SSH_Node *sshNode;
  char localDir[2048];
  char localFile[2048];
  char remoteFile[2048];
  int wd; // Watch file descriptor
  time_t lastSaved;
} SMirrorFile;

void sftp_panel_mirror_dump ();

#define SFTP_STATUS_IMMEDIATE 1
#define SFTP_STATUS_IDLE 2

void sftp_set_status_mode (int mode, const char *fmt,...);
void sftp_set_status (const char *fmt,...);
//void sftp_clear_status_mode (int mode);
void sftp_clear_status ();
void sftp_spinner_start ();
void sftp_spinner_stop ();
void sftp_spinner_refresh ();
void sftp_begin ();
void sftp_end ();
gboolean sftp_stoped_by_user ();

int transfer_set_error (STransferInfo *pTi, int code, char *fmt, ...);
char * transfer_get_error (STransferInfo *pTi);
char * getTransferStatusDesc (int i);
/*
void transfer_window_create (struct TransferInfo *p_ti);
void transfer_window_update (struct TransferInfo *p_ti);
void transfer_window_close ();
*/
int upload_directory (struct SSH_Info *p_ssh, char *rootdir, char *destdir, STransferInfo *p_ti);
int download_directory (struct SSH_Info *p_ssh, char *rootdir, char *destdir, STransferInfo *p_ti);
int sftp_copy_file_upload (sftp_session sftp, struct TransferInfo *p_ti);
int sftp_copy_file_download (sftp_session sftp, struct TransferInfo *p_ti);
//int transfer_sftp (int action, GSList *filelist, struct SSH_Info *p_ssh, char *local_directory);

void follow_terminal_folder ();
void sftp_panel_show_filter (gboolean show);
GtkWidget *create_sftp_panel ();
void refresh_sftp_panel (struct SSH_Info *p_ssh);
void refresh_current_sftp_panel ();
int sftp_panel_count_selected_rows ();
GSList *sftp_panel_get_selected_files ();

int sftp_panel_mirror_file_clear (struct SSH_Node *p_ssh_node, int flagRemoveAll);
void sftp_panel_check_inotify ();
void sftp_panel_open ();

void sftp_panel_create_folder ();
void sftp_panel_create_file ();
void sftp_panel_rename ();
void sftp_panel_delete ();
void sftp_panel_change_time ();
//void sftp_panel_copy_name_terminal ();
void sftp_panel_copy_path_terminal ();
void sftp_panel_copy_path_clipboard ();
void load_additional_ssh_menu (char *filename);
int count_load_additional_ssh_menu ();
void load_types ();
GdkPixbuf *get_type_pixbuf (char *filename);
void show_output (char *title, char *text);

int sftp_queue_length ();
int sftp_queue_count (int *nUp, int *nDown);
STransferInfo *sftp_queue_nth (int n);
int sftp_queue_add (int action, GSList *filelist, struct SSH_Info *p_ssh, char *local_directory);

#endif

