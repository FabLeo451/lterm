
#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#define PROT_TYPE_TELNET 1
#define PROT_TYPE_SSH 2
#define PROT_TYPE_SAMBA 3
#define PROT_TYPE_OTHER 99

#define PROT_FLAG_NO 0
#define PROT_FLAG_ASKUSER 1
#define PROT_FLAG_ASKPASSWORD 2
#define PROT_FLAG_DISCONNECTCLOSE 4
#define PROT_FLAG_MASK 255

struct Protocol
  {
    char name[64];
    unsigned int type;
    char command[256];
    char args[256];
    int port;
    unsigned int flags;

    struct Protocol *next;
  };

struct Protocol_List
  {
    struct Protocol *head;
    struct Protocol *tail;
  };

void pl_init (struct Protocol_List *p_pl);
void pl_release (struct Protocol_List *p_pl);

struct Protocol *get_protocol (struct Protocol_List *p_pl, char *name);
void manage_protocols (struct Protocol_List *);
void refresh_protocols (struct Protocol_List *p_pl);
int load_protocols_from_file_xml (char *filename, struct Protocol_List *p_pl);
int save_protocols_to_file_xml (char *filename, struct Protocol_List *p_pl);
void check_standard_protocols (struct Protocol_List *p_pl);

#endif
