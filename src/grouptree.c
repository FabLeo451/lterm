
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
 * @file grouptree.c
 * @brief
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "connection_list.h"
#include "grouptree.h"
#include "main.h"

extern struct Connection_List conn_list;

/**
 * group_node_set_name() - sets the name of a node
 */
void
group_node_set_name (struct GroupNode *p_gn, char *name)
{
  strcpy (p_gn->name, name);
}

void 
group_node_set_type (struct GroupNode *p_gn, int type)
{
  p_gn->type = type;
}

/**
 * group_node_init() - initializes a node
 */
void
group_node_init (struct GroupNode *p_gn, int type, char *name)
{
  int i;

  memset (p_gn, 0, sizeof (struct GroupNode));

  group_node_set_name (p_gn, name);
  group_node_set_type (p_gn, type);

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      p_gn->child[i] = NULL;
    }
}

/**
 * group_node_add_child() - adds a child to a node 
 */
struct GroupNode *
group_node_add_child (struct GroupNode *p_parent, int type, char *name)
{
  int i;
  struct GroupNode *p_newchild = NULL;

  /* find next available slot */
  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i] == 0)
        break;
    }
    
  if (i == MAX_CHILD_GROUPS)
    return (NULL);

  p_newchild = (struct GroupNode *) malloc (sizeof (struct GroupNode));
  group_node_init (p_newchild, type, name);
  p_parent->child[i] = p_newchild;
  p_newchild->parent = p_parent;

  return (p_newchild);
}

/**
 * group_node_find_child() - finds a child among p_parent children
 */
struct GroupNode *
group_node_find_child (struct GroupNode *p_parent, char *name)
{
  int i, n=0;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i])
        {
          if (!strcmp (p_parent->child[i]->name, name))
            {
              return (p_parent->child[i]);
            }
        }

      n ++;
    }

  return (NULL);
}

/**
 * group_node_find() - finds a given connection
 */
struct GroupNode *
group_node_find (struct GroupNode *p_parent, char *name)
{
  struct GroupNode *p_node;
  int i;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i])
        {
          if (p_parent->child[i]->type == GN_TYPE_FOLDER)
            {
              //log_debug ("search folder %s\n", p_parent->child[i]->name);
              
              if (p_node = group_node_find (p_parent->child[i], name))
                return (p_node);
            }
          else if (!strcmp (p_parent->child[i]->name, name))
            {
              return (p_parent->child[i]);
            }
        }
    }

  return (NULL);
}
/**
 * group_node_find_child_by_position() - finds the nth child among p_parent children
 */
struct GroupNode *
group_node_find_child_by_position (struct GroupNode *p_parent, int pos)
{
  int i, n=0;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i])
        {
          if (pos == n)
            {
              return (p_parent->child[i]);
            }

          n ++;
        }

    }

  return (NULL);
}


/**
 * group_node_delete_child() - deletes one child of a node
 */
void
group_node_delete_child (struct GroupNode *p_parent, struct GroupNode *p_node)
{
  int i;

  group_node_delete_all_children (p_node);

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i] == p_node)
        {
          if (p_node->type == GN_TYPE_CONNECTION)
            cl_remove (&conn_list, p_node->name);

          free (p_node);
          p_parent->child[i] = NULL;
        }
    }
}

/**
 * group_node_delete_all_children() - deletes all the children of a node
 */
void
group_node_delete_all_children (struct GroupNode *p_parent)
{
  int i;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_parent->child[i])
        {
          group_node_delete_all_children (p_parent->child[i]);

          if (p_parent->child[i]->type == GN_TYPE_CONNECTION)
            cl_remove (&conn_list, p_parent->child[i]->name);

          free (p_parent->child[i]);
          p_parent->child[i] = NULL;
        }
    }
}

/**
 * group_node_find_by_path() - finds a node following a given path, or creates the path
 * @param[in] p_parent parent of the subtree to be scanned
 * @param[in] path path to search for in the form "folder1/folder2/..."
 * @param[in] index position of node in path string, must be 1 in the first call
 * @param[in] add_missing 1 if missing nodes should be created, 0 if not
 */
struct GroupNode *
group_node_find_by_path (struct GroupNode *p_parent, char *path, int index, int add_missing)
{
  char nodename[1024];
  char s_tmp[64];
  struct GroupNode *p_node;

  list_get_nth (path, index, '/', nodename);

  //log_debug ("node is %s; index = %d; child nodename = %s\n", p_parent->name, index, nodename);

  if (list_count (path, '/') < index)
    return (p_parent);

  p_node = group_node_find_child (p_parent, nodename);

  if (p_node == NULL)
    {
      if (add_missing)
        p_node = group_node_add_child (p_parent, GN_TYPE_FOLDER, nodename);
    }

  if (p_node)
    return (group_node_find_by_path (p_node, path, index+1, add_missing));
  else
    return (NULL);
}
/**
 * group_node_find_by_numeric_path() - finds a node following a given numeric path
 * @param[in] p_parent parent of the subtree to be scanned (root for the first call);
 * @param[in] numeric_path path to search for in the gtk string form "3:0:2:..." (0 for the first child)
 * @param[in] index position of node in path string, must be 1 in the first call
 */
struct GroupNode *
group_node_find_by_numeric_path (struct GroupNode *p_parent, char *numeric_path, int index)
{
  char nodepos[16];
  char s_tmp[64];
  struct GroupNode *p_node;
  int pos;

  list_get_nth (numeric_path, index, ':', nodepos);

  //log_debug ("node is %s; index = %d; child nodename = %s\n", p_parent->name, index, nodename);

  if (list_count (numeric_path, ':') < index)
    return (p_parent);

  pos = atoi (nodepos);

  p_node = group_node_find_child_by_position (p_parent, pos);

  if (p_node)
    return (group_node_find_by_numeric_path (p_node, numeric_path, index+1));
  else
    return (NULL);
}

