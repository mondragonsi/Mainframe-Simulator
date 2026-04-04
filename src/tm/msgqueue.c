/*
 * IMS Transaction Manager - Message Queue Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "msgqueue.h"

/* Internal message ID counter */
static int next_msg_id = 1;

/* Initialize message queue system */
int msgqueue_init(void) {
    memset(&ims_system.input_queue, 0, sizeof(IMS_MSG_QUEUE));
    memset(&ims_system.output_queue, 0, sizeof(IMS_MSG_QUEUE));
    strncpy(ims_system.input_queue.queue_name, "INPUT", 8);
    strncpy(ims_system.output_queue.queue_name, "OUTPUT", 8);
    ims_log("INFO", "Message queue system initialized");
    return 0;
}

void msgqueue_shutdown(void) {
    /* Free all messages in input queue */
    IMS_MESSAGE *msg = ims_system.input_queue.head;
    while (msg) {
        IMS_MESSAGE *next = msg->next;
        msg_free(msg);
        msg = next;
    }
    
    /* Free all messages in output queue */
    msg = ims_system.output_queue.head;
    while (msg) {
        IMS_MESSAGE *next = msg->next;
        msg_free(msg);
        msg = next;
    }
    
    ims_log("INFO", "Message queue system shutdown");
}

/* Create a new message */
IMS_MESSAGE *msg_create(const char *txn_code, const char *lterm) {
    IMS_MESSAGE *msg = (IMS_MESSAGE *)calloc(1, sizeof(IMS_MESSAGE));
    if (!msg) {
        ims_log("ERROR", "Failed to allocate message");
        return NULL;
    }
    
    msg->id = next_msg_id++;
    msg->type = MSG_TYPE_INPUT;
    msg->priority = MSG_PRIORITY_NORMAL;
    msg->segment_count = 0;
    msg->total_length = 0;
    
    if (txn_code) {
        strncpy(msg->transaction_code, txn_code, 8);
    }
    if (lterm) {
        strncpy(msg->lterm, lterm, 8);
    }
    
    return msg;
}

/* Free a message and all its segments */
void msg_free(IMS_MESSAGE *msg) {
    if (!msg) return;
    
    for (int i = 0; i < msg->segment_count; i++) {
        if (msg->segments[i]) {
            free(msg->segments[i]);
        }
    }
    free(msg);
}

/* Add a segment to a message */
int msg_add_segment(IMS_MESSAGE *msg, const void *data, int length) {
    if (!msg || !data || length <= 0) {
        return -1;
    }
    
    if (msg->segment_count >= 100) {
        ims_log("ERROR", "Message segment limit exceeded");
        return -1;
    }
    
    char *segment_data = (char *)malloc(length);
    if (!segment_data) {
        return -1;
    }
    
    memcpy(segment_data, data, length);
    msg->segments[msg->segment_count] = segment_data;
    msg->segment_lengths[msg->segment_count] = length;
    msg->segment_count++;
    msg->total_length += length;
    
    return 0;
}

/* Add message to input queue */
int msg_enqueue_input(IMS_MESSAGE *msg) {
    if (!msg) return -1;
    
    msg->type = MSG_TYPE_INPUT;
    msg->next = NULL;
    msg->prev = ims_system.input_queue.tail;
    
    if (ims_system.input_queue.tail) {
        ims_system.input_queue.tail->next = msg;
    } else {
        ims_system.input_queue.head = msg;
    }
    ims_system.input_queue.tail = msg;
    ims_system.input_queue.count++;
    
    ims_log("DEBUG", "Message %d queued (TXN: %s, LTERM: %s)", 
            msg->id, msg->transaction_code, msg->lterm);
    
    return 0;
}

/* Add message to output queue */
int msg_enqueue_output(IMS_MESSAGE *msg) {
    if (!msg) return -1;
    
    msg->type = MSG_TYPE_OUTPUT;
    msg->next = NULL;
    msg->prev = ims_system.output_queue.tail;
    
    if (ims_system.output_queue.tail) {
        ims_system.output_queue.tail->next = msg;
    } else {
        ims_system.output_queue.head = msg;
    }
    ims_system.output_queue.tail = msg;
    ims_system.output_queue.count++;
    
    return 0;
}

/* Get next message from input queue for a transaction */
IMS_MESSAGE *msg_dequeue_input(const char *txn_code) {
    IMS_MESSAGE *msg = ims_system.input_queue.head;
    
    while (msg) {
        if (!txn_code || strcmp(msg->transaction_code, txn_code) == 0) {
            /* Remove from queue */
            if (msg->prev) {
                msg->prev->next = msg->next;
            } else {
                ims_system.input_queue.head = msg->next;
            }
            if (msg->next) {
                msg->next->prev = msg->prev;
            } else {
                ims_system.input_queue.tail = msg->prev;
            }
            ims_system.input_queue.count--;
            msg->next = msg->prev = NULL;
            return msg;
        }
        msg = msg->next;
    }
    
    return NULL;
}

/* Get message by logical terminal */
IMS_MESSAGE *msg_dequeue_by_lterm(const char *lterm) {
    IMS_MESSAGE *msg = ims_system.output_queue.head;
    
    while (msg) {
        if (strcmp(msg->lterm, lterm) == 0) {
            /* Remove from queue */
            if (msg->prev) {
                msg->prev->next = msg->next;
            } else {
                ims_system.output_queue.head = msg->next;
            }
            if (msg->next) {
                msg->next->prev = msg->prev;
            } else {
                ims_system.output_queue.tail = msg->prev;
            }
            ims_system.output_queue.count--;
            msg->next = msg->prev = NULL;
            return msg;
        }
        msg = msg->next;
    }
    
    return NULL;
}

/* Get a segment from a message */
int msg_get_segment(IMS_MESSAGE *msg, int index, void *buffer, int max_length) {
    if (!msg || !buffer || index < 0 || index >= msg->segment_count) {
        return -1;
    }
    
    int len = msg->segment_lengths[index];
    if (len > max_length) {
        len = max_length;
    }
    
    memcpy(buffer, msg->segments[index], len);
    return len;
}

/* Queue count */
int msg_queue_count(IMS_MSG_QUEUE *queue) {
    return queue ? queue->count : 0;
}

/* Display queue contents (for debugging) */
void msg_queue_display(IMS_MSG_QUEUE *queue) {
    if (!queue) return;
    
    printf("\n=== Queue: %-8s (Count: %d) ===\n", queue->queue_name, queue->count);
    printf("%-6s %-8s %-8s %-4s %-6s\n", "ID", "TXN", "LTERM", "SEGS", "LEN");
    printf("------ -------- -------- ---- ------\n");
    
    IMS_MESSAGE *msg = queue->head;
    while (msg) {
        printf("%-6d %-8s %-8s %-4d %-6d\n",
               msg->id, msg->transaction_code, msg->lterm,
               msg->segment_count, msg->total_length);
        msg = msg->next;
    }
}
