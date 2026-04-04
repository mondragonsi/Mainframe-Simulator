/*
 * z/OS Boot Sequence and System Initialization
 *
 * Simulates the z/OS IPL (Initial Program Load) sequence.
 * Subsystems are started in the correct dependency order,
 * mirroring real z/OS startup procedures.
 *
 * Real z/OS IPL sequence (simplified):
 *   1. Hardware loads NUC from volume (IPLPARM)
 *   2. Master Scheduler (MSTR) initialized
 *   3. JES2/JES3 started
 *   4. System subsystems (RACF, SMF, etc.) started
 *   5. Started tasks from JES2 PROC library
 *   6. Operator interface ready
 *
 * IBM Reference: z/OS MVS Initialization and Tuning Guide (SA23-1379)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "address_space.h"
#include "svc.h"
#include "../core/ims.h"
#include "../datasets/datasets.h"

/* Forward declaration — ims_init is in src/core/ims_system.c */
extern int ims_init(const char *imsid);

/* =========================================================================
 * INTERNAL HELPERS
 * ========================================================================= */

static void print_ipl_message(const char *msg) {
    printf("\033[36m  %s\033[0m\n", msg);
}

static ZOS_ADDRESS_SPACE *boot_as(const char *name, ZOS_AS_TYPE type,
                                   int priority, int key) {
    ZOS_ADDRESS_SPACE *as = zos_as_create(name, type, priority, key);
    if (!as) {
        fprintf(stderr, "FATAL: Failed to create address space %s\n", name);
        return NULL;
    }
    zos_as_activate(as);
    return as;
}

/* =========================================================================
 * SUBSYSTEM CONTEXT INITIALIZATION
 * ========================================================================= */

static void init_jes2_ctx(void) {
    ZOS_JES2_CTX *jes = &zos_system.jes2;
    memset(jes, 0, sizeof(ZOS_JES2_CTX));
    jes->next_job_number = 1;

    /* Define default initiators (Class A-E) */
    jes->initiator_count = 3;
    jes->initiators[0].id = 1;
    strncpy(jes->initiators[0].classes, "ABCDE", 17);
    jes->initiators[0].is_active = true;

    jes->initiators[1].id = 2;
    strncpy(jes->initiators[1].classes, "ABCDE", 17);
    jes->initiators[1].is_active = true;

    jes->initiators[2].id = 3;
    strncpy(jes->initiators[2].classes, "FGHIJ", 17);
    jes->initiators[2].is_active = true;
}

static ZOS_IMS_CTL_CTX *init_ims_ctl_ctx(const char *imsid) {
    ZOS_IMS_CTL_CTX *ctx = (ZOS_IMS_CTL_CTX *)calloc(1, sizeof(ZOS_IMS_CTL_CTX));
    if (!ctx) return NULL;

    strncpy(ctx->imsid, imsid, 4);
    ctx->imsid[4]             = '\0';
    ctx->is_active            = true;
    ctx->ims_system_ptr       = &ims_system;
    ctx->db_buffer_pool_size  = 4 * 1024 * 1024;  /* 4MB DB buffer pool */
    ctx->msg_buffer_pool_size = 1 * 1024 * 1024;  /* 1MB message buffer pool */
    ctx->mpp_count            = 0;
    ctx->bmp_count            = 0;

    return ctx;
}

static ZOS_DB2_CTX *init_db2_ctx(void) {
    ZOS_DB2_CTX *ctx = (ZOS_DB2_CTX *)calloc(1, sizeof(ZOS_DB2_CTX));
    if (!ctx) return NULL;

    strncpy(ctx->subsystem_name, "DB2T", 4);
    ctx->subsystem_name[4] = '\0';
    ctx->buffer_pool_size  = 8 * 1024 * 1024;  /* 8MB BP0 */
    ctx->db2_engine        = NULL;  /* Populated when DB2 module is implemented */

    return ctx;
}

/* =========================================================================
 * MAIN IPL ENTRY POINT
 * ========================================================================= */

