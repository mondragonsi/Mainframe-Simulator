/*
 * z/OS Address Space Simulation
 *
 * Models the MVS address space architecture:
 * - Each subsystem (IMS, DB2, JES2, RACF) runs in its own address space
 * - Address spaces communicate via SVC calls (simulated) and CSA (shared area)
 * - Storage protection keys control access between address spaces
 *
 * IBM Reference: z/OS MVS Programming: Authorized Assembler Services Guide
 *                SA23-1371
 */

#ifndef ZOS_ADDRESS_SPACE_H
#define ZOS_ADDRESS_SPACE_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* =========================================================================
 * CONSTANTS
 * ========================================================================= */

#define ZOS_AS_NAME_LEN      8
#define ZOS_PGMNAME_LEN      8
#define ZOS_MAX_AS           32
#define ZOS_CSA_SIZE         (512 * 1024)    /* 512KB simulated CSA */
#define ZOS_PRIVATE_DEFAULT  (256 * 1024)    /* 256KB simulated private area */
#define ZOS_MAX_SVC          256
#define ZOS_SPOOL_MAX        4096
#define ZOS_MAX_INITIATORS   16
#define ZOS_MAX_JOB_CLASSES  36

/* Storage protection keys (0=system/most privileged, 8=user typical) */
#define ZOS_KEY_MVS       0
#define ZOS_KEY_IMS       5
#define ZOS_KEY_DB2       6
#define ZOS_KEY_USER      8

/* ASID assignments (fixed for system address spaces) */
#define ASID_MVS          1
#define ASID_RACF         2
#define ASID_JES2         3
#define ASID_IMS_CTL      4
#define ASID_IMS_DBRC     5
#define ASID_DB2          6
#define ASID_FIRST_DYNAMIC 7   /* Dynamic ASIDs start here */

/* =========================================================================
 * ENUMERATIONS
 * ========================================================================= */

typedef enum {
    AS_TYPE_MVS,        /* z/OS kernel */
    AS_TYPE_RACF,       /* Security subsystem */
    AS_TYPE_JES2,       /* Job Entry Subsystem 2 */
    AS_TYPE_IMS_CTL,    /* IMS Control Region */
    AS_TYPE_IMS_MPP,    /* IMS Message Processing Program region */
    AS_TYPE_IMS_BMP,    /* IMS Batch Message Processing region */
    AS_TYPE_IMS_IFP,    /* IMS IMS Fast Path region */
    AS_TYPE_IMS_DBRC,   /* IMS Database Recovery Control */
    AS_TYPE_DB2,        /* DB2 address space (DSNMSTR) */
    AS_TYPE_BATCH,      /* Batch job step */
    AS_TYPE_TSO,        /* TSO user session */
} ZOS_AS_TYPE;

typedef enum {
    AS_STATUS_STARTING,
    AS_STATUS_ACTIVE,
    AS_STATUS_WAITING,       /* Waiting for I/O or post */
    AS_STATUS_SWAPPED_OUT,
    AS_STATUS_TERMINATING,
    AS_STATUS_TERMINATED,
} ZOS_AS_STATUS;

typedef enum {
    ABEND_NONE = 0,
    ABEND_S0C1 = 0x0C1,   /* Invalid instruction */
    ABEND_S0C4 = 0x0C4,   /* Storage protection exception */
    ABEND_S0C7 = 0x0C7,   /* Data exception (bad numeric data) */
    ABEND_S0CB = 0x0CB,   /* Division by zero */
    ABEND_S222 = 0x222,   /* Operator cancel */
    ABEND_S322 = 0x322,   /* Time limit exceeded */
    ABEND_S806 = 0x806,   /* Load module not found */
    ABEND_S837 = 0x837,   /* REGION too small */
    ABEND_S878 = 0x878,   /* GETMAIN failure */
} ZOS_ABEND_CODE;

/* =========================================================================
 * STORAGE MAP
 *
 * Simulated virtual storage layout per address space.
 * Values are symbolic sizes (not backed by actual separate memory).
 * ========================================================================= */

typedef struct {
    size_t nucleus_size;     /* MVS nucleus (key 0) */
    size_t psa_size;         /* Prefixed Storage Area */
    size_t sqa_size;         /* System Queue Area */
    size_t csa_size;         /* Common Service Area (shared) */
    size_t lsqa_size;        /* Local System Queue Area */
    size_t swa_size;         /* Scheduler Work Area (JCL, DDs) */
    size_t private_size;     /* Private area (user program) */
    size_t private_used;     /* Bytes currently allocated */
    size_t ecsa_size;        /* Extended CSA (above 16MB line) */
} ZOS_STORAGE_MAP;

