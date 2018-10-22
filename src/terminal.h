
#ifndef _TERMINAL_H
#define _TERMINAL_H

#include "gui.h"

gboolean terminal_new (struct ConnectionTab *p_connection_tab, char *directory);
int log_on (struct ConnectionTab *p_conn_tab);
void session_load ();
void session_save ();
char *get_remote_directory_from_vte_title (GtkWidget *vte);
char *get_remote_directory ();
int check_log_in_parameter (int auth, char *auth_param, char *param, char *default_param, 
                        int attempt, unsigned int prot_flags, int required_flags, 
                        char *label, int query_type, char *log_on_data);
int asked_for_user (struct ConnectionTab *p_ct, char *log_on_data);
int asked_for_password (struct ConnectionTab *p_ct, char *log_on_data);
int check_log_in_state (struct ConnectionTab *p_ct, char *line);
int load_session_file (char *filename);
int save_session_file (char *filename);
void terminal_write_ex (struct ConnectionTab *p_ct, const char *fmt, ...);
void terminal_write (const char *fmt, ...);
void terminal_write_child_ex (SConnectionTab *pTab, const char *text);
void terminal_write_child (const char *text);
void terminal_set_search_expr (char *expr);
void terminal_find_next ();
void terminal_find_previous ();
int terminal_set_encoding (SConnectionTab *pTab, const char *codeset);
void terminal_set_font_from_string (VteTerminal *vte, const char *font);

#endif

