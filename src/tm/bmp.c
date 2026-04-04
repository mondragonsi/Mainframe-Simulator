/*
 * IMS Transaction Manager - BMP Region Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bmp.h"
#include "msgqueue.h"

static int bmp_next_id = 1;
static IMS_BMP_REGION *bmp_regions[16];
static int bmp_region_count = 0;

int bmp_init(void) {
    memset(bmp_regions, 0, sizeof(bmp_regions));
    bmp_region_count = 0;
    ims_log("INFO", "BMP subsystem initialized");
    return 0;
}

void bmp_shutdown(void) {
    for (int i = 0; i < bmp_region_count; i++) {
        if (bmp_regions[i]) {
            bmp_destroy_region(bmp_regions[i]);
        }
    }
    bmp_region_count = 0;
    ims_log("INFO", "BMP subsystem shutdown");
}

IMS_BMP_REGION *bmp_create_region(const char *name, BMP_MODE mode) {
    if (bmp_region_count >= 16) {
        ims_log("ERROR", "Maximum BMP regions reached");
        return NULL;
    }
    
    IMS_BMP_REGION *region = (IMS_BMP_REGION *)calloc(1, sizeof(IMS_BMP_REGION));
    if (!region) {
        return NULL;
    }
    
    region->base.id = bmp_next_id++;
    region->base.type = REGION_BMP;
    strncpy(region->base.name, name, 8);
    region->mode = mode;
    region->state = BMP_STATE_IDLE;
    
    bmp_regions[bmp_region_count++] = region;
    
    ims_log("INFO", "BMP region '%s' created (mode: %s)", 
            name, mode == BMP_MODE_WAIT_MSG ? "WAIT-MSG" : "DB-ONLY");
    
    return region;
}

void bmp_destroy_region(IMS_BMP_REGION *region) {
    if (!region) return;
    
    if (region->current_msg) {
        msg_free(region->current_msg);
    }
    
    for (int i = 0; i < bmp_region_count; i++) {
        if (bmp_regions[i] == region) {
            bmp_regions[i] = bmp_regions[--bmp_region_count];
            break;
        }
    }
    
    ims_log("INFO", "BMP region '%s' destroyed", region->base.name);
    free(region);
}

int bmp_start(IMS_BMP_REGION *region, const char *psb_name) {
    if (!region) return -1;
    
    /* Schedule PSB */
    IMS_PSB *psb = NULL;
    for (int i = 0; i < ims_system.psb_count; i++) {
        if (strcmp(ims_system.psbs[i]->name, psb_name) == 0) {
            psb = ims_system.psbs[i];
            break;
        }
    }
    
    if (!psb) {
        ims_log("ERROR", "PSB '%s' not found", psb_name);
        return -1;
    }
    
    region->base.current_psb = psb;
    region->base.is_active = true;
    region->state = BMP_STATE_RUNNING;
    region->start_time = time(NULL);
    region->db_calls = 0;
    region->segments_processed = 0;
    region->checkpoint_count = 0;
    
    ims_log("INFO", "BMP '%s' started with PSB '%s'", region->base.name, psb_name);
    
    return 0;
}

int bmp_checkpoint(IMS_BMP_REGION *region) {
    if (!region || region->state != BMP_STATE_RUNNING) {
        return -1;
    }
    
    region->checkpoint_count++;
    
    ims_log("INFO", "BMP '%s' checkpoint #%d (calls: %d, segments: %d)", 
            region->base.name, region->checkpoint_count,
            region->db_calls, region->segments_processed);
    
    return 0;
}

int bmp_end(IMS_BMP_REGION *region) {
    if (!region) return -1;
    
    region->end_time = time(NULL);
    region->state = BMP_STATE_COMPLETED;
    region->base.is_active = false;
    
    double elapsed = difftime(region->end_time, region->start_time);
    
    ims_log("INFO", "BMP '%s' completed - Elapsed: %.0fs, Calls: %d, Segments: %d",
            region->base.name, elapsed, region->db_calls, region->segments_processed);
    
    return 0;
}

int bmp_abend(IMS_BMP_REGION *region, const char *reason) {
    if (!region) return -1;
    
    region->state = BMP_STATE_ABENDED;
    region->base.is_active = false;
    region->end_time = time(NULL);
    
    ims_log("ERROR", "BMP '%s' ABEND: %s", region->base.name, reason ? reason : "Unknown");
    
    return 0;
}

int bmp_get_message(IMS_BMP_REGION *region, void *buffer, int max_length) {
    if (!region || !buffer) return -1;
    
    if (region->mode != BMP_MODE_WAIT_MSG) {
        ims_log("WARN", "BMP '%s' not in wait-for-input mode", region->base.name);
        return -1;
    }
    
    region->state = BMP_STATE_WAITING_MSG;
    
    IMS_MESSAGE *msg = msg_dequeue_input(NULL);
    if (!msg) {
        return -1;  /* No message available */
    }
    
    region->current_msg = msg;
    region->state = BMP_STATE_RUNNING;
    
    return msg_get_segment(msg, 0, buffer, max_length);
}

void bmp_display_status(IMS_BMP_REGION *region) {
    if (!region) return;
    
    const char *state_str[] = {"IDLE", "RUNNING", "WAIT-MSG", "COMPLETED", "ABENDED"};
    const char *mode_str[] = {"DB-ONLY", "WAIT-MSG"};
    
    printf("\n=== BMP Region: %-8s ===\n", region->base.name);
    printf("ID:          %d\n", region->base.id);
    printf("State:       %s\n", state_str[region->state]);
    printf("Mode:        %s\n", mode_str[region->mode]);
    printf("DB Calls:    %d\n", region->db_calls);
    printf("Segments:    %d\n", region->segments_processed);
    printf("Checkpoints: %d\n", region->checkpoint_count);
    
    if (region->base.current_psb) {
        printf("PSB:         %s\n", region->base.current_psb->name);
    }
    
    if (region->start_time > 0) {
        if (region->state == BMP_STATE_RUNNING) {
            printf("Elapsed:     %.0fs\n", difftime(time(NULL), region->start_time));
        } else if (region->end_time > 0) {
            printf("Total Time:  %.0fs\n", difftime(region->end_time, region->start_time));
        }
    }
}
