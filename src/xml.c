
/**
 * Copyright (C) 2009-2017 Fabio Leone
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * @file xml.c
 * @brief XML parser
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "xml.h"


typedef struct {
  gchar *key;
  gchar *value;
} KeyValuePair;

static void xml_node_free (XMLNode *node);
static XMLNode *xml_node_last_child (XMLNode *node);

static void
xml_node_free (XMLNode *node)
{
  XMLNode *l;
  GSList        *list;
  
  g_return_if_fail (node != NULL);

	for (l = node->children; l;) 
    {
		  XMLNode *next = l->next;

		  xml_node_unref (l);
		  l = next;
    }

  g_free (node->name);
  g_free (node->value);
  
  for (list = node->attributes; list; list = list->next) 
    {
      KeyValuePair *kvp = (KeyValuePair *) list->data;
      
      g_free (kvp->key);
      g_free (kvp->value);
      g_free (kvp);
    }
  
  g_slist_free (node->attributes);
  g_free (node);
}

static XMLNode *
xml_node_last_child (XMLNode *node)
{
        XMLNode *l;
        
        g_return_val_if_fail (node != NULL, NULL);

        if (!node->children) {
                return NULL;
        }
                
        l = node->children;

        while (l->next) {
                l = l->next;
        }

        return l;
}

XMLNode *
_xml_node_new (const gchar *name)
{
        XMLNode *node;

        node = g_new0 (XMLNode, 1);
        
        node->name       = g_strdup (name);
        node->value      = NULL;
	node->raw_mode   = FALSE;
        node->attributes = NULL;
        node->next       = NULL;
        node->prev       = NULL;
        node->parent     = NULL;
        node->children   = NULL;

	node->ref_count  = 1;

        return node;
}
void
_xml_node_add_child_node (XMLNode *node, XMLNode *child)
{
        XMLNode *prev;
	
        g_return_if_fail (node != NULL);

        prev = xml_node_last_child (node);
	xml_node_ref (child);

        if (prev) {
                prev->next    = child;
                child->prev   = prev;
        } else {
                node->children = child;
        }
        
        child->parent = node;
}

/**
 * xml_node_get_value:
 * @node: an #XMLNode
 * 
 * Retrieves the value of @node.
 * 
 * Return value: The value of the node or %NULL.
 **/
const gchar *
xml_node_get_value (XMLNode *node)
{
	g_return_val_if_fail (node != NULL, NULL);
	
	return node->value;
}

/**
 * xml_node_set_value:
 * @node: an #XMLNode
 * @value: the new value.
 * 
 * Sets the value of @node. If a previous value is set it will be freed.
 **/
void
xml_node_set_value (XMLNode *node, const gchar *value)
{
        g_return_if_fail (node != NULL);
       
        g_free (node->value);
	
        if (!value) {
                node->value = NULL;
                return;
        }

        node->value = g_strdup (value);
}

/**
 * xml_node_add_child:
 * @node: an #XMLNode
 * @name: the name of the new child
 * @value: value of the new child
 * 
 * Add a child node with @name and value set to @value. 
 * 
 * Return value: the newly created child
 **/
XMLNode *
xml_node_add_child (XMLNode *node, 
			   const gchar   *name, 
			   const gchar   *value)
{
	XMLNode *child;
	
        g_return_val_if_fail (node != NULL, NULL);
        g_return_val_if_fail (name != NULL, NULL);

	child = _xml_node_new (name);

	xml_node_set_value (child, value);
	_xml_node_add_child_node (node, child);
	xml_node_unref (child);

	return child;
}

/**
 * xml_node_set_attributes:
 * @node: an #XMLNode
 * @name: the first attribute, should be followed by a string with the value
 * @Varargs: The rest of the name/value pairs
 * 
 * Sets a list of attributes. The arguments should be names and corresponding 
 * value and needs to be ended with %NULL.
 **/
void
xml_node_set_attributes  (XMLNode *node,
				 const gchar   *name,
				 ...)
{
	va_list args;
	
        g_return_if_fail (node != NULL);

	for (va_start (args, name); 
	     name; 
	     name = (const gchar *) va_arg (args, gpointer)) {
		const gchar *value;

		value = (const gchar *) va_arg (args, gpointer);

		xml_node_set_attribute (node, name, value);
		
	}

	va_end (args);
}

