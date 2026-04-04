/*
 * z/OS Address Space Implementation
 *
 * Simulates the MVS address space model. Each subsystem (IMS, DB2, JES2)
 * runs in its own address space with separate private storage and a
 * shared Common Service Area (CSA).
 *
 * Educational note: In real z/OS, address spaces are created by the
 * ASCRE (Address Space Create) system macro. Here we simulate the
 * essential behavior without actual hardware memory protection.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "address_space.h"

/* Global z/OS system instance */
ZOS_SYSTEM zos_system;

/* =========================================================================
 * TYPE / STATUS NAME STRINGS (for display panels)
 * ========================================================================= */

const char *zos_as_type_name(ZOS_AS_TYPE type) {
    switch (type) {
        case AS_TYPE_MVS:      return "MVS KERNEL";
        case AS_TYPE_RACF:     return "RACF      ";
        case AS_TYPE_JES2:     return "JES2      ";
        case AS_TYPE_IMS_CTL:  return "IMS CTL   ";
        case AS_TYPE_IMS_MPP:  return "IMS MPP   ";
        case AS_TYPE_IMS_BMP:  return "IMS BMP   ";
        case AS_TYPE_IMS_IFP:  return "IMS IFP   ";
        case AS_TYPE_IMS_DBRC: return "IMS DBRC  ";
        case AS_TYPE_DB2:      return "DB2       ";
        case AS_TYPE_BATCH:    return "BATCH     ";
        case AS_TYPE_TSO:      return "TSO       ";
        default:               return "UNKNOWN   ";
    }
}

const char *zos_as_status_name(ZOS_AS_STATUS status) {
    switch (status) {
        case AS_STATUS_STARTING:    return "STARTING ";
        case AS_STATUS_ACTIVE:      return "ACTIVE   ";
        case AS_STATUS_WAITING:     return "WAITING  ";
        case AS_STATUS_SWAPPED_OUT: return "SWAPPED  ";
        case AS_STATUS_TERMINATING: return "STOPPING ";
        case AS_STATUS_TERMINATED:  return "ENDED    ";
        default:                    return "UNKNOWN  ";
    }
}

const char *zos_abend_name(ZOS_ABEND_CODE code, bool is_user, int user_code) {
    static char buf[16];
    if (is_user) {
        snprintf(buf, sizeof(buf), "U%04d", user_code);
        return buf;
    }
    switch (code) {
        case ABEND_S0C1: return "S0C1";
        case ABEND_S0C4: return "S0C4";
        case ABEND_S0C7: return "S0C7";
        case ABEND_S0CB: return "S0CB";
        case ABEND_S222: return "S222";
        case ABEND_S322: return "S322";
        case ABEND_S806: return "S806";
        case ABEND_S837: return "S837";
        case ABEND_S878: return "S878";
        default:
            snprintf(buf, sizeof(buf), "S%03X", (unsigned int)code);
            return buf;
    }
}

/* =========================================================================
 * ADDRESS SPACE LIFECYCLE
 * ========================================================================= */