int zos_init(const char *sysname, const char *plexname) {
    memset(&zos_system, 0, sizeof(ZOS_SYSTEM));
    zos_system.next_asid  = 1;
    zos_system.boot_time  = time(NULL);
    zos_system.is_booted  = false;
    zos_system.csa_used   = 0;

    /* System identification */
    memset(zos_system.system_name, ' ', 8);
    memset(zos_system.sysplex_name, ' ', 8);
    memcpy(zos_system.system_name,  "SYS1    ", 8);
    memcpy(zos_system.sysplex_name, "PLEX1   ", 8);
    if (sysname)  memcpy(zos_system.system_name,  sysname,  strnlen(sysname,  8));
    if (plexname) memcpy(zos_system.sysplex_name, plexname, strnlen(plexname, 8));
    zos_system.system_name[8]  = '\0';
    zos_system.sysplex_name[8] = '\0';

    printf("\n");
    printf("\033[1;37m");
    printf("  ============================================================\n");
    printf("   z/OS SIMULATOR - IPL SEQUENCE\n");
    printf("   System: %-8s   Sysplex: %-8s\n",
           zos_system.system_name, zos_system.sysplex_name);
    printf("  ============================================================\n");
    printf("\033[0m");

    /*
     * STEP 1: Initialize SVC dispatch table
     * Must be done before any address space can issue SVCs.
     */
    print_ipl_message("IEA101A SYSTEM INITIALIZATION -- INSTALLING SVC TABLE");
    svc_init();

    /*
     * STEP 1b: Initialize ICF Catalog (DFSMS)
     * The catalog must be initialized before any dataset can be allocated.
     * In real z/OS this is the master catalog (SYS1.MASTER.ICFCAT).
     */
    print_ipl_message("IGG001I ICF CATALOG (DFSMS) INITIALIZATION COMPLETE");
    ds_catalog_init("SYS1.MASTER.ICFCAT");

    /*
     * STEP 2: MVS Kernel Address Space (ASID 0001)
     * The nucleus. Always ASID 1. Key 0 (most privileged).
     * Contains: interrupt handlers, dispatcher, SVC router,
     *           I/O subsystem, storage manager.
     */
    print_ipl_message("IEA102I INITIALIZING MVS NUCLEUS (ASID=0001)");
    ZOS_ADDRESS_SPACE *mvs_as = boot_as("MVSSYS  ", AS_TYPE_MVS, 255, ZOS_KEY_MVS);
    if (!mvs_as) return -1;

    /*
     * STEP 3: RACF (Resource Access Control Facility) — ASID 0002
     * Security must be active before any resource can be accessed.
     * RACF controls: dataset access, transaction authorization,
     *                SURROGAT class for batch user IDs.
     */
    print_ipl_message("ICH001I RACF INITIALIZATION COMPLETE (ASID=0002)");
    ZOS_ADDRESS_SPACE *racf_as = boot_as("RACF    ", AS_TYPE_RACF, 200, ZOS_KEY_MVS);
    if (!racf_as) return -1;

    /*
     * STEP 4: JES2 Job Entry Subsystem — ASID 0003
     * Controls: job submission, job queue, initiators, spool, SDSF output.
     * Every batch job runs in a step address space started by JES2.
     */
    print_ipl_message("$HASP426 SPECIFY OPTIONS - HASP-II, VERSION JES2 HJE7780");
    print_ipl_message("$HASP160 JES2 INITIALIZATION COMPLETE (ASID=0003)");
    ZOS_ADDRESS_SPACE *jes2_as = boot_as("JES2    ", AS_TYPE_JES2, 190, ZOS_KEY_MVS);
    if (!jes2_as) return -1;
    init_jes2_ctx();

    /*
     * STEP 5: IMS Control Region — ASID 0004
     * The IMS nucleus. All DL/I calls are processed here regardless
     * of which address space (MPP, BMP, batch) issued them.
     *
     * Buffer pools allocated in CSA so all regions can share them.
     * In real z/OS: OSAM, VSAM, DFSPOOL buffers allocated from ECSA.
     */
    print_ipl_message("DFS3650I IMS INITIALIZATION STARTED (ASID=0004)");
    ZOS_ADDRESS_SPACE *ims_ctl = boot_as("IMSCTL  ", AS_TYPE_IMS_CTL, 180, ZOS_KEY_IMS);
    if (!ims_ctl) return -1;

    ZOS_IMS_CTL_CTX *ims_ctx = init_ims_ctl_ctx("IMS1");
    if (!ims_ctx) return -1;
    ims_ctl->subsystem_ctx = ims_ctx;

    /* Allocate IMS buffer pools from CSA */
    void *db_pool = zos_csa_getmain(64 * 1024, ASID_IMS_CTL, "IMSDBPOOL");
    void *msg_pool = zos_csa_getmain(32 * 1024, ASID_IMS_CTL, "IMSMSGPOOL");
    if (!db_pool || !msg_pool) {
        print_ipl_message("DFS3651E IMS CSA BUFFER POOL ALLOCATION FAILED");
        return -1;
    }
    print_ipl_message("DFS3652I IMS BUFFER POOLS ALLOCATED IN CSA");

    /* Initialize the IMS core engine (existing IMS_SYSTEM) */
    if (ims_init("IMS1") != 0) {
        print_ipl_message("DFS3653E IMS ENGINE INITIALIZATION FAILED");
        return -1;
    }
    print_ipl_message("DFS3499I IMS CONTROL REGION INITIALIZATION COMPLETE");

    /*
     * STEP 6: IMS DBRC (Database Recovery Control) — ASID 0005
     * Tracks database registration and recovery information.
     * Maintains RECON (Recovery Control) datasets.
     * Must be started before any IMS database can be opened.
     */
    print_ipl_message("DSP0001I DBRC INITIALIZATION COMPLETE (ASID=0005)");
    ZOS_ADDRESS_SPACE *dbrc_as = boot_as("IMSDBRC ", AS_TYPE_IMS_DBRC, 175, ZOS_KEY_IMS);
    if (!dbrc_as) return -1;

    /*
     * STEP 7: DB2 Address Space (DSNMSTR) — ASID 0006
     * DB2 database manager. Applications attach via the DB2 Call
     * Attachment Facility (CAF) or RRSAF using cross-memory services.
     *
     * DB2 AS components:
     *   - Database services (SQL engine)
     *   - Buffer manager (buffer pools BP0-BP49)
     *   - Lock manager
     *   - Log manager (BSDS, active logs)
     *   - Distributed data facility (DDF for DRDA connections)
     */
    print_ipl_message("DSNJ003I DB2 INITIALIZATION STARTED  (ASID=0006)");
    ZOS_ADDRESS_SPACE *db2_as = boot_as("DSNMSTR ", AS_TYPE_DB2, 170, ZOS_KEY_DB2);
    if (!db2_as) return -1;

    ZOS_DB2_CTX *db2_ctx = init_db2_ctx();
    if (!db2_ctx) return -1;
    db2_as->subsystem_ctx = db2_ctx;

    /* Allocate DB2 buffer pool from CSA */
    void *db2_pool = zos_csa_getmain(128 * 1024, ASID_DB2, "DB2BP0");
    if (!db2_pool) {
        print_ipl_message("DSNJ004E DB2 BUFFER POOL ALLOCATION FAILED");
        return -1;
    }
    print_ipl_message("DSN9022I DB2 INITIALIZATION COMPLETE");

    /*
     * IPL complete.
     */
    zos_system.is_booted = true;

    printf("\033[1;32m");
    printf("  ============================================================\n");
    printf("   IEA101I SYSTEM INITIALIZATION COMPLETE\n");
    printf("   %d ADDRESS SPACES ACTIVE\n", zos_system.space_count);
    printf("   CSA USED: %zu KB of %d KB\n",
           zos_system.csa_used / 1024, ZOS_CSA_SIZE / 1024);
    printf("  ============================================================\n");
    printf("\033[0m\n");

    (void)db_pool; (void)msg_pool; (void)db2_pool;
    (void)jes2_as; (void)racf_as; (void)dbrc_as;

    return 0;
}