/**
 * xml_node_set_attribute:
 * @node: an #XMLNode
 * @name: name of attribute
 * @value: value of attribute.
 * 
 * Sets the attribute @name to @value.
 **/
void
xml_node_set_attribute (XMLNode *node,
			       const gchar   *name,
			       const gchar   *value)
{
	gboolean  found = FALSE; 
	GSList   *l;

        g_return_if_fail (node != NULL);
        g_return_if_fail (name != NULL);
        g_return_if_fail (value != NULL);

	for (l = node->attributes; l; l = l->next) {
		KeyValuePair *kvp = (KeyValuePair *) l->data;
                
		if (strcmp (kvp->key, name) == 0) {
			g_free (kvp->value);
			kvp->value = g_strdup (value);
			found = TRUE;
			break;
		}
	}
	
	if (!found) {
		KeyValuePair *kvp;
	
		kvp = g_new0 (KeyValuePair, 1);                
		kvp->key = g_strdup (name);
		kvp->value = g_strdup (value);
		
		node->attributes = g_slist_prepend (node->attributes, kvp);
	}
}

/**
 * xml_node_get_attribute:
 * @node: an #XMLNode
 * @name: the attribute name
 * 
 * Fetches the attribute @name from @node.
 * 
 * Return value: the attribute value or %NULL if not set
 **/
const gchar *
xml_node_get_attribute (XMLNode *node, const gchar *name)
{
        GSList      *l;
        const gchar *ret_val = NULL;

        g_return_val_if_fail (node != NULL, NULL);
        g_return_val_if_fail (name != NULL, NULL);

        for (l = node->attributes; l; l = l->next) {
                KeyValuePair *kvp = (KeyValuePair *) l->data;
                
                if (strcmp (kvp->key, name) == 0) {
                        ret_val = kvp->value;
                }
        }
        
        return ret_val;
}

/**
 * xml_node_get_child:
 * @node: an #XMLNode
 * @child_name: the childs name
 * 
 * Fetches the child @child_name from @node. If child is not found as an 
 * immediate child of @node %NULL is returned.
 * 
 * Return value: the child node or %NULL if not found
 **/
XMLNode *
xml_node_get_child (XMLNode *node, const gchar *child_name)
{
	XMLNode *l;

	for (l = node->children; l; l = l->next)
    {
		  if (!strcmp (l->name, child_name)) 
        {
			    return (l);
		    }
	  }

	return NULL;
}

/**
 * xml_node_find_child:
 * @node: A #XMLNode
 * @child_name: The name of the child to find
 * 
 * Locates a child among all children of @node. The entire tree will be search 
 * until a child with name @child_name is located. 
 * 
 * Return value: the located child or %NULL if not found
 **/
XMLNode * 
xml_node_find_child (XMLNode *node,
			    const gchar   *child_name)
{
        XMLNode *l;
        XMLNode *ret_val = NULL;

        g_return_val_if_fail (node != NULL, NULL);
        g_return_val_if_fail (child_name != NULL, NULL);

        for (l = node->children; l; l = l->next) {
                if (strcmp (l->name, child_name) == 0) {
                        return l;
                }
                if (l->children) {
                        ret_val = xml_node_find_child (l, child_name);
                        if (ret_val) {
                                return ret_val;
                        }
                }
        }

        return NULL;
}

/**
 * xml_node_get_raw_mode:
 * @node: an #XMLNode
 * 
 * Checks if the nodes value should be sent as raw mode. 
 *
 * Return value: %TRUE if nodes value should be sent as is and %FALSE if the value will be escaped before sending.
 **/
gboolean
xml_node_get_raw_mode (XMLNode *node)
{
	g_return_val_if_fail (node != NULL, FALSE);

	return node->raw_mode;
}

/** 
 * xml_node_set_raw_mode:
 * @node: an #XMLNode
 * @raw_mode: boolean specifying if node value should be escaped or not.
 *
 * Set @raw_mode to %TRUE if you don't want to escape the value. You need to make sure the value is valid XML yourself.
 **/
