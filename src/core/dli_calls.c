/*
 * IMS DL/I Call Implementation
 * 
 * Implements the DL/I call interface for database operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ims.h"
#include "database.h"

/* Current position tracking per PCB */
static IMS_SEGMENT *current_position = NULL;
static IMS_SEGMENT *held_segment = NULL;

/* Forward sequential position for GN/GNP */
static IMS_SEGMENT *get_next_in_sequence(IMS_SEGMENT *current, IMS_SEGMENT_DEF *target_segment);

/* Main DL/I entry point (similar to CBLTDLI) */
int CBLDLI(DLI_FUNCTION func, void *pcb, void *io_area, ...) {
    va_list args;
    va_start(args, io_area);
    
    ims_system.total_calls++;
    
    int result = 0;
    
    switch (func) {
        case DLI_GU: {
            IMS_SSA **ssa = va_arg(args, IMS_SSA **);
            int ssa_count = va_arg(args, int);
            result = dli_gu((IMS_PCB *)pcb, io_area, ssa, ssa_count);
            break;
        }
        case DLI_GN: {
            IMS_SSA **ssa = va_arg(args, IMS_SSA **);
            int ssa_count = va_arg(args, int);
            result = dli_gn((IMS_PCB *)pcb, io_area, ssa, ssa_count);
            break;
        }
        case DLI_GNP: {
            IMS_SSA **ssa = va_arg(args, IMS_SSA **);
            int ssa_count = va_arg(args, int);
            result = dli_gnp((IMS_PCB *)pcb, io_area, ssa, ssa_count);
            break;
        }
        case DLI_GHU: {
            IMS_SSA **ssa = va_arg(args, IMS_SSA **);
            int ssa_count = va_arg(args, int);
            result = dli_ghu((IMS_PCB *)pcb, io_area, ssa, ssa_count);
            break;
        }
        case DLI_ISRT: {
            IMS_SSA **ssa = va_arg(args, IMS_SSA **);
            int ssa_count = va_arg(args, int);
            result = dli_isrt((IMS_PCB *)pcb, io_area, ssa, ssa_count);
            break;
        }
        case DLI_DLET:
            result = dli_dlet((IMS_PCB *)pcb, io_area);
            break;
        case DLI_REPL:
            result = dli_repl((IMS_PCB *)pcb, io_area);
            break;
        case DLI_GU_MSG:
            result = dli_gu_msg((IMS_IOPCB *)pcb, io_area);
            break;
        case DLI_GN_MSG:
            result = dli_gn_msg((IMS_IOPCB *)pcb, io_area);
            break;
        case DLI_ISRT_MSG: {
            int length = va_arg(args, int);
            result = dli_isrt_msg((IMS_IOPCB *)pcb, io_area, length);
            break;
        }
        default:
            ims_log("ERROR", "Unknown DL/I function code: %d", func);
            result = -1;
    }
    
    va_end(args);
    
    if (result == 0) {
        ims_system.successful_calls++;
    } else {
        ims_system.failed_calls++;
    }
    
    return result;
}

/* Get Unique - Direct retrieval based on SSA qualifications */
int dli_gu(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    if (!pcb || !io_area) {
        return -1;
    }
    
    ims_log("DEBUG", "DL/I GU call on DB '%s'", pcb->db_name);
    
    /* Get root segment of database */
    IMS_SEGMENT *root = segment_get_root(pcb->dbd);
    if (!root) {
        pcb->status_code = IMS_STATUS_GE;
        return -1;
    }
    
    /* If no SSA, return root segment */
    if (ssa_count == 0 || !ssa || !ssa[0]) {
        memcpy(io_area, root->data, root->length);
        strncpy(pcb->segment_name, root->definition->name, IMS_MAX_SEGMENT_NAME);
        pcb->level = root->definition->level;
        pcb->status_code = IMS_STATUS_OK;
        pcb->current_position = root;
        current_position = root;
        return 0;
    }
    
    /* Navigate hierarchy based on SSAs */
    IMS_SEGMENT *current = root;
    
    for (int i = 0; i < ssa_count && ssa[i]; i++) {
        /* Find segment matching SSA */
        bool found = false;
        
        if (strcmp(current->definition->name, ssa[i]->segment_name) == 0) {
            /* Current segment matches - check qualifications if any */
            if (ssa[i]->is_qualified) {
                /* Check qualifications */
                if (ssa_match(ssa[i], current)) {
                    found = true;
                }
            } else {
                found = true;
            }
        }
        
        if (!found) {
            /* Search children */
            IMS_SEGMENT *child = current->first_child;
            while (child) {
                if (strcmp(child->definition->name, ssa[i]->segment_name) == 0) {
                    if (!ssa[i]->is_qualified || ssa_match(ssa[i], child)) {
                        current = child;
                        found = true;
                        break;
                    }
                }
                child = child->next_sibling;
            }
        }
        
        if (!found) {
            pcb->status_code = IMS_STATUS_GE;
            strncpy(pcb->segment_name, ssa[i]->segment_name, IMS_MAX_SEGMENT_NAME);
            return -1;
        }
    }
    
    /* Copy segment data to I/O area */
    memcpy(io_area, current->data, current->length);
    strncpy(pcb->segment_name, current->definition->name, IMS_MAX_SEGMENT_NAME);
    pcb->level = current->definition->level;
    pcb->status_code = IMS_STATUS_OK;
    pcb->current_position = current;
    current_position = current;
    
    ims_log("DEBUG", "GU returned segment '%s'", pcb->segment_name);
    
    return 0;
}