ZOS_ADDRESS_SPACE *zos_as_create(const char *name, ZOS_AS_TYPE type,
                                  int priority, int storage_key) {
    if (zos_system.space_count >= ZOS_MAX_AS) {
        zos_wto(0, "ZOS001E ADDRESS SPACE TABLE FULL - CANNOT CREATE NEW AS");
        return NULL;
    }

    ZOS_ADDRESS_SPACE *as = (ZOS_ADDRESS_SPACE *)calloc(1, sizeof(ZOS_ADDRESS_SPACE));
    if (!as) {
        zos_wto(0, "ZOS002E GETMAIN FAILED FOR ASCB");
        return NULL;
    }

    /* Assign ASID */
    as->asid = zos_system.next_asid++;

    /* Name: pad to 8 chars with spaces */
    memset(as->name, ' ', ZOS_AS_NAME_LEN);
    if (name) {
        size_t len = strlen(name);
        if (len > ZOS_AS_NAME_LEN) len = ZOS_AS_NAME_LEN;
        memcpy(as->name, name, len);
    }
    as->name[ZOS_AS_NAME_LEN] = '\0';

    as->type             = type;
    as->status           = AS_STATUS_STARTING;
    as->dispatch_priority = priority;
    as->storage_key      = storage_key;
    as->start_time       = time(NULL);
    as->return_code      = -1;   /* Not yet complete */
    as->getmain_count    = 0;
    as->subsystem_ctx    = NULL;

    /* Set symbolic program name based on type (8-char IBM names, space-padded) */
    const char *pgm = "????????";
    switch (type) {
        case AS_TYPE_MVS:      pgm = "IEANUC01"; break;
        case AS_TYPE_RACF:     pgm = "IRRMAIN "; break;
        case AS_TYPE_JES2:     pgm = "HASJES20"; break;
        case AS_TYPE_IMS_CTL:  pgm = "DFSMVRC0"; break;
        case AS_TYPE_IMS_MPP:  pgm = "DFSRRC00"; break;
        case AS_TYPE_IMS_BMP:  pgm = "DFSRRC00"; break;
        case AS_TYPE_IMS_IFP:  pgm = "DFSRRC00"; break;
        case AS_TYPE_IMS_DBRC: pgm = "DSPDBRC0"; break;
        case AS_TYPE_DB2:      pgm = "DSNMSTR "; break;
        case AS_TYPE_BATCH:    pgm = "--------"; break;
        case AS_TYPE_TSO:      pgm = "IKJEFT01"; break;
        default:               pgm = "????????"; break;
    }
    memcpy(as->pgm_name, pgm, ZOS_PGMNAME_LEN);
    as->pgm_name[ZOS_PGMNAME_LEN] = '\0';

    /* Simulated storage map */
    as->storage.nucleus_size  = 4 * 1024 * 1024;   /* 4MB nucleus */
    as->storage.psa_size      = 4096;               /* 4KB PSA */
    as->storage.sqa_size      = 256 * 1024;         /* 256KB SQA */
    as->storage.csa_size      = ZOS_CSA_SIZE;       /* Shared */
    as->storage.lsqa_size     = 64 * 1024;          /* 64KB LSQA */
    as->storage.swa_size      = 128 * 1024;         /* 128KB SWA */
    as->storage.private_size  = ZOS_PRIVATE_DEFAULT;
    as->storage.private_used  = 0;
    as->storage.ecsa_size     = 1024 * 1024;        /* 1MB ECSA */

    /* Register in system table */
    zos_system.spaces[zos_system.space_count++] = as;

    /* Log creation */
    char msg[128];
    snprintf(msg, sizeof(msg),
             "$HASP373 %.8s STARTED - ASID=%04X TYPE=%s",
             as->name, as->asid, zos_as_type_name(type));
    zos_wto(0, msg);

    return as;
}

int zos_as_activate(ZOS_ADDRESS_SPACE *as) {
    if (!as) return -1;
    as->status = AS_STATUS_ACTIVE;
    return 0;
}

void zos_as_terminate(ZOS_ADDRESS_SPACE *as, int return_code) {
    if (!as) return;

    as->status      = AS_STATUS_TERMINATING;
    as->return_code = return_code;
    as->elapsed_time_ms = (long)(time(NULL) - as->start_time) * 1000;

    /* Free all GETMAIN allocations */
    for (int i = 0; i < as->getmain_count; i++) {
        if (as->getmain_table[i].ptr) {
            free(as->getmain_table[i].ptr);
            as->getmain_table[i].ptr = NULL;
        }
    }
    as->getmain_count = 0;
    as->storage.private_used = 0;

    as->status = AS_STATUS_TERMINATED;

    char msg[128];
    snprintf(msg, sizeof(msg),
             "$HASP395 %.8s ENDED - RC=%04d",
             as->name, return_code);
    zos_wto(0, msg);
}

void zos_as_abend(ZOS_ADDRESS_SPACE *as, ZOS_ABEND_CODE code,
                  bool is_user, int user_code) {
    if (!as) return;

    as->abended        = true;
    as->abend_code     = code;
    as->is_user_abend  = is_user;
    as->user_abend_code = user_code;

    char msg[128];
    snprintf(msg, sizeof(msg),
             "IEA995I SYMPTOM DUMP OUTPUT   ABEND=%.4s  ASID=%04X  PGM=%.8s",
             zos_abend_name(code, is_user, user_code), as->asid, as->pgm_name);
    zos_wto(0, msg);

    zos_as_terminate(as, -1);
}

/* =========================================================================
 * ADDRESS SPACE LOOKUP
 * ========================================================================= */

ZOS_ADDRESS_SPACE *zos_as_find_asid(int asid) {
    for (int i = 0; i < zos_system.space_count; i++) {
        if (zos_system.spaces[i] && zos_system.spaces[i]->asid == asid) {
            return zos_system.spaces[i];
        }
    }
    return NULL;
}