void
xml_node_set_raw_mode (XMLNode *node, gboolean raw_mode)
{
	g_return_if_fail (node != NULL);

	node->raw_mode = raw_mode;	
}

/**
 * xml_node_ref:
 * @node: an #XMLNode
 * 
 * Adds a reference to @node.
 * 
 * Return value: the node
 **/
XMLNode *
xml_node_ref (XMLNode *node)
{
	g_return_val_if_fail (node != NULL, NULL);
	
	node->ref_count++;
       
	return node;
}

/**
 * xml_node_unref:
 * @node: an #XMLNode
 * 
 * Removes a reference from @node. When no more references are present the
 * node is freed. When freed xml_node_unref() will be called on all
 * children. If caller needs to keep references to the children a call to 
 * xml_node_ref() needs to be done before the call to 
 *lm_message_unref().
 **/
void
xml_node_unref (XMLNode *node)
{
	g_return_if_fail (node != NULL);
	
	node->ref_count--;
	
	if (node->ref_count == 0) {
		xml_node_free (node);
	}
}

int g_indent = 0;

/**
 * xml_node_to_string:
 * @node: an #XMLNode
 * 
 * Returns an XML string representing the node. This is what is sent over the
 * wire. This is used internally Loudmouth and is external for debugging 
 * purposes.
 * 
 * Return value: an XML string representation of @node
 **/
gchar *
xml_node_to_string (XMLNode *node)
{
	GString       *ret;
	GSList        *l;
	XMLNode *child;

	g_return_val_if_fail (node != NULL, NULL);
	
	if (node->name == NULL) {
		return g_strdup ("");
	}
	

  ret = g_string_new ("<");
/*  
  ret = g_string_new ("");
  
  int i;
  
  for (i=0; i<g_indent; i++)
    g_string_append_c (ret, ' ');
    
  g_string_append (ret, "<");
*/

	g_string_append (ret, node->name);
	
	for (l = node->attributes; l; l = l->next) {
		KeyValuePair *kvp = (KeyValuePair *) l->data;

		if (node->raw_mode == FALSE) {
			gchar *escaped;

			escaped = g_markup_escape_text (kvp->value, -1);
			g_string_append_printf (ret, " %s=\"%s\"", 
						kvp->key, escaped);
			g_free (escaped);
		} else {
			g_string_append_printf (ret, " %s=\"%s\"", 
						kvp->key, kvp->value);
		}
		
	}
	
	g_string_append_c (ret, '>');
	
	if (node->value) {
		gchar *tmp;

		if (node->raw_mode == FALSE) {
			tmp = g_markup_escape_text (node->value, -1);
			g_string_append (ret,  tmp);
			g_free (tmp);
		} else {
			g_string_append (ret, node->value);
		}
	} 
    
  g_indent += 2;

	for (child = node->children; child; child = child->next) {
		gchar *child_str = xml_node_to_string (child);
/*    
    int i;
    
    for (i=0; i<g_indent; i++)
		  g_string_append_c (ret, ' ');
*/      
		g_string_append (ret, child_str);
		g_free (child_str);
	}
	
  g_indent -= 2;

	g_string_append_printf (ret, "</%s>\n", node->name);
	
	return g_string_free (ret, FALSE);
}

/*
XMLNode *
xml_node_get_first_child (XMLNode *node)
{
	return (node->children);
}

XMLNode *
xml_node_get_next (XMLNode *node)
{
	return (node->next);
}
*/

void
xml_init (XML *p_xml)
{
  memset (p_xml, 0, sizeof (XML));
}