/* =========================================================================
 * CSA ALLOCATION BLOCK
 *
 * Tracks allocations within the Common Service Area.
 * ========================================================================= */

#define CSA_MAX_ALLOCS 256
#define CSA_OWNER_LEN  9

typedef struct {
    size_t   offset;                 /* Byte offset into CSA buffer */
    size_t   size;                   /* Allocated size */
    int      owner_asid;             /* Who allocated it */
    char     owner_name[CSA_OWNER_LEN]; /* Symbolic owner name */
    bool     in_use;
} ZOS_CSA_BLOCK;

/* =========================================================================
 * ADDRESS SPACE CONTROL BLOCK (ASCB)
 *
 * The central control structure for one z/OS address space.
 * Mirrors IBM's ASCB (Address Space Control Block).
 * ========================================================================= */

typedef struct ZOS_ADDRESS_SPACE {
    /* Identity */
    int            asid;                       /* Address Space ID */
    char           name[ZOS_AS_NAME_LEN + 1];  /* e.g., "IMSCTL  " */
    ZOS_AS_TYPE    type;
    ZOS_AS_STATUS  status;

    /* Program executing */
    char           pgm_name[ZOS_PGMNAME_LEN + 1]; /* Like DFSMVRC0, DSNMSTR */
    int            dispatch_priority;              /* Higher = runs first */
    int            storage_key;

    /* Timing and accounting */
    time_t         start_time;
    long           cpu_time_ms;                /* Simulated CPU time */
    long           elapsed_time_ms;

    /* Virtual storage map (symbolic) */
    ZOS_STORAGE_MAP storage;

    /* Execution outcome */
    int            return_code;
    bool           abended;
    ZOS_ABEND_CODE abend_code;
    bool           is_user_abend;             /* false=Sxxx, true=Uxxx */
    int            user_abend_code;           /* For Uxxx codes */

    /* Subsystem-specific data — cast by each subsystem */
    void          *subsystem_ctx;

    /* Linked list of private allocations (GETMAIN simulation) */
    struct ZOS_GETMAIN_ENTRY {
        void  *ptr;
        size_t size;
        int    subpool;
    } getmain_table[128];
    int getmain_count;

} ZOS_ADDRESS_SPACE;

/* =========================================================================
 * JES2 SPOOL ENTRY
 * ========================================================================= */

#define JES_JOBNAME_LEN  8
#define JES_STEPNAME_LEN 8
#define JES_DDNAME_LEN   8
#define JES_SPOOL_LINELEN 133

typedef struct {
    int  job_number;
    char jobname[JES_JOBNAME_LEN + 1];
    char stepname[JES_STEPNAME_LEN + 1];
    char ddname[JES_DDNAME_LEN + 1];
    char sysout_class;
    char **lines;
    int  line_count;
    int  line_capacity;
    bool is_closed;
} ZOS_SPOOL_ENTRY;

/* =========================================================================
 * JES2 CONTEXT
 * ========================================================================= */

#define JES_MAX_JOBS       256
#define JES_MAX_SPOOL      512

typedef struct {
    int next_job_number;

    /* Job queue (simple FIFO by class) */
    struct JES_JOB_ENTRY {
        int  job_number;
        char jobname[JES_JOBNAME_LEN + 1];
        char job_class;
        char msgclass;
        int  asid;              /* ASID of address space running this job */
        int  return_code;
        bool is_running;
        bool is_complete;
    } jobs[JES_MAX_JOBS];
    int job_count;

    /* Initiators */
    struct JES_INITIATOR {
        int  id;
        char classes[18];    /* Job classes this initiator accepts */
        int  active_asid;    /* Currently running job's ASID, 0 if idle */
        bool is_active;
    } initiators[ZOS_MAX_INITIATORS];
    int initiator_count;

    /* Spool */
    ZOS_SPOOL_ENTRY spool[JES_MAX_SPOOL];
    int spool_count;
} ZOS_JES2_CTX;

/* =========================================================================
 * IMS CONTROL REGION CONTEXT
 * ========================================================================= */

typedef struct {
    char imsid[5];
    bool is_active;
    /* Points back to the main IMS_SYSTEM — set during boot */
    void *ims_system_ptr;

    /* Buffer pool sizes (simulated) */
    size_t db_buffer_pool_size;
    size_t msg_buffer_pool_size;

    /* Active region ASIDs */
    int mpp_asids[16];
    int mpp_count;
    int bmp_asids[8];
    int bmp_count;

    /* Statistics */
    long dli_calls_total;
    long dli_calls_ok;
    long dli_calls_err;
    long checkpoint_count;
} ZOS_IMS_CTL_CTX;