/* =========================================================================
 * z/OS SHUTDOWN
 * ========================================================================= */

void zos_shutdown(void) {
    zos_wto(0, "IEE334I SHUTDOWN INITIATED");

    /* Terminate dynamic address spaces first (batch, MPP, BMP, TSO) */
    for (int i = zos_system.space_count - 1; i >= 0; i--) {
        ZOS_ADDRESS_SPACE *as = zos_system.spaces[i];
        if (!as) continue;
        if (as->type == AS_TYPE_BATCH || as->type == AS_TYPE_TSO ||
            as->type == AS_TYPE_IMS_MPP || as->type == AS_TYPE_IMS_BMP) {
            if (as->status == AS_STATUS_ACTIVE) {
                zos_as_terminate(as, 0);
            }
        }
    }

    /* Terminate subsystems in reverse startup order */
    ZOS_AS_TYPE shutdown_order[] = {
        AS_TYPE_DB2, AS_TYPE_IMS_DBRC, AS_TYPE_IMS_CTL,
        AS_TYPE_JES2, AS_TYPE_RACF, AS_TYPE_MVS
    };

    for (int si = 0; si < 6; si++) {
        ZOS_ADDRESS_SPACE *as = zos_as_find_type(shutdown_order[si]);
        if (as && as->status == AS_STATUS_ACTIVE) {
            /* Free subsystem context */
            if (as->subsystem_ctx) {
                free(as->subsystem_ctx);
                as->subsystem_ctx = NULL;
            }
            zos_as_terminate(as, 0);
        }
    }

    /* Free JES2 spool */
    for (int i = 0; i < zos_system.jes2.spool_count; i++) {
        ZOS_SPOOL_ENTRY *e = &zos_system.jes2.spool[i];
        if (e->lines) {
            for (int j = 0; j < e->line_count; j++) {
                free(e->lines[j]);
            }
            free(e->lines);
            e->lines = NULL;
        }
    }

    /* Free all address space structures */
    for (int i = 0; i < zos_system.space_count; i++) {
        if (zos_system.spaces[i]) {
            free(zos_system.spaces[i]);
            zos_system.spaces[i] = NULL;
        }
    }
    zos_system.space_count = 0;
    zos_system.is_booted   = false;

    zos_wto(0, "IEE362A SYSTEM SHUTDOWN COMPLETE");
}

