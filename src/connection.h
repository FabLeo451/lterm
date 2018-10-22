
#ifndef _CONNECTION_H
#define _CONNECTION_H

#include <gtk/gtk.h>
#include "grouptree.h"
#include "connection_list.h"
#include "gui.h"

#define XML_STATE_INIT 0
#define XML_STATE_CONNECTION_SET 1
#define XML_STATE_FOLDER 2
#define XML_STATE_CONNECTION 3
#define XML_STATE_AUTHENTICATION 4
#define XML_STATE_AUTH_USER 5
#define XML_STATE_AUTH_PASSWORD 6
#define XML_STATE_LAST_USER 7
#define XML_STATE_DIRECTORY 8
#define XML_STATE_USER_OPTIONS 9
#define XML_STATE_NOTE 10

struct Connection *get_connection (struct Connection_List *p_cl, char *name);
struct Connection *get_connection_by_index (int index);
struct Connection *get_connection_by_name (char *name);
struct Connection *get_connection_by_host (char *host);

//int load_connections_from_file_version (char *filename, struct Connection_List *p_cl, int version);
//int load_connections_from_file (char *filename, struct Connection_List *p_cl);
int load_connections_from_file_xml (char *filename);
GList *load_connection_list_from_file_xml (char *filename);
int save_connections_to_file (char *, struct Connection_List *, int);
//int save_connections_to_file_xml_from_list (struct Connection_List *pList, char *filename);
int save_connections_to_file_xml_from_glist (GList *pList, char *filename);
int load_connections ();

#define ERR_VALIDATE_MISSING_VALUES 1
#define ERR_VALIDATE_EXISTING_CONNECTION 2
#define ERR_VALIDATE_EXISTING_ITEM_LEVEL 3

char *get_validation_error_string (int error_code);
int validate_name (struct GroupNode *p_parent, struct GroupNode *p_node_upd, struct Connection *p_conn, char *item_name);
struct GroupNode *add_update_connection (struct GroupNode *p_node, struct Connection *p_conn_model);
struct GroupNode *add_update_folder (struct GroupNode *p_node);

void connection_import_lterm ();
void connection_import_MobaXterm ();
void connection_import_Putty ();
void connection_export_lterm ();
void connection_export_MobaXterm ();
void connection_export_Putty ();
void connection_export_CSV ();

void connection_init_stuff ();
void rebuild_tree_store ();
GtkWidget *create_entry_control (char *label, GtkWidget *entry);
void create_quick_launch_window (struct QuickLaunchWindow *p_qlv);

#endif

