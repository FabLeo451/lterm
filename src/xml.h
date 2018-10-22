
#ifndef __XML_H__
#define __XML_H__

#include <glib.h>

typedef struct _XMLNode XMLNode;

struct _XMLNode 
  {
	  gchar *name;
	  gchar *value;
	  gboolean raw_mode;

    XMLNode *next;
    XMLNode *prev;
    XMLNode *parent;
    XMLNode *children;

    GSList *attributes;
    gint ref_count;
  };

typedef struct _XML XML;

struct _XML 
  {
	  gpointer user_data;
	  //GDestroyNotify notify;
	
	  XMLNode *cur_root;
	  XMLNode *cur_node;
		
	  //GMarkupParser *m_parser;
	  GMarkupParseContext *context;
    GError error;
  };

const gchar *xml_node_get_value (XMLNode *node);
void xml_node_set_value (XMLNode *node, const gchar *value);
XMLNode *xml_node_add_child (XMLNode *node, const gchar *name, const gchar *value);
void xml_node_set_attributes (XMLNode *node, const gchar *name, ...);
void xml_node_set_attribute (XMLNode *node, const gchar *name, const gchar *value);
const gchar *xml_node_get_attribute (XMLNode *node, const gchar *name);
XMLNode *xml_node_get_child (XMLNode *node, const gchar *child_name);
XMLNode *xml_node_find_child (XMLNode *node, const gchar *child_name);
gboolean xml_node_get_raw_mode (XMLNode *node);
void xml_node_set_raw_mode (XMLNode *node, gboolean raw_mode);
XMLNode *xml_node_ref (XMLNode *node);
void xml_node_unref (XMLNode *node);
gchar * xml_node_to_string (XMLNode *node);
/*
XMLNode *xml_node_get_first_child (XMLNode *node);
XMLNode *xml_node_get_next (XMLNode *node);
*/
int xml_parse (char *doc, XML *p_xml);
int xml_load (XML *xmldoc, char *filename);
int xml_save (XML *p_xmldoc, char *filename);
void xml_free (XML *p_xml);

#endif

