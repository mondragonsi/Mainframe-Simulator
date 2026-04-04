/*
 * IMS Transaction Manager - MPP Region Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mpp.h"
#include "msgqueue.h"

/* Internal region counter */
static int next_region_id = 1;
static IMS_MPP_REGION *mpp_regions[16];
static int mpp_region_count = 0;

int mpp_init(void) {
    memset(mpp_regions, 0, sizeof(mpp_regions));
    mpp_region_count = 0;
    ims_log("INFO", "MPP subsystem initialized");
    return 0;
}

void mpp_shutdown(void) {
    for (int i = 0; i < mpp_region_count; i++) {
        if (mpp_regions[i]) {
            mpp_destroy_region(mpp_regions[i]);
        }
    }
    mpp_region_count = 0;
    ims_log("INFO", "MPP subsystem shutdown");
}

IMS_MPP_REGION *mpp_create_region(const char *name) {
    if (mpp_region_count >= 16) {
        ims_log("ERROR", "Maximum MPP regions reached");
        return NULL;
    }
    
    IMS_MPP_REGION *region = (IMS_MPP_REGION *)calloc(1, sizeof(IMS_MPP_REGION));
    if (!region) {
        ims_log("ERROR", "Failed to allocate MPP region");
        return NULL;
    }
    
    region->base.id = next_region_id++;
    region->base.type = REGION_MPP;
    strncpy(region->base.name, name, 8);
    region->base.is_active = true;
    region->state = MPP_STATE_IDLE;
    region->wait_timeout = 30; /* Default 30 second timeout */
    
    mpp_regions[mpp_region_count++] = region;
    
    ims_log("INFO", "MPP region '%s' created (ID: %d)", name, region->base.id);
    return region;
}

void mpp_destroy_region(IMS_MPP_REGION *region) {
    if (!region) return;
    
    if (region->input_msg) {
        msg_free(region->input_msg);
    }
    if (region->output_msg) {
        msg_free(region->output_msg);
    }
    if (region->spa) {
        free(region->spa);
    }
    
    /* Remove from regions array */
    for (int i = 0; i < mpp_region_count; i++) {
        if (mpp_regions[i] == region) {
            mpp_regions[i] = mpp_regions[--mpp_region_count];
            break;
        }
    }
    
    ims_log("INFO", "MPP region '%s' destroyed", region->base.name);
    free(region);
}

int mpp_start(IMS_MPP_REGION *region) {
    if (!region) return -1;
    
    region->state = MPP_STATE_WAITING;
    region->base.is_active = true;
    
    ims_log("INFO", "MPP region '%s' started, waiting for messages", region->base.name);
    return 0;
}

int mpp_wait_for_message(IMS_MPP_REGION *region, int timeout_sec) {
    if (!region) return -1;
    
    region->state = MPP_STATE_WAITING;
    
    /* In simulation, we just check if there's a message in the queue */
    IMS_MESSAGE *msg = msg_dequeue_input(NULL);  /* Get any transaction */
    
    if (msg) {
        region->input_msg = msg;
        region->current_input_segment = 0;
        region->state = MPP_STATE_PROCESSING;
        
        /* Find transaction definition */
        for (int i = 0; i < ims_system.transaction_count; i++) {
            if (strcmp(ims_system.transactions[i].code, msg->transaction_code) == 0) {
                region->current_txn = &ims_system.transactions[i];
                break;
            }
        }
        
        /* Check if conversational and get SPA */
        if (region->current_txn && region->current_txn->is_conversational) {
            if (!region->spa) {
                region->spa = (IMS_SPA *)calloc(1, sizeof(IMS_SPA));
                region->spa->total_size = region->current_txn->spa_size;
            }
            region->base.in_conversation = true;
        }
        
        ims_log("INFO", "MPP '%s' received message for TXN '%s'", 
                region->base.name, msg->transaction_code);
        return 0;
    }
    
    return -1;  /* No message available */
}

