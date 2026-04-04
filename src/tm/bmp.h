/*
 * IMS Transaction Manager - BMP Region
 * 
 * Batch Message Processing region simulator.
 * Handles batch-oriented database operations.
 */

#ifndef IMS_BMP_H
#define IMS_BMP_H

#include "../core/ims.h"

/* BMP region states */
typedef enum {
    BMP_STATE_IDLE,
    BMP_STATE_RUNNING,
    BMP_STATE_WAITING_MSG,   /* Wait-for-input mode */
    BMP_STATE_COMPLETED,
    BMP_STATE_ABENDED,
} BMP_STATE;

/* BMP execution modes */
typedef enum {
    BMP_MODE_DB_ONLY,        /* Database access only */
    BMP_MODE_WAIT_MSG,       /* Can read message queue */
} BMP_MODE;

/* BMP Region structure */
typedef struct {
    IMS_REGION base;
    BMP_STATE state;
    BMP_MODE mode;
    
    /* Execution tracking */
    time_t start_time;
    time_t end_time;
    int db_calls;
    int segments_processed;
    
    /* Current context */
    IMS_MESSAGE *current_msg;
    int checkpoint_count;
} IMS_BMP_REGION;

/* Initialize BMP subsystem */
int bmp_init(void);
void bmp_shutdown(void);

/* Region management */
IMS_BMP_REGION *bmp_create_region(const char *name, BMP_MODE mode);
void bmp_destroy_region(IMS_BMP_REGION *region);

/* Execution control */
int bmp_start(IMS_BMP_REGION *region, const char *psb_name);
int bmp_checkpoint(IMS_BMP_REGION *region);
int bmp_end(IMS_BMP_REGION *region);
int bmp_abend(IMS_BMP_REGION *region, const char *reason);

/* Wait-for-input mode (optional message queue access) */
int bmp_get_message(IMS_BMP_REGION *region, void *buffer, int max_length);

/* Status display */
void bmp_display_status(IMS_BMP_REGION *region);

#endif /* IMS_BMP_H */