static void
xml_parser_start_element_handler (GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names,
                                  const gchar **attribute_values, gpointer user_data, GError **error)
{
  int i;

  //log_debug("%s\n", element_name);

  XML *p_xml = (XML *) user_data;

	if (!p_xml->cur_root) 
    {
		  p_xml->cur_root = _xml_node_new (element_name);
		  p_xml->cur_node = p_xml->cur_root;

      //log_debug("root element: %s\n", p_xml->cur_root->name);
	  } 
  else 
    {
		  XMLNode *parent_node;
		
		  parent_node = p_xml->cur_node;
		
		  p_xml->cur_node = _xml_node_new (element_name);
		  _xml_node_add_child_node (parent_node, p_xml->cur_node);
	  }

	for (i = 0; attribute_names[i]; ++i) 
    {
		  //log_debug("attribute: %s=%s\n", attribute_names[i], attribute_values[i]);
		
		  xml_node_set_attributes (p_xml->cur_node, attribute_names[i], attribute_values[i], NULL);
	  }
}

static void
xml_parser_end_element_handler (GMarkupParseContext *context, const gchar *element_name, gpointer user_data, GError **error)
{
  //log_debug("%s\n", element_name);

  XML *p_xml = (XML *) user_data;

  if (!p_xml->cur_node) 
    {
      /* FIXME */
      return;
    }

	if (strcmp (p_xml->cur_node->name, element_name))
    {
		  g_warning ("Trying to close node that isn't open: %s", element_name);
		  return;
	  }

	p_xml->cur_node = p_xml->cur_node->parent;
}

static void
xml_parser_error_handler (GMarkupParseContext *context, GError *error, gpointer user_data)
{
  XML *p_xml = (XML *) user_data;

  memcpy (&p_xml->error, error, sizeof (GError));
}

static void
xml_parser_text_handler (GMarkupParseContext *context, const gchar *text, gsize text_len, gpointer user_data, GError **error)
{
  //log_debug("%s\n", text);

  XML *p_xml = (XML *) user_data;

	if (p_xml->cur_node && strcmp (text, "")) {
		xml_node_set_value (p_xml->cur_node, text);
	} 
}

static void
xml_parser_passthrough_handler (GMarkupParseContext *context, const gchar *passthrough_text, gsize text_len, gpointer user_data, GError **error)
{
#ifdef DEBUG
  //printf ("PASS '%.*s'\n", (int)text_len, passthrough_text);
#endif
}

static const GMarkupParser xml_parser = {
  xml_parser_start_element_handler,
  xml_parser_end_element_handler,
  xml_parser_text_handler,
  xml_parser_passthrough_handler,
  xml_parser_error_handler
};

int
xml_parse (char *doc, XML *p_xml)
{
  

  xml_init (p_xml);
  
  p_xml->context = g_markup_parse_context_new (&xml_parser, G_MARKUP_TREAT_CDATA_AS_TEXT, p_xml, NULL);

  if (!g_markup_parse_context_parse (p_xml->context, doc, strlen (doc), NULL))
    {
      g_markup_parse_context_free (p_xml->context);
      return;
    }

  g_markup_parse_context_free (p_xml->context);

  
  
  return (p_xml->error.code);
}

int
xml_load (XML *p_xmldoc, char *filename)
{
  int i, rc = 0;
  char *xml;
  char line[2048];
  FILE *fp;
  //XML xmldoc;

  /* put xml content into a string */
  
  fp = fopen (filename, "r");
  
  if (fp == NULL)
    return (1);
  
  xml = (char *) malloc (2048);
  strcpy (xml, "");
  
  while (fgets (line, 1024, fp) != 0)
    {
      if (strlen (xml) + strlen (line) > sizeof (xml))
        xml = (char *) realloc (xml, strlen (xml) + strlen (line) + 1);
      
      strcat (xml, line);
    }
    
  fclose (fp);

  xml_parse (xml, p_xmldoc);
  free (xml);

  return (p_xmldoc->error.code);
}

int
xml_save (XML *p_xmldoc, char *filename)
{
  int rc = 0;
  gchar *xmltext;
  FILE *fp;

  fp = fopen (filename, "w");
  
  if (fp == NULL)
    return (1);
  
  g_indent = 0;
  xmltext = xml_node_to_string (p_xmldoc->cur_root);
  
  fprintf (fp, "%s\n", xmltext);

  fclose (fp);
  
  return (rc);
}

void
xml_free (XML *p_xml)
{
  xml_node_free (p_xml->cur_root);
}


