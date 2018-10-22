
#ifndef _SSH_H
#define _SSH_H

#include <libssh/libssh.h> 
#include <libssh/sftp.h>
#include <time.h>

#define SSH_ERR_CONNECT 1
#define SSH_ERR_AUTH 2
#define SSH_ERR_UNKNOWN_AUTH_METHOD 3

struct Directory_Entry
  {
    int type;
    char name[256];
    long long unsigned int size;
    long unsigned int mtime;
    char owner[32];
    char group[32];
    uint32_t permissions;   

    struct Directory_Entry *next;
  };

struct Directory_List
  {
    struct Directory_Entry *head;
    struct Directory_Entry *tail;

    int show_hidden_files;
    int count;
  };
 
/**
 * struct SSH_Node
 * ssh/sftp information node
 * Shared between tabs connected to the same host and user
 */ 
struct SSH_Node
  {
    ssh_session session;
    sftp_session sftp;
    //ssh_channel channel;
    int auth_methods;
    char host[32];
    char user[32];
    char password[32];
    int port;
    
    int refcount;
    int valid;
    time_t last;
    struct SSH_Node *next;
  };
  
/**
 * struct SSH_List
 * list of ssh established connections
 */ 
struct SSH_List
  {
    struct SSH_Node *head;
    struct SSH_Node *tail;
  };

/**
 * struct SSH_Info
 * ssh/sftp informations
 * Every tab holds its own SSH_Info object
 */
struct SSH_Info
  {
    struct SSH_Node *ssh_node;
    char error_s[512];
    char home[1024]; /* user home directory */
    char directory[1024]; /* used in sftp panel */
    struct Directory_List dirlist;
    
    /* toggle flags */
    int follow_terminal_folder;
    int filter;
    char match_string[1024];
  };
  
struct SSH_Auth_Data
  {
    char host[32];
    char user[32];
    char password[32];
    int port;
    int sftp_enabled;
    int mode;
    char identityFile[512];
    
    int error_code;
    char error_s[512];
  };
  
void ssh_list_init (struct SSH_List *p_ssh_list);
void ssh_list_release_chain (struct SSH_Node *p_head);
void ssh_list_release (struct SSH_List *p_ssh_list);
struct SSH_Node *ssh_list_append (struct SSH_List *p_ssh_list, struct SSH_Node *p_new);
struct SSH_Node *ssh_list_search (struct SSH_List *p_ssh_list, char *host, char *user);
void ssh_list_dump (struct SSH_List *p_ssh_list);

struct SSH_Node *ssh_node_connect (struct SSH_List *p_ssh_list, struct SSH_Auth_Data *p_auth);
void ssh_node_free (struct SSH_Node *p_ssh_node);
void ssh_node_ref (struct SSH_Node *p_ssh_node);
void ssh_node_unref (struct SSH_Node *p_ssh_node);
void ssh_node_set_validity (struct SSH_Node *p_ssh_node, int valid);
int ssh_node_get_validity (struct SSH_Node *p_ssh_node);
ssh_channel ssh_node_open_channel (struct SSH_Node *p_node);
int ssh_node_keepalive (struct SSH_Node *p_ssh_node);
void ssh_node_update_time (struct SSH_Node *p_ssh_node);

void dl_init (struct Directory_List *p_dl);
void dl_release_chain (struct Directory_Entry *p_head);
void dl_release (struct Directory_List *p_dl);
void dl_append (struct Directory_List *p_dl, struct Directory_Entry *p_new);
void ssh_list_remove (struct SSH_List *p_ssh_list, struct SSH_Node *p_node);
void dl_dump (struct Directory_List *p_dl);
void ssh_list_keepalive (struct SSH_List *p_ssh_list);
int is_hidden_file (struct Directory_Entry *entry);
int is_directory (struct Directory_Entry *entry);
struct Directory_Entry *dl_search_by_name (struct Directory_List *p_dl, char *name);

void lt_ssh_init (struct SSH_Info *p_ssh);
//int lt_ssh_connect_old (struct SSH_Info *p_ssh);
int lt_ssh_connect (struct SSH_Info *p_ssh, struct SSH_List *p_ssh_list, struct SSH_Auth_Data *p_auth);
void lt_ssh_disconnect (struct SSH_Info *p_ssh);
int lt_ssh_is_connected (struct SSH_Info *p_ssh);
int lt_ssh_getenv (struct SSH_Info *p_ssh, char *variable, char *value);
void sftp_normalize_directory (struct SSH_Info *p_ssh, char *path);
//int lt_sftp_create (struct SSH_Info *p_ssh);
int sftp_refresh_directory_list (struct SSH_Info *p_ssh);
int lt_ssh_exec (struct SSH_Info *p_ssh, char *command, char *output, int outlen, char *error, int errlen);

#endif