/* Get Next - Sequential forward retrieval */
int dli_gn(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    if (!pcb || !io_area) {
        return -1;
    }
    
    ims_log("DEBUG", "DL/I GN call on DB '%s'", pcb->db_name);
    
    IMS_SEGMENT *next = NULL;
    IMS_SEGMENT_DEF *target_def = NULL;
    
    /* If SSA specified, we're looking for a specific segment type */
    if (ssa_count > 0 && ssa && ssa[0]) {
        /* Find segment definition */
        for (int i = 0; i < pcb->dbd->segment_count; i++) {
            if (strcmp(pcb->dbd->segments[i].name, ssa[0]->segment_name) == 0) {
                target_def = &pcb->dbd->segments[i];
                break;
            }
        }
    }
    
    /* Get next segment in sequence */
    IMS_SEGMENT *current = pcb->current_position ? pcb->current_position : segment_get_root(pcb->dbd);
    next = get_next_in_sequence(current, target_def);
    
    if (!next) {
        pcb->status_code = IMS_STATUS_GB;  /* End of database */
        return -1;
    }
    
    /* Check if we moved to different level */
    if (pcb->current_position && next->definition->level != pcb->level) {
        pcb->status_code = IMS_STATUS_GA;  /* Different level */
    } else if (pcb->current_position && 
               strcmp(next->definition->name, pcb->segment_name) != 0 &&
               next->definition->level == pcb->level) {
        pcb->status_code = IMS_STATUS_GK;  /* Same level, different type */
    } else {
        pcb->status_code = IMS_STATUS_OK;
    }
    
    /* Copy segment data */
    memcpy(io_area, next->data, next->length);
    strncpy(pcb->segment_name, next->definition->name, IMS_MAX_SEGMENT_NAME);
    pcb->level = next->definition->level;
    pcb->current_position = next;
    current_position = next;
    
    return 0;
}

/* Get Next in Parent - Sequential within current parent */
int dli_gnp(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    if (!pcb || !io_area) {
        return -1;
    }
    
    ims_log("DEBUG", "DL/I GNP call on DB '%s'", pcb->db_name);
    
    IMS_SEGMENT *current = pcb->current_position;
    if (!current) {
        pcb->status_code = IMS_STATUS_GE;
        return -1;
    }
    
    /* Get parent to constrain search */
    IMS_SEGMENT *parent = current->parent;
    if (!parent) {
        pcb->status_code = IMS_STATUS_GE;
        return -1;
    }
    
    /* Look for next sibling or child, but don't go above parent */
    IMS_SEGMENT *next = current->next_sibling;
    
    if (!next && current->first_child) {
        next = current->first_child;
    }
    
    if (!next) {
        pcb->status_code = IMS_STATUS_GE;  /* No more within parent */
        return -1;
    }
    
    /* Copy segment data */
    memcpy(io_area, next->data, next->length);
    strncpy(pcb->segment_name, next->definition->name, IMS_MAX_SEGMENT_NAME);
    pcb->level = next->definition->level;
    pcb->status_code = IMS_STATUS_OK;
    pcb->current_position = next;
    
    return 0;
}

/* Get Hold versions - same as Get but set hold flag */
int dli_ghu(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    int result = dli_gu(pcb, io_area, ssa, ssa_count);
    if (result == 0 && pcb->current_position) {
        pcb->current_position->is_held = true;
        held_segment = pcb->current_position;
    }
    return result;
}

int dli_ghn(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    int result = dli_gn(pcb, io_area, ssa, ssa_count);
    if (result == 0 && pcb->current_position) {
        pcb->current_position->is_held = true;
        held_segment = pcb->current_position;
    }
    return result;
}

int dli_ghnp(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    int result = dli_gnp(pcb, io_area, ssa, ssa_count);
    if (result == 0 && pcb->current_position) {
        pcb->current_position->is_held = true;
        held_segment = pcb->current_position;
    }
    return result;
}

