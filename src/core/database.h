/*
 * IMS Database Manager - Hierarchical Database Engine
 */

#ifndef IMS_DATABASE_H
#define IMS_DATABASE_H

#include "../core/ims.h"

/* Create a new DBD */
IMS_DBD *dbd_create(const char *name, const char *access_method);

/* Add segment definition to DBD */
IMS_SEGMENT_DEF *dbd_add_segment(IMS_DBD *dbd, const char *name, 
                                  const char *parent_name, int max_length);

/* Add field to segment definition */
int dbd_add_field(IMS_SEGMENT_DEF *seg, const char *name, 
                  int offset, int length, bool is_key, char type);

/* Create segment instance */
IMS_SEGMENT *segment_create(IMS_SEGMENT_DEF *def, const void *data, int length);
void segment_free(IMS_SEGMENT *seg);

/* Hierarchy navigation */
IMS_SEGMENT *segment_get_root(IMS_DBD *dbd);
IMS_SEGMENT *segment_get_parent(IMS_SEGMENT *seg);
IMS_SEGMENT *segment_get_first_child(IMS_SEGMENT *seg);
IMS_SEGMENT *segment_get_next_sibling(IMS_SEGMENT *seg);
IMS_SEGMENT *segment_get_next_twin(IMS_SEGMENT *seg);

/* Add segment to hierarchy */
int segment_add_child(IMS_SEGMENT *parent, IMS_SEGMENT *child);
int segment_add_twin(IMS_SEGMENT *seg, IMS_SEGMENT *twin);

/* Get field value from segment */
int segment_get_field(IMS_SEGMENT *seg, const char *field_name, 
                      void *buffer, int max_length);

/* Set field value in segment */
int segment_set_field(IMS_SEGMENT *seg, const char *field_name,
                      const void *data, int length);

/* Search for segment */
IMS_SEGMENT *segment_find(IMS_SEGMENT *start, IMS_SSA *ssa);

/* Display functions */
void dbd_display(IMS_DBD *dbd);
void segment_display(IMS_SEGMENT *seg, int indent);

/* Load sample HOSPITAL database */
int load_hospital_database(void);

#endif /* IMS_DATABASE_H */
