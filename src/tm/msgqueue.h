/*
 * IMS Transaction Manager - Message Queue
 * 
 * Implements the IMS message queueing system for transaction processing.
 * Messages are queued by priority and processed FIFO within each priority level.
 */

#ifndef IMS_MSGQUEUE_H
#define IMS_MSGQUEUE_H

#include "../core/ims.h"

/* Message priority levels (1 = highest) */
#define MSG_PRIORITY_HIGH   1
#define MSG_PRIORITY_NORMAL 2
#define MSG_PRIORITY_LOW    3

/* Initialize message queue system */
int msgqueue_init(void);
void msgqueue_shutdown(void);

/* Queue operations */
IMS_MESSAGE *msg_create(const char *txn_code, const char *lterm);
void msg_free(IMS_MESSAGE *msg);

/* Add segment to message */
int msg_add_segment(IMS_MESSAGE *msg, const void *data, int length);

/* Queue a message for processing */
int msg_enqueue_input(IMS_MESSAGE *msg);
int msg_enqueue_output(IMS_MESSAGE *msg);

/* Get next message for a transaction */
IMS_MESSAGE *msg_dequeue_input(const char *txn_code);
IMS_MESSAGE *msg_dequeue_by_lterm(const char *lterm);

/* Get message segments */
int msg_get_segment(IMS_MESSAGE *msg, int index, void *buffer, int max_length);
int msg_get_next_segment(IMS_MESSAGE *msg, void *buffer, int max_length);

/* Queue status */
int msg_queue_count(IMS_MSG_QUEUE *queue);
void msg_queue_display(IMS_MSG_QUEUE *queue);

#endif /* IMS_MSGQUEUE_H */
