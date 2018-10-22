
#ifndef _CONNECTION_LIST_H
#define _CONNECTION_LIST_H

#include <gtk/gtk.h>

#define LOCAL_SHELL_TAG "local-shell"

#define MAX_BOOKMARKS 10
/*
struct Bookmark {
  char item[1024];
  struct Bookmark *next;
  struct Bookmark *prev;
};

struct Bookmarks {
  struct Bookmark *head;
  struct Bookmark *tail;
};
*/
#define CONN_FLAG_MASK 255
#define CONN_FLAG_NONE 0
#define CONN_FLAG_IGNORE_WARNINGS 1

#define CONN_WARNING_NONE 0
#define CONN_WARNING_HOST_DUPLICATED 1
#define CONN_WARNING_PROTOCOL_NOT_FOUND 2
#define CONN_WARNING_PROTOCOL_COMMAND_NOT_FOUND 4

#define MAX_NOTE_LEN 1024

#define CONN_SAVE_NORMAL 0
#define CONN_SAVE_USERPASS 1

#define CONN_AUTH_MODE_PROMPT 0
#define CONN_AUTH_MODE_SAVE 1
#define CONN_AUTH_MODE_KEY 2

typedef struct _SSH_Options {
  int x11Forwarding;
  int agentForwarding;
  int disableStrictKeyChecking;
  int flagKeepAlive;
  int keepAliveInterval;
} SSH_Options;

typedef struct Connection
  {
    char name[256];
    char host[256];
    int port;
    char protocol[64];
    //char emulation[64];
    char last_user[32];
    char user_options[256];
    //int auth; // deprecated
    int auth_mode;
    char auth_user[32];
    char auth_password[32];
    char auth_password_encrypted[64]; /* base 64 encoded */
    char user[32];
    char password[32];
    char password_encrypted[64]; /* base 64 encoded */
    char directory[256]; /* e.g. samba condivision folder */
    unsigned int flags;
    char note[MAX_NOTE_LEN];
    char sftp_dir[1024];
    char upload_dir[1024];
    char download_dir[1024];
    //int x11Forwarding;
    char identityFile[1024];
    //int disableStrictKeyChecking;
    SSH_Options sshOptions;
    
    GPtrArray *directories;
    //struct Bookmarks history;

    /* reserved */
    unsigned int warnings;

    struct Connection *next;
  } SConnection;

struct Connection_List
  {
    struct Connection *head;
    struct Connection *tail;
  };

void connection_init (SConnection *);

void cl_init (struct Connection_List *p_cl);
void cl_release (struct Connection_List *p_cl);
struct Connection * cl_append (struct Connection_List *p_cl, struct Connection *p_new);
struct Connection *cl_insert_sorted (struct Connection_List *p_cl, struct Connection *p_new);
struct Connection *cl_host_search (struct Connection_List *p_cl, char *host, char *skip_this);
struct Connection *cl_get_by_index (struct Connection_List *p_cl, int index);
struct Connection *cl_get_by_name (struct Connection_List *p_cl, char *name);

/*
//void init_bookmarks (struct Bookmarks *bookmarks);
int count_bookmarks (struct Bookmarks *bookmarks);
struct Bookmark* search_bookmark (struct Bookmarks *bookmarks, char *item);
void add_bookmark (struct Bookmarks *bookmarks, char *item);
*/
int count_directories (SConnection *pConn);
int search_directory (SConnection *pConn, gchar *item);
void add_directory (SConnection *pConn, char *item);

int connection_fill_from_string (struct Connection *p_conn, char *connection_string);

#endif