/* =========================================================================
 * DB2 CONTEXT (per address space)
 * ========================================================================= */

#define DB2_MAX_THREADS 64

typedef struct {
    /* Points to DB2_CONTEXT in src/db2/ (planned) */
    void *db2_engine;

    /* Buffer pool stats */
    size_t buffer_pool_size;
    long   bp_getpage;
    long   bp_read;

    /* Active threads (one per attached address space) */
    struct DB2_THREAD_SLOT {
        int  caller_asid;
        char plan_name[9];
        char correlation_id[13];
        bool in_uow;             /* In unit of work (transaction) */
        int  sqlcode_last;
    } threads[DB2_MAX_THREADS];
    int thread_count;

    char subsystem_name[5];     /* e.g., "DB2T" */
} ZOS_DB2_CTX;

/* =========================================================================
 * z/OS SYSTEM — THE ROOT STRUCTURE
 *
 * Owns all address spaces and the shared CSA.
 * One global instance: zos_system
 * ========================================================================= */

typedef struct {
    /* All address spaces */
    ZOS_ADDRESS_SPACE *spaces[ZOS_MAX_AS];
    int space_count;
    int next_asid;

    /* Currently dispatched address space (simulated) */
    int current_asid;

    /* Common Service Area — shared memory visible to all AS */
    unsigned char    csa_buffer[ZOS_CSA_SIZE];
    ZOS_CSA_BLOCK    csa_allocs[CSA_MAX_ALLOCS];
    int              csa_alloc_count;
    size_t           csa_used;

    /* SVC dispatch table */
    int (*svc_table[ZOS_MAX_SVC])(int caller_asid, void *parm);

    /* JES2 context (lives in the JES2 AS) */
    ZOS_JES2_CTX     jes2;

    /* System state */
    bool   is_booted;
    time_t boot_time;
    char   sysplex_name[9];   /* e.g., "PLEX1   " */
    char   system_name[9];    /* e.g., "SYS1    " */

    /* System log (operator messages — WTO) */
    char   syslog[65536];     /* In-memory syslog buffer */
    int    syslog_len;

} ZOS_SYSTEM;

/* Global instance */
extern ZOS_SYSTEM zos_system;

/* =========================================================================
 * FUNCTION PROTOTYPES
 * ========================================================================= */

/* System initialization and shutdown */
int  zos_init(const char *sysname, const char *plexname);
void zos_shutdown(void);

/* Address space lifecycle */
ZOS_ADDRESS_SPACE *zos_as_create(const char *name, ZOS_AS_TYPE type,
                                  int priority, int storage_key);
int  zos_as_activate(ZOS_ADDRESS_SPACE *as);
void zos_as_terminate(ZOS_ADDRESS_SPACE *as, int return_code);
void zos_as_abend(ZOS_ADDRESS_SPACE *as, ZOS_ABEND_CODE code, bool is_user, int user_code);

/* Address space lookup */
ZOS_ADDRESS_SPACE *zos_as_find_asid(int asid);
ZOS_ADDRESS_SPACE *zos_as_find_name(const char *name);
ZOS_ADDRESS_SPACE *zos_as_find_type(ZOS_AS_TYPE type);

/* Storage management */
void *zos_getmain(ZOS_ADDRESS_SPACE *as, size_t size, int subpool);
void  zos_freemain(ZOS_ADDRESS_SPACE *as, void *ptr);
void *zos_csa_getmain(size_t size, int owner_asid, const char *owner_name);
void  zos_csa_freemain(void *ptr);

/* SVC dispatch */
int zos_svc(int caller_asid, int svc_number, void *parm);

/* WTO (Write to Operator) */
void zos_wto(int caller_asid, const char *message);

/* JES2 operations */
int  zos_jes2_submit_job(const char *jobname, char job_class, char msgclass);
int  zos_jes2_complete_job(int job_number, int return_code);
int  zos_jes2_spool_write(int job_number, const char *stepname,
                           const char *ddname, char sysout_class,
                           const char *line);
void zos_jes2_spool_close(int spool_idx);

/* Type name strings (for display) */
const char *zos_as_type_name(ZOS_AS_TYPE type);
const char *zos_as_status_name(ZOS_AS_STATUS status);
const char *zos_abend_name(ZOS_ABEND_CODE code, bool is_user, int user_code);

#endif /* ZOS_ADDRESS_SPACE_H */
