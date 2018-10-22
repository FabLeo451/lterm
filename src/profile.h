
#ifndef _PROFILE_H
#define _PROFILE_H

/* program settings */

#define PROFILE_OK 0
#define PROFILE_FILE_NOT_FOUND 1
#define PROFILE_PARAMETER_NOT_FOUND 2

#define PROFILE_SAVE 10
#define PROFILE_DELETE 11

#define PROFILE_BEFORE_SECTION 1
#define PROFILE_IN_SECTION 2
#define PROFILE_AFTER_SECTION 3

int profile_load_string (char *profile_file, char *section, char *param, char *dest, char *default_val);
int profile_modify_string (int operation, char *profile_file, char *section, char *param, char *value);
int profile_load_int (char *profile_file, char *section, char *param, int default_val);
int profile_modify_int (int operation, char *profile_file, char *section, char *param, int value);
int profile_delete_section (char *profile_file, char *section);

/* graphic profile */

struct Profile {
  int id;
  char name[256];
  int font_use_system; /* Use system font for terminal */
  char font[128];
  char bg_color[64];
  char fg_color[64];
  double alpha;
  int cursor_shape;
  int cursor_blinking;
  int bell_audible;
  int bell_visible;
  
  struct Profile* next;
};

struct ProfileList
  {
    int id_default;
    
    struct Profile *head;
    struct Profile *tail;
  };
  
void profile_list_init (struct ProfileList *p_pl);
void profile_list_release (struct ProfileList *p_pl);
struct Profile *profile_get_by_id (struct ProfileList *p_pl, int id);
struct Profile *profile_get_by_position (struct ProfileList *p_pl, int pos);
struct Profile *profile_get_by_name (struct ProfileList *p_pl, char *name);
struct Profile *profile_get_default (struct ProfileList *p_pl);
struct Profile *profile_list_append (struct ProfileList *p_pl, struct Profile *p);
void profile_list_delete (struct ProfileList *p_pl, struct Profile *p);
int profile_count (struct ProfileList *p_pl);
int load_profiles (struct ProfileList *p_pl, char *filename);
int save_profiles (struct ProfileList *p_pl, char *filename);
void profile_create_default (struct ProfileList *p_pl);

#endif