ZOS_ADDRESS_SPACE *zos_as_find_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < zos_system.space_count; i++) {
        ZOS_ADDRESS_SPACE *as = zos_system.spaces[i];
        if (as && strncmp(as->name, name, strlen(name)) == 0) {
            return as;
        }
    }
    return NULL;
}

ZOS_ADDRESS_SPACE *zos_as_find_type(ZOS_AS_TYPE type) {
    for (int i = 0; i < zos_system.space_count; i++) {
        if (zos_system.spaces[i] && zos_system.spaces[i]->type == type) {
            return zos_system.spaces[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * STORAGE MANAGEMENT (GETMAIN / FREEMAIN simulation)
 * ========================================================================= */

void *zos_getmain(ZOS_ADDRESS_SPACE *as, size_t size, int subpool) {
    if (!as || size == 0) return NULL;
    if (as->getmain_count >= 128) {
        zos_as_abend(as, ABEND_S878, false, 0);
        return NULL;
    }
    if (as->storage.private_used + size > as->storage.private_size) {
        zos_as_abend(as, ABEND_S878, false, 0);
        return NULL;
    }

    void *ptr = calloc(1, size);
    if (!ptr) {
        zos_as_abend(as, ABEND_S878, false, 0);
        return NULL;
    }

    as->getmain_table[as->getmain_count].ptr     = ptr;
    as->getmain_table[as->getmain_count].size    = size;
    as->getmain_table[as->getmain_count].subpool = subpool;
    as->getmain_count++;
    as->storage.private_used += size;

    return ptr;
}

void zos_freemain(ZOS_ADDRESS_SPACE *as, void *ptr) {
    if (!as || !ptr) return;
    for (int i = 0; i < as->getmain_count; i++) {
        if (as->getmain_table[i].ptr == ptr) {
            as->storage.private_used -= as->getmain_table[i].size;
            free(as->getmain_table[i].ptr);
            /* Compact table */
            as->getmain_table[i] = as->getmain_table[as->getmain_count - 1];
            as->getmain_count--;
            return;
        }
    }
}

void *zos_csa_getmain(size_t size, int owner_asid, const char *owner_name) {
    if (zos_system.csa_used + size > ZOS_CSA_SIZE) {
        zos_wto(owner_asid, "IEA001E CSA STORAGE EXHAUSTED");
        return NULL;
    }
    if (zos_system.csa_alloc_count >= CSA_MAX_ALLOCS) {
        return NULL;
    }

    void *ptr = zos_system.csa_buffer + zos_system.csa_used;
    memset(ptr, 0, size);

    ZOS_CSA_BLOCK *blk = &zos_system.csa_allocs[zos_system.csa_alloc_count++];
    blk->offset     = zos_system.csa_used;
    blk->size       = size;
    blk->owner_asid = owner_asid;
    blk->in_use     = true;
    if (owner_name) {
        strncpy(blk->owner_name, owner_name, CSA_OWNER_LEN - 1);
    }

    zos_system.csa_used += size;
    return ptr;
}

void zos_csa_freemain(void *ptr) {
    if (!ptr) return;
    size_t offset = (unsigned char *)ptr - zos_system.csa_buffer;
    for (int i = 0; i < zos_system.csa_alloc_count; i++) {
        if (zos_system.csa_allocs[i].offset == offset) {
            zos_system.csa_allocs[i].in_use = false;
            return;
        }
    }
}

/* =========================================================================
 * WTO — WRITE TO OPERATOR
 *
 * Appends a message to the in-memory system log.
 * In real z/OS, WTO writes to the operator console and SMF.
 * ========================================================================= */

void zos_wto(int caller_asid, const char *message) {
    if (!message) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[10];
    strftime(ts, sizeof(ts), "%H:%M:%S", t);

    char line[256];
    int  n;

    if (caller_asid > 0) {
        n = snprintf(line, sizeof(line), "[%s] A=%04X %s\n", ts, caller_asid, message);
    } else {
        n = snprintf(line, sizeof(line), "[%s] SYSTEM %s\n", ts, message);
    }

    /* Append to in-memory syslog */
    if (n > 0 && zos_system.syslog_len + n < (int)sizeof(zos_system.syslog)) {
        memcpy(zos_system.syslog + zos_system.syslog_len, line, n);
        zos_system.syslog_len += n;
        zos_system.syslog[zos_system.syslog_len] = '\0';
    }
}

/* =========================================================================
 * JES2 OPERATIONS
 * ========================================================================= */

int zos_jes2_submit_job(const char *jobname, char job_class, char msgclass) {
    ZOS_JES2_CTX *jes = &zos_system.jes2;

    if (jes->job_count >= JES_MAX_JOBS) {
        zos_wto(ASID_JES2, "$HASP000 JOB QUEUE FULL");
        return -1;
    }

    int jnum = jes->next_job_number++;
    struct JES_JOB_ENTRY *job = &jes->jobs[jes->job_count++];

    memset(job, 0, sizeof(*job));
    job->job_number  = jnum;
    job->job_class   = job_class;
    job->msgclass    = msgclass;
    job->is_running  = false;
    job->is_complete = false;
    job->return_code = -1;

    memset(job->jobname, ' ', JES_JOBNAME_LEN);
    if (jobname) {
        size_t len = strlen(jobname);
        if (len > JES_JOBNAME_LEN) len = JES_JOBNAME_LEN;
        memcpy(job->jobname, jobname, len);
    }
    job->jobname[JES_JOBNAME_LEN] = '\0';

    char msg[80];
    snprintf(msg, sizeof(msg), "$HASP100 %.8s ON INTRDR", job->jobname);
    zos_wto(ASID_JES2, msg);

    snprintf(msg, sizeof(msg), "$HASP373 %.8s STARTED   CLASS %c  SYS %s",
             job->jobname, job_class, zos_system.system_name);
    zos_wto(ASID_JES2, msg);

    return jnum;
}

int zos_jes2_complete_job(int job_number, int return_code) {
    ZOS_JES2_CTX *jes = &zos_system.jes2;

    for (int i = 0; i < jes->job_count; i++) {
        if (jes->jobs[i].job_number == job_number) {
            jes->jobs[i].is_running  = false;
            jes->jobs[i].is_complete = true;
            jes->jobs[i].return_code = return_code;

            char msg[80];
            snprintf(msg, sizeof(msg),
                     "$HASP395 %.8s ENDED     RC=%04d",
                     jes->jobs[i].jobname, return_code);
            zos_wto(ASID_JES2, msg);
            return 0;
        }
    }
    return -1;
}

int zos_jes2_spool_write(int job_number, const char *stepname,
                          const char *ddname, char sysout_class,
                          const char *line) {
    ZOS_JES2_CTX *jes = &zos_system.jes2;

    /* Find existing spool entry for this job+step+dd, or create new one */
    ZOS_SPOOL_ENTRY *entry = NULL;
    for (int i = 0; i < jes->spool_count; i++) {
        if (jes->spool[i].job_number == job_number &&
            !jes->spool[i].is_closed &&
            strncmp(jes->spool[i].ddname, ddname, JES_DDNAME_LEN) == 0) {
            entry = &jes->spool[i];
            break;
        }
    }

    if (!entry) {
        if (jes->spool_count >= JES_MAX_SPOOL) return -1;
        entry = &jes->spool[jes->spool_count++];
        memset(entry, 0, sizeof(*entry));
        entry->job_number   = job_number;
        entry->sysout_class = sysout_class;
        entry->is_closed    = false;
        entry->line_capacity = 64;
        entry->lines = (char **)calloc(64, sizeof(char *));

        memset(entry->stepname, ' ', JES_STEPNAME_LEN);
        if (stepname) strncpy(entry->stepname, stepname, JES_STEPNAME_LEN);
        entry->stepname[JES_STEPNAME_LEN] = '\0';

        memset(entry->ddname, ' ', JES_DDNAME_LEN);
        if (ddname) strncpy(entry->ddname, ddname, JES_DDNAME_LEN);
        entry->ddname[JES_DDNAME_LEN] = '\0';

        /* Look up jobname */
        for (int i = 0; i < jes->job_count; i++) {
            if (jes->jobs[i].job_number == job_number) {
                strncpy(entry->jobname, jes->jobs[i].jobname, JES_JOBNAME_LEN);
                entry->jobname[JES_JOBNAME_LEN] = '\0';
                break;
            }
        }
    }

    if (!entry->lines) return -1;

    /* Grow if needed */
    if (entry->line_count >= entry->line_capacity) {
        int newcap = entry->line_capacity * 2;
        char **newlines = (char **)realloc(entry->lines, newcap * sizeof(char *));
        if (!newlines) return -1;
        entry->lines = newlines;
        entry->line_capacity = newcap;
    }

    entry->lines[entry->line_count++] = strdup(line ? line : "");
    return 0;
}

void zos_jes2_spool_close(int spool_idx) {
    if (spool_idx >= 0 && spool_idx < zos_system.jes2.spool_count) {
        zos_system.jes2.spool[spool_idx].is_closed = true;
    }
}