/* Insert segment */
int dli_isrt(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count) {
    if (!pcb || !io_area) {
        return -1;
    }
    
    ims_log("DEBUG", "DL/I ISRT call on DB '%s'", pcb->db_name);
    
    /* Need at least one SSA to know what to insert */
    if (ssa_count == 0 || !ssa || !ssa[0]) {
        pcb->status_code = IMS_STATUS_AH;  /* Missing SSA */
        return -1;
    }
    
    /* Find segment definition */
    IMS_SEGMENT_DEF *seg_def = NULL;
    for (int i = 0; i < pcb->dbd->segment_count; i++) {
        if (strcmp(pcb->dbd->segments[i].name, ssa[0]->segment_name) == 0) {
            seg_def = &pcb->dbd->segments[i];
            break;
        }
    }
    
    if (!seg_def) {
        pcb->status_code = IMS_STATUS_AK;  /* Invalid segment name */
        return -1;
    }
    
    /* Create new segment */
    IMS_SEGMENT *new_seg = segment_create(seg_def, io_area, seg_def->max_length);
    if (!new_seg) {
        return -1;
    }
    
    /* Find parent to insert under */
    IMS_SEGMENT *parent = pcb->current_position;
    if (!parent && seg_def->level > 1) {
        pcb->status_code = IMS_STATUS_LD;  /* No parent */
        segment_free(new_seg);
        return -1;
    }
    
    /* Insert segment */
    if (parent) {
        segment_add_child(parent, new_seg);
    }
    
    pcb->current_position = new_seg;
    strncpy(pcb->segment_name, seg_def->name, IMS_MAX_SEGMENT_NAME);
    pcb->level = seg_def->level;
    pcb->status_code = IMS_STATUS_OK;
    
    ims_log("INFO", "Inserted segment '%s'", seg_def->name);
    
    return 0;
}

/* Delete held segment */
int dli_dlet(IMS_PCB *pcb, void *io_area) {
    if (!pcb) {
        return -1;
    }
    
    if (!held_segment) {
        ims_log("ERROR", "DLET: No segment held");
        return -1;
    }
    
    ims_log("INFO", "Deleted segment '%s'", held_segment->definition->name);
    
    /* Note: In a real implementation, we would properly remove from hierarchy */
    held_segment->is_held = false;
    held_segment = NULL;
    
    pcb->status_code = IMS_STATUS_OK;
    return 0;
}

/* Replace held segment */
int dli_repl(IMS_PCB *pcb, void *io_area) {
    if (!pcb || !io_area) {
        return -1;
    }
    
    if (!held_segment) {
        ims_log("ERROR", "REPL: No segment held");
        return -1;
    }
    
    /* Copy new data to segment */
    int len = held_segment->definition->max_length;
    memcpy(held_segment->data, io_area, len);
    
    held_segment->is_held = false;
    held_segment = NULL;
    
    pcb->status_code = IMS_STATUS_OK;
    ims_log("INFO", "Replaced segment '%s'", pcb->segment_name);
    
    return 0;
}

/* Message I/O calls - delegate to TM */
int dli_gu_msg(IMS_IOPCB *pcb, void *io_area) {
    /* Get input message from queue - would be implemented via MPP */
    pcb->status_code = IMS_STATUS_QC;  /* No message */
    return -1;
}

int dli_gn_msg(IMS_IOPCB *pcb, void *io_area) {
    /* Get next segment of message */
    pcb->status_code = IMS_STATUS_QD;  /* No more segments */
    return -1;
}

int dli_isrt_msg(IMS_IOPCB *pcb, void *io_area, int length) {
    /* Send output message */
    ims_log("INFO", "Output message queued (%d bytes)", length);
    pcb->status_code = IMS_STATUS_OK;
    return 0;
}

/* PSB Scheduling */
int dli_pcb(const char *psb_name, IMS_PSB **psb) {
    *psb = ims_find_psb(psb_name);
    if (!*psb) {
        ims_log("ERROR", "PSB '%s' not found", psb_name);
        return -1;
    }
    
    ims_system.current_psb = *psb;
    ims_log("INFO", "PSB '%s' scheduled", psb_name);
    return 0;
}

int dli_term(IMS_PSB *psb) {
    if (ims_system.current_psb == psb) {
        ims_system.current_psb = NULL;
    }
    ims_log("INFO", "PSB terminated");
    return 0;
}

/* Helper: Get next segment in hierarchical sequence */
static IMS_SEGMENT *get_next_in_sequence(IMS_SEGMENT *current, IMS_SEGMENT_DEF *target_def) {
    if (!current) return NULL;
    
    /* Try children first (depth-first) */
    if (current->first_child) {
        if (!target_def || strcmp(current->first_child->definition->name, target_def->name) == 0) {
            return current->first_child;
        }
    }
    
    /* Try next sibling */
    if (current->next_sibling) {
        if (!target_def || strcmp(current->next_sibling->definition->name, target_def->name) == 0) {
            return current->next_sibling;
        }
    }
    
    /* Go up and try parent's sibling */
    IMS_SEGMENT *parent = current->parent;
    while (parent) {
        if (parent->next_sibling) {
            if (!target_def || strcmp(parent->next_sibling->definition->name, target_def->name) == 0) {
                return parent->next_sibling;
            }
        }
        parent = parent->parent;
    }
    
    return NULL;  /* End of database */
}
