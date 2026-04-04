/*
 * IMS Transaction Manager - MPP Region
 * 
 * Message Processing Program region simulator.
 * Handles online transaction processing from terminals.
 */

#ifndef IMS_MPP_H
#define IMS_MPP_H

#include "../core/ims.h"

/* MPP region states */
typedef enum {
    MPP_STATE_IDLE,
    MPP_STATE_WAITING,
    MPP_STATE_PROCESSING,
    MPP_STATE_TERMINATED,
} MPP_STATE;

/* MPP Region structure */
typedef struct {
    IMS_REGION base;
    MPP_STATE state;
    int transactions_processed;
    int wait_timeout;              /* Seconds to wait for message */
    
    /* Current transaction context */
    IMS_TRANSACTION_DEF *current_txn;
    IMS_MESSAGE *input_msg;
    IMS_MESSAGE *output_msg;
    int current_input_segment;
    
    /* SPA for conversational */
    IMS_SPA *spa;
} IMS_MPP_REGION;

/* Initialize MPP subsystem */
int mpp_init(void);
void mpp_shutdown(void);

/* Region management */
IMS_MPP_REGION *mpp_create_region(const char *name);
void mpp_destroy_region(IMS_MPP_REGION *region);

/* Transaction processing */
int mpp_start(IMS_MPP_REGION *region);
int mpp_wait_for_message(IMS_MPP_REGION *region, int timeout_sec);
int mpp_process_message(IMS_MPP_REGION *region);
int mpp_send_response(IMS_MPP_REGION *region, const void *data, int length);
int mpp_end_transaction(IMS_MPP_REGION *region);

/* Get input message (for DL/I GU call to I/O PCB) */
int mpp_get_message(IMS_MPP_REGION *region, void *buffer, int max_length);
int mpp_get_next_segment(IMS_MPP_REGION *region, void *buffer, int max_length);

/* SPA operations */
int mpp_get_spa(IMS_MPP_REGION *region, void *buffer, int max_length);
int mpp_set_spa(IMS_MPP_REGION *region, const void *data, int length);

/* Status display */
void mpp_display_status(IMS_MPP_REGION *region);

#endif /* IMS_MPP_H */
