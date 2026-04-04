/*
 * IMS Database Manager - Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database.h"

/* Storage for database instances */
static IMS_SEGMENT *db_roots[64];
static int db_root_count = 0;

IMS_DBD *dbd_create(const char *name, const char *access_method) {
    IMS_DBD *dbd = (IMS_DBD *)calloc(1, sizeof(IMS_DBD));
    if (!dbd) return NULL;
    
    strncpy(dbd->name, name, 8);
    strncpy(dbd->access_method, access_method, 4);
    
    ims_log("INFO", "DBD '%s' created (access: %s)", name, access_method);
    return dbd;
}

IMS_SEGMENT_DEF *dbd_add_segment(IMS_DBD *dbd, const char *name,
                                  const char *parent_name, int max_length) {
    if (!dbd || dbd->segment_count >= IMS_MAX_SEGMENTS_PER_DB) {
        return NULL;
    }
    
    IMS_SEGMENT_DEF *seg = &dbd->segments[dbd->segment_count];
    memset(seg, 0, sizeof(IMS_SEGMENT_DEF));
    
    strncpy(seg->name, name, IMS_MAX_SEGMENT_NAME);
    seg->max_length = max_length;
    seg->min_length = 1;
    
    /* Find parent */
    if (parent_name && parent_name[0]) {
        for (int i = 0; i < dbd->segment_count; i++) {
            if (strcmp(dbd->segments[i].name, parent_name) == 0) {
                seg->parent = &dbd->segments[i];
                seg->level = seg->parent->level + 1;
                
                /* Add as child to parent */
                if (!seg->parent->first_child) {
                    seg->parent->first_child = seg;
                } else {
                    IMS_SEGMENT_DEF *sibling = seg->parent->first_child;
                    while (sibling->next_sibling) {
                        sibling = sibling->next_sibling;
                    }
                    sibling->next_sibling = seg;
                }
                break;
            }
        }
    } else {
        /* Root segment */
        seg->level = 1;
        dbd->root = seg;
    }
    
    dbd->segment_count++;
    
    ims_log("DEBUG", "Segment '%s' added to DBD '%s' (level %d)", 
            name, dbd->name, seg->level);
    
    return seg;
}

int dbd_add_field(IMS_SEGMENT_DEF *seg, const char *name,
                  int offset, int length, bool is_key, char type) {
    if (!seg || seg->field_count >= 32) {
        return -1;
    }
    
    IMS_FIELD *field = &seg->fields[seg->field_count];
    strncpy(field->name, name, IMS_MAX_FIELD_NAME);
    field->offset = offset;
    field->length = length;
    field->is_key = is_key;
    field->type = type;
    
    seg->field_count++;
    
    return 0;
}

IMS_SEGMENT *segment_create(IMS_SEGMENT_DEF *def, const void *data, int length) {
    if (!def) return NULL;
    
    IMS_SEGMENT *seg = (IMS_SEGMENT *)calloc(1, sizeof(IMS_SEGMENT));
    if (!seg) return NULL;
    
    seg->definition = def;
    
    if (data && length > 0) {
        int copy_len = length;
        if (copy_len > def->max_length) copy_len = def->max_length;
        if (copy_len > IMS_MAX_SEGMENT_LENGTH) copy_len = IMS_MAX_SEGMENT_LENGTH;
        memcpy(seg->data, data, copy_len);
        seg->length = copy_len;
    }
    
    return seg;
}

void segment_free(IMS_SEGMENT *seg) {
    if (!seg) return;
    
    /* Recursively free children */
    IMS_SEGMENT *child = seg->first_child;
    while (child) {
        IMS_SEGMENT *next = child->next_sibling;
        segment_free(child);
        child = next;
    }
    
    /* Free twins */
    IMS_SEGMENT *twin = seg->next_twin;
    while (twin) {
        IMS_SEGMENT *next = twin->next_twin;
        free(twin);
        twin = next;
    }
    
    free(seg);
}