int mpp_process_message(IMS_MPP_REGION *region) {
    if (!region || !region->input_msg) {
        return -1;
    }
    
    region->state = MPP_STATE_PROCESSING;
    
    /* Create output message container */
    region->output_msg = msg_create(
        region->input_msg->transaction_code,
        region->input_msg->lterm
    );
    
    if (!region->output_msg) {
        return -1;
    }
    
    region->output_msg->type = MSG_TYPE_OUTPUT;
    
    return 0;
}

int mpp_get_message(IMS_MPP_REGION *region, void *buffer, int max_length) {
    if (!region || !region->input_msg || !buffer) {
        return -1;
    }
    
    /* Get first segment of input message */
    region->current_input_segment = 0;
    return msg_get_segment(region->input_msg, 0, buffer, max_length);
}

int mpp_get_next_segment(IMS_MPP_REGION *region, void *buffer, int max_length) {
    if (!region || !region->input_msg || !buffer) {
        return -1;
    }
    
    region->current_input_segment++;
    if (region->current_input_segment >= region->input_msg->segment_count) {
        return -1;  /* No more segments - will set QD status */
    }
    
    return msg_get_segment(region->input_msg, region->current_input_segment, buffer, max_length);
}

int mpp_send_response(IMS_MPP_REGION *region, const void *data, int length) {
    if (!region || !data) {
        return -1;
    }
    
    if (!region->output_msg) {
        mpp_process_message(region);
    }
    
    return msg_add_segment(region->output_msg, data, length);
}

int mpp_end_transaction(IMS_MPP_REGION *region) {
    if (!region) return -1;
    
    /* Queue output message if any */
    if (region->output_msg && region->output_msg->segment_count > 0) {
        msg_enqueue_output(region->output_msg);
        region->output_msg = NULL;
    }
    
    /* Free input message */
    if (region->input_msg) {
        msg_free(region->input_msg);
        region->input_msg = NULL;
    }
    
    /* If not conversational, clear SPA */
    if (!region->base.in_conversation && region->spa) {
        memset(region->spa->data, 0, sizeof(region->spa->data));
    }
    
    region->transactions_processed++;
    region->state = MPP_STATE_WAITING;
    region->current_txn = NULL;
    
    ims_log("DEBUG", "MPP '%s' transaction completed (total: %d)", 
            region->base.name, region->transactions_processed);
    
    return 0;
}

int mpp_get_spa(IMS_MPP_REGION *region, void *buffer, int max_length) {
    if (!region || !region->spa || !buffer) {
        return -1;
    }
    
    int len = region->spa->total_size;
    if (len > max_length) len = max_length;
    
    memcpy(buffer, region->spa->data, len);
    return len;
}

int mpp_set_spa(IMS_MPP_REGION *region, const void *data, int length) {
    if (!region || !data) {
        return -1;
    }
    
    if (!region->spa) {
        region->spa = (IMS_SPA *)calloc(1, sizeof(IMS_SPA));
        region->spa->total_size = 1024;  /* Default size */
    }
    
    if (length > (int)sizeof(region->spa->data)) {
        length = sizeof(region->spa->data);
    }
    
    memcpy(region->spa->data, data, length);
    return 0;
}

void mpp_display_status(IMS_MPP_REGION *region) {
    if (!region) return;
    
    const char *state_str[] = {"IDLE", "WAITING", "PROCESSING", "TERMINATED"};
    
    printf("\n=== MPP Region: %-8s ===\n", region->base.name);
    printf("ID:          %d\n", region->base.id);
    printf("State:       %s\n", state_str[region->state]);
    printf("Active:      %s\n", region->base.is_active ? "YES" : "NO");
    printf("Conversational: %s\n", region->base.in_conversation ? "YES" : "NO");
    printf("TXNs Processed: %d\n", region->transactions_processed);
    
    if (region->current_txn) {
        printf("Current TXN: %s\n", region->current_txn->code);
    }
}