/**
 * group_node_sort_children() - sorts the children of the given node
 * @param[in] p_gn parent node
 * @param[in] flags sorting options
 */
void
group_node_sort_children (struct GroupNode *p_gn, int flags)
{
  int i, j, min;
  struct GroupNode *p_node_tmp;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_gn->child[i] == NULL)
        continue;

      min = i;

      for (j=i+1; j<MAX_CHILD_GROUPS; j++)
        {
          if (p_gn->child[j] == NULL)
            continue;

          if (strcasecmp (p_gn->child[j]->name, p_gn->child[min]->name) < 0)
            min = j;
        }

      if (min > i)
        {
          p_node_tmp = p_gn->child[i];
          p_gn->child[i] = p_gn->child[min];
          p_gn->child[min] = p_node_tmp;
        }
    }

  if (flags & GN_SORT_SUBTREE)
    {
      for (i=0; i<MAX_CHILD_GROUPS; i++)
        {
          if (p_gn->child[i])
            group_node_sort_children (p_gn->child[i], flags);
        }
    }
}

/**
 * group_node_get_child_first() - returns the first child
 * @param[in] p_node parent node
 */
struct GroupNode *
group_node_get_child_first (struct GroupNode *p_node)
{
  int i;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->child[i])
        return (p_node->child[i]);
    }

  return (NULL);
}

/**
 * group_node_get_child_next() - returns the node following p_child among p_node children
 * @param[in] p_node parent node
 * @param[in] p_child 
 */
struct GroupNode *
group_node_get_child_next (struct GroupNode *p_node, struct GroupNode *p_child)
{
  struct GroupNode *p_child_next = NULL;
  int i, start;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->child[i] == p_child)
        break;
    }

  if (i == MAX_CHILD_GROUPS)
    return (NULL);

  start = i + 1;

  for (i=start; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->child[i])
        return (p_node->child[i]);
    }

  return (NULL);
}

/**
 * group_node_move() - moves a node under a parent
 * @param[in] p_new_parent new parent node
 * @param[in] p_node node to be moved
 */
struct GroupNode *
group_node_move (struct GroupNode *p_new_parent, struct GroupNode *p_node)
{
  struct GroupNode *p_node_return = NULL;
  int i;

  /* reset original parent slot */
  
  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->parent->child[i] == p_node)
        {
          log_debug ("%s.child[%d] reset\n", p_node->parent->name, i);
          p_node->parent->child[i] = 0;
          break;
        }
    }

  /* find a free slot under new parent node */
  
  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_new_parent->child[i] == NULL)
        {
          p_new_parent->child[i] = p_node;
          p_node->parent = p_new_parent;
          p_node_return = p_node;
          log_debug ("%s.child[%d] new\n", p_node->parent->name, i);
          break;
        }
    }
    
  return (p_node_return);
}

/**
 * group_tree_init() - group tree initialization
 */
void
group_tree_init (struct GroupTree *p_gt)
{
  group_node_init (&p_gt->root, GN_TYPE_FOLDER, "root");
}

/**
 * group_tree_release() - deletes the tree and release memory
 */
void
group_tree_release (struct GroupTree *p_gt)
{
  group_node_delete_all_children (&p_gt->root);
}

/**
 * group_tree_get_root() - returns the root node
 */
struct GroupNode *
group_tree_get_root (struct GroupTree *p_gt)
{
  return (&p_gt->root);
}

/**
 * group_tree_get_node() - finds a node following a given path
 */
struct GroupNode *
group_tree_get_node (struct GroupTree *p_gt, char *path)
{
  return (group_node_find_by_path (&p_gt->root, path, 1, 0));
}

/**
 * group_tree_get_node_path() - returns node path in a gtk string form "2:5:3..."
 */
void
group_tree_get_node_path (struct GroupTree *p_gt, struct GroupNode *p_node, char *path)
{
  int i, pos=0;
  char tmp_s[32];

  if (p_node->parent == NULL)
    return;

  group_tree_get_node_path (p_gt, p_node->parent, path);

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->parent->child[i])
        {
          if (p_node->parent->child[i] == p_node)
            {
              sprintf (tmp_s, "%d", pos);

              if (p_node->parent != &p_gt->root)
                strcat (path, ":");

              strcat (path, tmp_s);

              return;
            }
          else
            pos ++;
        }
    }
}

/**
 * group_tree_create_path() - adds nodes in order to create the given path
 */
struct GroupNode *
group_tree_create_path (struct GroupTree *p_gt, char *path)
{
  return (group_node_find_by_path (&p_gt->root, path, 1, 1));
}

/**
 * group_tree_sort() - sorts the node of the tree
 * @param[in] p_gt pointer to the group tree
 * @param[in] flags sorting options
 */
void
group_tree_sort (struct GroupTree *p_gt, int flags)
{
 flags |= GN_SORT_SUBTREE;
 group_node_sort_children (&p_gt->root, flags);
}

#ifdef DEBUG
int gt_indent = 0;

void
group_subtree_dump (struct GroupNode *p_node)
{
  int i;

  printf ("%*s %s (%s)\n", gt_indent, " ", p_node->name, p_node->parent != NULL ? p_node->parent->name : "");

  gt_indent += 2;

  for (i=0; i<MAX_CHILD_GROUPS; i++)
    {
      if (p_node->child[i])
        {
          group_subtree_dump (p_node->child[i]);
        }
    }

  gt_indent -= 2;
}

void
group_tree_dump (struct GroupTree *p_gt)
{
  group_subtree_dump (&p_gt->root);
}
#endif

