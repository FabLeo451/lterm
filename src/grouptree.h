
#ifndef _GROUPTREE_H
#define _GROUPTREE_H

#define MAX_CHILD_GROUPS 1000

#define GN_TYPE_FOLDER 1
#define GN_TYPE_CONNECTION 2

//#define GN_FOLDER_EXPANDED 1

#define GN_SORT_SUBTREE 1
#define GN_SORT_FOLDER_FIRST 2

struct GroupNode
  {
    int type;
    char name[256];
    int expanded;
    struct GroupNode *parent;
    struct GroupNode *child[MAX_CHILD_GROUPS];
  };

struct GroupTree
  {
    struct GroupNode root;
  };

void group_node_set_name (struct GroupNode *p_gn, char *name);
void group_node_set_type (struct GroupNode *p_gn, int type);
void group_node_init (struct GroupNode *p_gn, int type, char *name);
struct GroupNode * group_node_add_child (struct GroupNode *p_parent, int type, char *name);
struct GroupNode * group_node_find_child (struct GroupNode *p_parent, char *name);
struct GroupNode * group_node_find (struct GroupNode *p_parent, char *name);
struct GroupNode * group_node_find_child_by_position (struct GroupNode *p_parent, int pos);
void group_node_delete_child (struct GroupNode *p_parent, struct GroupNode *p_node);
void group_node_delete_all_children (struct GroupNode *p_parent);
struct GroupNode * group_node_find_by_path (struct GroupNode *p_parent, char *path, int index, int add_missing);
struct GroupNode * group_node_find_by_numeric_path (struct GroupNode *p_parent, char *numeric_path, int index);
void group_node_sort_children (struct GroupNode *p_gn, int flags);
struct GroupNode * group_node_get_child_first (struct GroupNode *p_node);
struct GroupNode * group_node_get_child_next (struct GroupNode *p_node, struct GroupNode *p_child);
struct GroupNode * group_node_move (struct GroupNode *p_parent, struct GroupNode *p_node);

void group_tree_init (struct GroupTree *p_gt);
void group_tree_release (struct GroupTree *p_gt);
struct GroupNode * group_tree_get_root (struct GroupTree *p_gt);
struct GroupNode * group_tree_get_node (struct GroupTree *p_gt, char *path);
void group_tree_get_node_path (struct GroupTree *p_gt, struct GroupNode *p_node, char *path);
struct GroupNode * group_tree_create_path (struct GroupTree *p_gt, char *path);
void group_tree_sort (struct GroupTree *p_gt, int flags);
void group_tree_dump (struct GroupTree *p_gt);

#endif