IMS_SEGMENT *segment_get_root(IMS_DBD *dbd) {
    /* Find the root segment for this database */
    for (int i = 0; i < db_root_count; i++) {
        if (db_roots[i] && db_roots[i]->definition && 
            db_roots[i]->definition == dbd->root) {
            return db_roots[i];
        }
    }
    return NULL;
}

IMS_SEGMENT *segment_get_parent(IMS_SEGMENT *seg) {
    return seg ? seg->parent : NULL;
}

IMS_SEGMENT *segment_get_first_child(IMS_SEGMENT *seg) {
    return seg ? seg->first_child : NULL;
}

IMS_SEGMENT *segment_get_next_sibling(IMS_SEGMENT *seg) {
    return seg ? seg->next_sibling : NULL;
}

IMS_SEGMENT *segment_get_next_twin(IMS_SEGMENT *seg) {
    return seg ? seg->next_twin : NULL;
}

int segment_add_child(IMS_SEGMENT *parent, IMS_SEGMENT *child) {
    if (!parent || !child) return -1;
    
    child->parent = parent;
    
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        IMS_SEGMENT *sibling = parent->first_child;
        while (sibling->next_sibling) {
            sibling = sibling->next_sibling;
        }
        sibling->next_sibling = child;
    }
    
    return 0;
}

int segment_add_twin(IMS_SEGMENT *seg, IMS_SEGMENT *twin) {
    if (!seg || !twin) return -1;
    
    twin->parent = seg->parent;
    twin->definition = seg->definition;
    
    IMS_SEGMENT *last = seg;
    while (last->next_twin) {
        last = last->next_twin;
    }
    last->next_twin = twin;
    twin->prev_sibling = last;
    
    return 0;
}

int segment_get_field(IMS_SEGMENT *seg, const char *field_name,
                      void *buffer, int max_length) {
    if (!seg || !field_name || !buffer) return -1;
    
    IMS_SEGMENT_DEF *def = seg->definition;
    for (int i = 0; i < def->field_count; i++) {
        if (strcmp(def->fields[i].name, field_name) == 0) {
            int len = def->fields[i].length;
            if (len > max_length) len = max_length;
            memcpy(buffer, seg->data + def->fields[i].offset, len);
            return len;
        }
    }
    
    return -1;  /* Field not found */
}

int segment_set_field(IMS_SEGMENT *seg, const char *field_name,
                      const void *data, int length) {
    if (!seg || !field_name || !data) return -1;
    
    IMS_SEGMENT_DEF *def = seg->definition;
    for (int i = 0; i < def->field_count; i++) {
        if (strcmp(def->fields[i].name, field_name) == 0) {
            int len = def->fields[i].length;
            if (length < len) {
                /* Pad with spaces */
                memset(seg->data + def->fields[i].offset, ' ', len);
                len = length;
            }
            memcpy(seg->data + def->fields[i].offset, data, len);
            return 0;
        }
    }
    
    return -1;
}

void dbd_display(IMS_DBD *dbd) {
    if (!dbd) return;
    
    printf("\n================================================================================\n");
    printf("  DBD: %-8s    Access Method: %-8s    Segments: %d\n",
           dbd->name, dbd->access_method, dbd->segment_count);
    printf("================================================================================\n\n");
    
    /* Display hierarchy */
    printf("  Database Hierarchy:\n\n");
    
    for (int i = 0; i < dbd->segment_count; i++) {
        IMS_SEGMENT_DEF *seg = &dbd->segments[i];
        
        /* Indent based on level */
        for (int j = 1; j < seg->level; j++) {
            printf("    ");
        }
        
        if (seg->level > 1) {
            printf("└── ");
        }
        
        printf("%-8s (Level %d, Max %d bytes", seg->name, seg->level, seg->max_length);
        
        /* Show key field if any */
        for (int f = 0; f < seg->field_count; f++) {
            if (seg->fields[f].is_key) {
                printf(", Key: %s", seg->fields[f].name);
                break;
            }
        }
        printf(")\n");
        
        /* Show fields */
        for (int f = 0; f < seg->field_count; f++) {
            for (int j = 0; j < seg->level; j++) {
                printf("    ");
            }
            printf("    %-8s: offset=%d, len=%d%s\n",
                   seg->fields[f].name,
                   seg->fields[f].offset,
                   seg->fields[f].length,
                   seg->fields[f].is_key ? " (KEY)" : "");
        }
    }
    
    printf("\n================================================================================\n");
}