/* =========================================================================
 * DYNAMIC REGION CREATION (called at runtime for MPP/BMP/batch)
 * ========================================================================= */

ZOS_ADDRESS_SPACE *zos_start_mpp_region(const char *txn_code,
                                         const char *psb_name) {
    char as_name[ZOS_AS_NAME_LEN + 1];
    static int mpp_counter = 1;

    snprintf(as_name, sizeof(as_name), "IMSMPP%02d", mpp_counter++);

    ZOS_ADDRESS_SPACE *as = zos_as_create(as_name, AS_TYPE_IMS_MPP,
                                           150, ZOS_KEY_USER);
    if (!as) return NULL;

    /* Update IMS CTL tracking */
    ZOS_ADDRESS_SPACE *ims_ctl = zos_as_find_asid(ASID_IMS_CTL);
    if (ims_ctl && ims_ctl->subsystem_ctx) {
        ZOS_IMS_CTL_CTX *ctx = (ZOS_IMS_CTL_CTX *)ims_ctl->subsystem_ctx;
        if (ctx->mpp_count < 16) {
            ctx->mpp_asids[ctx->mpp_count++] = as->asid;
        }
    }

    /* Set program name to PSB name (application program) */
    if (psb_name) {
        strncpy(as->pgm_name, psb_name, ZOS_PGMNAME_LEN);
        as->pgm_name[ZOS_PGMNAME_LEN] = '\0';
    }

    char msg[80];
    snprintf(msg, sizeof(msg),
             "DFS551I MPP REGION %.8s STARTED FOR TXN %.8s",
             as_name, txn_code ? txn_code : "--------");
    zos_wto(ASID_IMS_CTL, msg);

    zos_as_activate(as);
    return as;
}

ZOS_ADDRESS_SPACE *zos_start_bmp_region(const char *pgm_name) {
    char as_name[ZOS_AS_NAME_LEN + 1];
    static int bmp_counter = 1;

    snprintf(as_name, sizeof(as_name), "IMSBMP%02d", bmp_counter++);

    ZOS_ADDRESS_SPACE *as = zos_as_create(as_name, AS_TYPE_IMS_BMP,
                                           140, ZOS_KEY_USER);
    if (!as) return NULL;

    if (pgm_name) {
        strncpy(as->pgm_name, pgm_name, ZOS_PGMNAME_LEN);
        as->pgm_name[ZOS_PGMNAME_LEN] = '\0';
    }

    /* Update IMS CTL tracking */
    ZOS_ADDRESS_SPACE *ims_ctl = zos_as_find_asid(ASID_IMS_CTL);
    if (ims_ctl && ims_ctl->subsystem_ctx) {
        ZOS_IMS_CTL_CTX *ctx = (ZOS_IMS_CTL_CTX *)ims_ctl->subsystem_ctx;
        if (ctx->bmp_count < 8) {
            ctx->bmp_asids[ctx->bmp_count++] = as->asid;
        }
    }

    zos_as_activate(as);
    return as;
}

ZOS_ADDRESS_SPACE *zos_start_batch_job(const char *jobname,
                                        const char *pgm_name,
                                        char job_class) {
    ZOS_ADDRESS_SPACE *as = zos_as_create(jobname, AS_TYPE_BATCH,
                                           120, ZOS_KEY_USER);
    if (!as) return NULL;

    if (pgm_name) {
        strncpy(as->pgm_name, pgm_name, ZOS_PGMNAME_LEN);
        as->pgm_name[ZOS_PGMNAME_LEN] = '\0';
    }

    /* Register with JES2 */
    int jnum = zos_jes2_submit_job(jobname, job_class, 'X');
    if (jnum >= 0) {
        /* Store job number in return_code field temporarily */
        as->return_code = jnum;
    }

    zos_as_activate(as);
    return as;
}
