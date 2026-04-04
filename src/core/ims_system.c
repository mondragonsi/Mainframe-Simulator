/*
 * IMS System Core - Main Implementation
 * 
 * Global IMS system context and utility functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "ims.h"

/* Global IMS system instance */
IMS_SYSTEM ims_system;

/* Log levels */
static int log_level = 1;  /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR */

/* Logging function */
void ims_log(const char *level, const char *format, ...) {
    int lvl = 1;
    if (strcmp(level, "DEBUG") == 0) lvl = 0;
    else if (strcmp(level, "INFO") == 0) lvl = 1;
    else if (strcmp(level, "WARN") == 0) lvl = 2;
    else if (strcmp(level, "ERROR") == 0) lvl = 3;
    
    if (lvl < log_level) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, 20, "%H:%M:%S", tm_info);
    
    printf("[%s] %-5s ", timestamp, level);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

/* Initialize IMS system */
int ims_init(const char *imsid) {
    memset(&ims_system, 0, sizeof(IMS_SYSTEM));
    
    if (imsid) {
        strncpy(ims_system.imsid, imsid, 4);
    } else {
        strcpy(ims_system.imsid, "IMS1");
    }
    
    /* Initialize message queues */
    strcpy(ims_system.input_queue.queue_name, "INPUT");
    strcpy(ims_system.output_queue.queue_name, "OUTPUT");
    
    ims_system.is_running = true;
    
    ims_log("INFO", "========================================");
    ims_log("INFO", "IMS Simulator %s - System ID: %s", IMS_VERSION, ims_system.imsid);
    ims_log("INFO", "========================================");
    ims_log("INFO", "IMS system initialized");
    
    return 0;
}

/* Shutdown IMS system */
void ims_shutdown(void) {
    ims_log("INFO", "IMS system shutting down...");
    
    /* Display statistics */
    ims_log("INFO", "Total DL/I calls: %d", ims_system.total_calls);
    ims_log("INFO", "Successful calls: %d", ims_system.successful_calls);
    ims_log("INFO", "Failed calls: %d", ims_system.failed_calls);
    
    /* Free all DBDs */
    for (int i = 0; i < ims_system.dbd_count; i++) {
        if (ims_system.dbds[i]) {
            free(ims_system.dbds[i]);
        }
    }
    
    /* Free all PSBs */
    for (int i = 0; i < ims_system.psb_count; i++) {
        if (ims_system.psbs[i]) {
            free(ims_system.psbs[i]);
        }
    }
    
    ims_system.is_running = false;
    ims_log("INFO", "IMS system shutdown complete");
}

/* Get status code description */
const char *ims_status_desc(IMS_STATUS status) {
    switch (status) {
        case IMS_STATUS_OK:  return "Successful completion";
        case IMS_STATUS_GA:  return "Moved to segment at different level";
        case IMS_STATUS_GK:  return "Segment at same level, different type";
        case IMS_STATUS_GB:  return "End of database reached";
        case IMS_STATUS_GE:  return "Segment not found";
        case IMS_STATUS_QC:  return "No more messages in queue";
        case IMS_STATUS_QD:  return "No more segments for message";
        case IMS_STATUS_II:  return "Segment already exists (duplicate)";
        default:             return "Unknown status code";
    }
}

/* Register a DBD */
int ims_register_dbd(IMS_DBD *dbd) {
    if (!dbd || ims_system.dbd_count >= 64) {
        return -1;
    }
    
    ims_system.dbds[ims_system.dbd_count++] = dbd;
    ims_log("INFO", "DBD '%s' registered (%s)", dbd->name, dbd->access_method);
    
    return 0;
}

/* Register a PSB */
int ims_register_psb(IMS_PSB *psb) {
    if (!psb || ims_system.psb_count >= 256) {
        return -1;
    }
    
    ims_system.psbs[ims_system.psb_count++] = psb;
    ims_log("INFO", "PSB '%s' registered (%d PCBs)", psb->name, psb->pcb_count);
    
    return 0;
}

/* Register a transaction */
int ims_register_transaction(IMS_TRANSACTION_DEF *txn) {
    if (!txn || ims_system.transaction_count >= 256) {
        return -1;
    }
    
    ims_system.transactions[ims_system.transaction_count++] = *txn;
    ims_log("INFO", "Transaction '%s' registered (PSB: %s, %s)", 
            txn->code, txn->psb_name,
            txn->is_conversational ? "CONV" : "NON-CONV");
    
    return 0;
}

/* Find DBD by name */
IMS_DBD *ims_find_dbd(const char *name) {
    for (int i = 0; i < ims_system.dbd_count; i++) {
        if (strcmp(ims_system.dbds[i]->name, name) == 0) {
            return ims_system.dbds[i];
        }
    }
    return NULL;
}

/* Find PSB by name */
IMS_PSB *ims_find_psb(const char *name) {
    for (int i = 0; i < ims_system.psb_count; i++) {
        if (strcmp(ims_system.psbs[i]->name, name) == 0) {
            return ims_system.psbs[i];
        }
    }
    return NULL;
}

/* Display system status */
void ims_display_status(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("                    IMS SYSTEM STATUS - %s                                    \n", ims_system.imsid);
    printf("================================================================================\n");
    printf("\n");
    printf(" Status:        %s\n", ims_system.is_running ? "RUNNING" : "STOPPED");
    printf(" DBDs:          %d registered\n", ims_system.dbd_count);
    printf(" PSBs:          %d registered\n", ims_system.psb_count);
    printf(" Transactions:  %d defined\n", ims_system.transaction_count);
    printf(" Regions:       %d active\n", ims_system.region_count);
    printf("\n");
    printf(" Message Queues:\n");
    printf("   Input:       %d messages\n", ims_system.input_queue.count);
    printf("   Output:      %d messages\n", ims_system.output_queue.count);
    printf("\n");
    printf(" Statistics:\n");
    printf("   Total Calls: %d\n", ims_system.total_calls);
    printf("   Successful:  %d\n", ims_system.successful_calls);
    printf("   Failed:      %d\n", ims_system.failed_calls);
    printf("\n");
    printf("================================================================================\n");
}