void segment_display(IMS_SEGMENT *seg, int indent) {
    if (!seg) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    
    printf("[%s] ", seg->definition->name);
    
    /* Show key field value */
    for (int f = 0; f < seg->definition->field_count; f++) {
        if (seg->definition->fields[f].is_key) {
            char key[64] = {0};
            int len = seg->definition->fields[f].length;
            if (len > 63) len = 63;
            memcpy(key, seg->data + seg->definition->fields[f].offset, len);
            printf("Key='%s'", key);
            break;
        }
    }
    printf("\n");
    
    /* Display children */
    IMS_SEGMENT *child = seg->first_child;
    while (child) {
        segment_display(child, indent + 1);
        child = child->next_sibling;
    }
    
    /* Display twins */
    if (seg->next_twin) {
        segment_display(seg->next_twin, indent);
    }
}

/* ==========================================================================
 * HOSPITAL Sample Database
 * ==========================================================================
 * 
 * Classic IMS training database structure:
 * 
 * HOSPITAL (root)
 * ├── WARD
 * │   └── PATIENT
 * │       ├── TREATMNT
 * │       └── DOCTOR
 * └── FACILITY
 */

int load_hospital_database(void) {
    ims_log("INFO", "Loading HOSPITAL sample database...");
    
    /* Create DBD */
    IMS_DBD *dbd = dbd_create("HOSPITAL", "HDAM");
    if (!dbd) {
        ims_log("ERROR", "Failed to create HOSPITAL DBD");
        return -1;
    }
    
    /* Define segments */
    IMS_SEGMENT_DEF *hospital = dbd_add_segment(dbd, "HOSPITAL", NULL, 100);
    dbd_add_field(hospital, "HOSPCODE", 0, 4, true, 'C');
    dbd_add_field(hospital, "HOSPNAME", 4, 30, false, 'C');
    dbd_add_field(hospital, "HOSPADDR", 34, 50, false, 'C');
    dbd_add_field(hospital, "HOSPPHON", 84, 15, false, 'C');
    
    IMS_SEGMENT_DEF *ward = dbd_add_segment(dbd, "WARD", "HOSPITAL", 60);
    dbd_add_field(ward, "WARDNO", 0, 4, true, 'C');
    dbd_add_field(ward, "WARDNAME", 4, 20, false, 'C');
    dbd_add_field(ward, "WARDTYPE", 24, 10, false, 'C');
    dbd_add_field(ward, "NUMBEDS", 34, 4, false, 'C');
    
    IMS_SEGMENT_DEF *patient = dbd_add_segment(dbd, "PATIENT", "WARD", 120);
    dbd_add_field(patient, "PATNO", 0, 6, true, 'C');
    dbd_add_field(patient, "PATNAME", 6, 30, false, 'C');
    dbd_add_field(patient, "PATADDR", 36, 50, false, 'C');
    dbd_add_field(patient, "DATADMIT", 86, 10, false, 'C');
    dbd_add_field(patient, "PATDOC", 96, 20, false, 'C');
    
    IMS_SEGMENT_DEF *treatmnt = dbd_add_segment(dbd, "TREATMNT", "PATIENT", 80);
    dbd_add_field(treatmnt, "TREATNO", 0, 4, true, 'C');
    dbd_add_field(treatmnt, "TREATDAT", 4, 10, false, 'C');
    dbd_add_field(treatmnt, "TREATTYP", 14, 20, false, 'C');
    dbd_add_field(treatmnt, "TREATMED", 34, 30, false, 'C');
    
    IMS_SEGMENT_DEF *doctor = dbd_add_segment(dbd, "DOCTOR", "PATIENT", 80);
    dbd_add_field(doctor, "DOCNO", 0, 4, true, 'C');
    dbd_add_field(doctor, "DOCNAME", 4, 30, false, 'C');
    dbd_add_field(doctor, "DOCSPEC", 34, 20, false, 'C');
    
    IMS_SEGMENT_DEF *facility = dbd_add_segment(dbd, "FACILITY", "HOSPITAL", 60);
    dbd_add_field(facility, "FACCODE", 0, 4, true, 'C');
    dbd_add_field(facility, "FACNAME", 4, 30, false, 'C');
    dbd_add_field(facility, "FACTYPE", 34, 20, false, 'C');
    
    /* Register DBD */
    ims_register_dbd(dbd);
    
    /* Create sample data */
    ims_log("INFO", "Creating sample data...");
    
    /* Create HOSPITAL segment instance */
    char hosp_data[100];
    memset(hosp_data, ' ', 100);
    memcpy(hosp_data + 0, "H001", 4);
    memcpy(hosp_data + 4, "SANTA CASA DE MISERICORDIA", 26);
    memcpy(hosp_data + 34, "RUA DA SAUDE, 123", 17);
    memcpy(hosp_data + 84, "11-3456-7890", 12);
    
    IMS_SEGMENT *hosp_seg = segment_create(hospital, hosp_data, 100);
    db_roots[db_root_count++] = hosp_seg;
    
    /* Create WARD segments */
    char ward_data[60];
    memset(ward_data, ' ', 60);
    memcpy(ward_data + 0, "W001", 4);
    memcpy(ward_data + 4, "EMERGENCIA", 10);
    memcpy(ward_data + 24, "URGENTE", 7);
    memcpy(ward_data + 34, "20", 2);
    
    IMS_SEGMENT *ward_seg = segment_create(ward, ward_data, 60);
    segment_add_child(hosp_seg, ward_seg);
    
    /* Create PATIENT segment */
    char pat_data[120];
    memset(pat_data, ' ', 120);
    memcpy(pat_data + 0, "P00001", 6);
    memcpy(pat_data + 6, "JOAO DA SILVA", 13);
    memcpy(pat_data + 36, "RUA DAS FLORES, 456", 19);
    memcpy(pat_data + 86, "2026-02-01", 10);
    memcpy(pat_data + 96, "DR. CARLOS SOUZA", 16);
    
    IMS_SEGMENT *pat_seg = segment_create(patient, pat_data, 120);
    segment_add_child(ward_seg, pat_seg);
    
    /* Create a PSB for HOSPITAL database */
    IMS_PSB *psb = (IMS_PSB *)calloc(1, sizeof(IMS_PSB));
    strncpy(psb->name, "HOSPPGM", 8);
    psb->language = 'C';
    psb->pcb_count = 2;  /* I/O PCB + 1 DB PCB */
    
    /* Create I/O PCB */
    psb->io_pcb = (IMS_IOPCB *)calloc(1, sizeof(IMS_IOPCB));
    strncpy(psb->io_pcb->lterm_name, "CONSOLE", 8);
    
    /* Create DB PCB for HOSPITAL */
    IMS_PCB *db_pcb = (IMS_PCB *)calloc(1, sizeof(IMS_PCB));
    strncpy(db_pcb->db_name, "HOSPITAL", 8);
    strncpy(db_pcb->proc_options, "A", 1);  /* All access */
    db_pcb->dbd = dbd;
    db_pcb->sens_seg_count = 6;  /* All segments sensitive */
    psb->db_pcbs[0] = db_pcb;
    
    ims_register_psb(psb);
    
    /* Create a sample transaction */
    IMS_TRANSACTION_DEF txn = {0};
    strncpy(txn.code, "HOSPINQ", 8);
    strncpy(txn.psb_name, "HOSPPGM", 8);
    txn.region_type = REGION_MPP;
    txn.priority = 2;
    txn.is_conversational = false;
    ims_register_transaction(&txn);
    
    ims_log("INFO", "HOSPITAL database loaded successfully");
    ims_log("INFO", "  Segments defined: %d", dbd->segment_count);
    ims_log("INFO", "  PSB: HOSPPGM");
    ims_log("INFO", "  TXN: HOSPINQ");
    
    /* Display the structure */
    dbd_display(dbd);
    
    return 0;
}
