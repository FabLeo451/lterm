
/**
 * @file utils.h
 * @brief Definisce le strutture Prefs e Globals, più alcune costanti
 */

#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>

#define LOG_MSG_NONE 0
#define LOG_MSG_TIME 1
#define LOG_MSG_SEP1 2

#define MAXLINE 1024
#define MAXBUFLEN   1024

#define DE_UNKNOWN 0
#define DE_GNOME 1
#define DE_KDE 2
#define DE_XFCE 3
#define DE_CINNAMON 4
#define DE_MAC_OS_X 10

#define NVL(a,b) (a) != NULL ? (a) : (b)


void ltrim (char *s);
void rtrim (char *s);
void trim (char *s);
char *timestamp_to_date (char *format, time_t t);
char *bytes_to_human_readable (double size, char *buf);
char *seconds_to_hhmmdd (uint64_t seconds, char *buf);
char *permissions_octal_to_string (uint32_t value, char *buf);
char *replace_str (const char *str, const char *old, const char *new);
void split_string (char *str, char **splitted, char *delimiters/*, int skipNulls*/);
char **splitString (char *str, char *delimiters, int skipNulls, char *quotes, int trailingNull, int *pCount);
int check_command (char *command);
int get_desktop_environment ();
char *get_desktop_environment_name (int id);
char *des_encrypt_b64 (char *clear_text);
char *des_decrypt_b64 (char *ecrypted_text);
void des_decrypt_b64_2 (char *ecrypted_text, char *clear_text);
char *shortenString (char *original, int threshold, char *shortened);
int file_exists (char *filename);
char *readFile (filename);

#endif

