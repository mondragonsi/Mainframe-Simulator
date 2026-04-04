# Spec 06: z/OS Address Space Simulation

## IBM Reference
- IBM z/OS MVS Programming: Authorized Assembler Services Guide (SA23-1371)
- IBM IMS Version 15 System Administration Guide (SC27-6237)
- IBM DB2 for z/OS Installation and Migration Guide (GC27-8843)

## Goal

Simulate the z/OS address space model so students understand **why** mainframe applications are structured the way they are: separate address spaces for IMS control, application regions, DB2, JES2 — each with its own virtual storage, communicating through controlled interfaces (SVC, PC, cross-memory).

This is both a **teaching tool** and an **architectural foundation**: the simulator's internal process model mirrors the real z/OS subsystem layout.

---

## Address Space Architecture

### Conceptual Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│                         z/OS MVS Kernel                              │
│   Handles dispatching, I/O, storage management, SVC routing          │
├──────────────┬──────────────┬──────────────┬────────────────────────┤
│   JES2       │ IMS Control  │   DB2        │  RACF / SMF / Logger   │
│   Address    │   Region     │   Address    │  System Subsystems      │
│   Space      │  (IMSCTL)    │   Space      │                        │
│              │              │  (DSNMSTR)   │                        │
├──────────────┴──────┬───────┴──────────────┴────────────────────────┤
│                     │    Common Service Area (CSA)                   │
│                     │    Shared control blocks visible to all AS      │
├─────────────────────┼─────────────────────────────────────────────  │
│  MPP Region 1       │  MPP Region 2    │  BMP Region    │ Batch AS  │
│  (COBOL online txn) │  (another txn)   │  (Batch+DB)    │ (JCL job) │
└─────────────────────┴──────────────────┴────────────────┴───────────┘
```

### Virtual Storage Map (per address space)

```
High address (2GB in 31-bit, 16EB in 64-bit)
┌─────────────────────────────────────┐
│  ESQA / ECSA (extended, above 16MB) │  System-wide shared
├─────────────────────────────────────┤
│  LSQA (Local System Queue Area)     │  Per-AS system data
│  SWA (Scheduler Work Area)          │  JCL, DD info
├─────────────────────────────────────┤
│  PRIVATE AREA                       │
│  ┌───────────────────────────────┐  │
│  │ User program (COBOL/PL/I)     │  │
│  │ Working Storage               │  │
│  │ I/O buffers                   │  │
│  │ PSB/PCB control blocks        │  │
│  └───────────────────────────────┘  │
├─────────────────────────────────────┤ 16MB line
│  Nucleus (MVS kernel)               │  Key 0, fetch-protected
│  PSA (Prefixed Storage Area @ 0)    │  CPU-specific
│  SQA (System Queue Area)            │  System-wide shared
│  CSA (Common Service Area)          │  System-wide shared
└─────────────────────────────────────┘
Low address (0)
```

---

## Simulated Address Spaces

Each "address space" in the simulator is a **C structure** (`ZOS_ADDRESS_SPACE`) with its own storage context, status, and identity. They communicate through a simulated **CSA** (shared memory region) and a **SVC dispatch table**.

### Data Structures

```c
/* src/zos/address_space.h */

#define AS_NAME_LEN     8
#define AS_MAX_COUNT    32
#define CSA_SIZE        (4 * 1024 * 1024)   /* 4MB simulated CSA */
#define PRIVATE_SIZE    (8 * 1024 * 1024)   /* 8MB per private area */

typedef enum {
    AS_TYPE_MVS,        /* z/OS kernel (always present) */
    AS_TYPE_JES2,       /* Job Entry Subsystem */
    AS_TYPE_IMS_CTL,    /* IMS Control Region */
    AS_TYPE_IMS_MPP,    /* IMS MPP Region (online) */
    AS_TYPE_IMS_BMP,    /* IMS BMP Region (batch+DB) */
    AS_TYPE_IMS_DBRC,   /* IMS DBRC */
    AS_TYPE_DB2,        /* DB2 address space */
    AS_TYPE_BATCH,      /* Batch job step address space */
    AS_TYPE_TSO,        /* TSO user address space */
    AS_TYPE_RACF,       /* RACF subsystem */
} ZOS_AS_TYPE;

typedef enum {
    AS_STATUS_ACTIVE,
    AS_STATUS_WAITING,      /* Waiting for I/O or event */
    AS_STATUS_SWAPPED_OUT,
    AS_STATUS_TERMINATING,
    AS_STATUS_TERMINATED,
} ZOS_AS_STATUS;

typedef struct {
    int  asid;                      /* Address Space ID (1-65535) */
    char name[AS_NAME_LEN + 1];    /* e.g., "IMSCTL  ", "DSNMSTR " */
    ZOS_AS_TYPE  type;
    ZOS_AS_STATUS status;

    /* Private storage area (simulated virtual memory) */
    unsigned char *private_storage;
    size_t private_size;
    size_t private_used;

    /* Storage protection key (0=system, 7=user typical) */
    int storage_key;

    /* Execution state */
    int  dispatch_priority;         /* Higher = dispatched first */
    bool is_system_key;             /* Running in key 0? */

    /* Current program executing in this AS */
    char program_name[AS_NAME_LEN + 1];
    int  return_code;               /* Step completion code */
    bool abended;
    int  abend_code;                /* S0C4, S222, etc. */
    bool is_user_abend;             /* false=Sxxx, true=Uxxx */

    /* Subsystem-specific context (cast as needed) */
    void *subsystem_context;

    /* SMF accounting */
    long cpu_time_ms;
    long elapsed_time_ms;
} ZOS_ADDRESS_SPACE;

/* The z/OS system — owns all address spaces and shared areas */
typedef struct {
    ZOS_ADDRESS_SPACE *spaces[AS_MAX_COUNT];
    int space_count;

    /* Common Service Area — visible to all address spaces */
    unsigned char csa[CSA_SIZE];
    size_t csa_used;

    /* SVC dispatch table */
    int (*svc_table[256])(ZOS_ADDRESS_SPACE *caller, void *parm);

    /* Currently dispatched address space */
    ZOS_ADDRESS_SPACE *current_as;

    /* System logger */
    FILE *system_log;
} ZOS_SYSTEM;
```

---

## Subsystem Startup Sequence

When the simulator initializes, it boots subsystems in the correct z/OS order:

```
1. MVS Kernel AS (ASID 1)     — always first, owns SVC table, dispatcher
2. RACF AS                    — security, must be up before any resource access
3. JES2 AS                    — job entry, spool
4. IMS Control Region         — IMS nucleus, buffer pools, lock manager, log
5. IMS DBRC                   — database recovery control
6. DB2 AS (DSNMSTR)          — DB2 engine, buffer pools
7. TSO AS (one per user)      — interactive session
8. Batch/MPP/BMP             — created dynamically per job/transaction
```

```c
/* src/zos/zos_init.c */
int zos_boot(ZOS_SYSTEM *sys) {
    zos_as_create(sys, "MVSSYS  ", AS_TYPE_MVS,       0);  /* ASID 1 */
    zos_as_create(sys, "RACF    ", AS_TYPE_RACF,       1);  /* ASID 2 */
    zos_as_create(sys, "JES2    ", AS_TYPE_JES2,       2);  /* ASID 3 */
    zos_as_create(sys, "IMSCTL  ", AS_TYPE_IMS_CTL,    3);  /* ASID 4 */
    zos_as_create(sys, "IMSDBRC ", AS_TYPE_IMS_DBRC,   3);  /* ASID 5 */
    zos_as_create(sys, "DSNMSTR ", AS_TYPE_DB2,        3);  /* ASID 6 */
    return 0;
}
```

---

## SVC (Supervisor Call) Simulation

SVCs are the mechanism programs use to request system services. In z/OS, an SVC instruction causes a hardware interrupt; MVS routes it to the appropriate handler.

In the simulator, SVCs are direct C function calls through the SVC table:

```c
/* Key SVCs to simulate */
#define SVC_GETMAIN     4    /* Allocate storage */
#define SVC_FREEMAIN    5    /* Free storage */
#define SVC_OPEN        19   /* Open dataset */
#define SVC_CLOSE       20   /* Close dataset */
#define SVC_DLI         30   /* IMS DL/I call (CBLTDLI) */
#define SVC_ABEND       13   /* Abend */
#define SVC_WAIT        1    /* Wait for event */
#define SVC_POST        2    /* Post event complete */
#define SVC_WTO         35   /* Write to operator */
#define SVC_TGET        93   /* TSO terminal get */
#define SVC_TPUT        94   /* TSO terminal put */

int svc_dli(ZOS_ADDRESS_SPACE *caller, void *parm) {
    /* 
     * Educational note: In real z/OS, SVC 30 switches from
     * the MPP/BMP address space to the IMS control region.
     * Here we simulate the cross-AS call directly.
     */
    DLI_PARM *p = (DLI_PARM *)parm;
    return ims_dispatch_dli(p->func, p->pcb, p->io_area, p->ssa, p->ssa_count);
}
```

---

## IMS Address Space Detail

### IMS Control Region (IMSCTL)

The brain of IMS. All DL/I requests come here regardless of which MPP/BMP region issued them.

```c
typedef struct {
    /* DL/I nucleus — resident in IMS CTL private storage */
    void *dli_nucleus;

    /* Buffer pools */
    unsigned char *db_buffer_pool;      /* Database I/O buffers */
    size_t db_bp_size;
    unsigned char *msg_buffer_pool;     /* Message queue buffers */
    size_t msg_bp_size;

    /* Lock manager — serializes concurrent DB access */
    /* (single-threaded simulator: simplified) */

    /* IMS log buffers (OLDS - Online Log Data Set) */
    ZOS_DATASET *olds;                  /* Active log */
    ZOS_DATASET *wads;                  /* Write-ahead data set */

    /* Active PSBs across all regions */
    IMS_PSB *active_psbs[64];
    int active_psb_count;

    /* Transaction scheduler */
    IMS_MSG_QUEUE *input_queue;
    IMS_TRANSACTION_DEF *transactions[256];
    int txn_count;

    /* IMS system console */
    char imsid[5];
    bool is_active;
} IMS_CTL_CONTEXT;
```

### MPP Region (IMSMPP)

Created dynamically for each online transaction. The COBOL application runs here.

```c
typedef struct {
    IMS_PSB  *scheduled_psb;            /* PSB scheduled for this transaction */
    IMS_REGION *ims_region;             /* IMS region control block */
    char txn_code[9];                   /* Transaction being processed */
    char lterm[9];                      /* Originating terminal */

    /* Application program loaded here */
    char program_name[9];
    /* In simulator: function pointer to COBOL program entry */
    int (*program_entry)(IMS_IOPCB *, IMS_PCB **, void *);

    /* Working storage (COBOL's DATA DIVISION) */
    CobolProgram *cobol_ctx;

    /* Message being processed */
    IMS_MESSAGE *current_message;
} IMS_MPP_CONTEXT;
```

---

## DB2 Address Space Detail

```c
typedef struct {
    /* DB2 engine */
    DB2_CONTEXT *db2;

    /* Buffer pool (simulated) */
    unsigned char *buffer_pool;
    size_t bp_size;
    int bp_hit_count;
    int bp_miss_count;

    /* Active threads (one per COBOL program accessing DB2) */
    struct DB2_THREAD {
        int asid;                       /* Which AS is using this thread */
        SQLCA *sqlca;                   /* Thread's SQLCA */
        char plan_name[9];              /* Bound plan */
        char correlation_id[13];        /* For accounting/monitoring */
        bool in_unit_of_work;
    } threads[64];
    int thread_count;

    /* SYSIBM catalog (simulated) */
    DB2_TABLE *catalog_tables[16];
    int catalog_count;
} DB2_AS_CONTEXT;
```

---

## JES2 Address Space Detail

```c
typedef struct {
    /* Job queues by class */
    struct JES_QUEUE {
        char job_class;
        JCL_JOB *jobs[256];
        int job_count;
        int next_job_number;            /* JOB00001, JOB00002, ... */
    } queues[36];                       /* A-Z + 0-9 */

    /* Active initiators */
    struct JES_INITIATOR {
        int id;
        char classes[18];              /* Job classes this initiator processes */
        ZOS_ADDRESS_SPACE *executing;  /* Currently running job AS */
        bool is_active;
    } initiators[16];
    int initiator_count;

    /* Spool (simulated with in-memory log) */
    struct JES_SPOOL_ENTRY {
        int job_number;
        char jobname[9];
        char stepname[9];
        char ddname[9];
        char *data;
        int data_len;
        char sysout_class;
    } spool[4096];
    int spool_count;
} JES2_CONTEXT;
```

---

## Operator Console Commands

The simulator terminal must accept z/OS operator commands:

| Command | Action |
|---------|--------|
| `D A,L` | Display all active address spaces |
| `D IMS,STATUS` | Display IMS control region status |
| `D J,JOB00123` | Display status of specific job |
| `F IMSCTL,STATUS` | Modify IMS — show transaction counts |
| `F IMSCTL,PSTOP TRN HOSP001` | Stop accepting HOSP001 transactions |
| `P IMSMPP01` | Stop MPP region 01 |
| `S IMSMPP01` | Start MPP region 01 |
| `CANCEL JOB00123` | Cancel a running job (S222 abend) |
| `D DB2,THREAD` | Display active DB2 threads |
| `F DB2,DISPLAY THREAD(*)` | Show all DB2 threads |
| `-DB2 DISPLAY BUFFERPOOL(BP0)` | DB2 buffer pool stats |

---

## Student-Facing Display

The simulator ISPF/TSO UI must have an **Address Space Monitor** panel that shows:

```
 IMS MAINFRAME SIMULATOR - ADDRESS SPACE MONITOR         ROW 1 OF 8
 COMMAND ===>

 ASID  NAME      TYPE          STATUS    CPU(ms)  PGMNAME   RC
 ----  --------  ------------  --------  -------  --------  --
 0001  MVSSYS    MVS KERNEL    ACTIVE         --  --        --
 0002  RACF      RACF          ACTIVE          2  IRRMAIN   --
 0003  JES2      JES2          ACTIVE         15  HASJES20  --
 0004  IMSCTL    IMS CTL       ACTIVE         88  DFSMVRC0  --
 0005  IMSDBRC   IMS DBRC      ACTIVE          4  DSPDBRC0  --
 0006  DSNMSTR   DB2           ACTIVE         34  DSNMSTR   --
 0007  IMSMPP01  IMS MPP       ACTIVE          9  HOSPINQ   --
 0008  JOB00123  BATCH         RUNNING       123  MYJOB     --

 F1=HELP  F3=END  F5=REFRESH  F7=UP  F8=DOWN
```

---

## Implementation Files

```
src/zos/
├── address_space.h      # ZOS_ADDRESS_SPACE, ZOS_SYSTEM structs
├── address_space.c      # Create/terminate/dispatch address spaces
├── zos_init.c           # Boot sequence, subsystem startup
├── svc.h                # SVC table definitions
├── svc.c                # SVC handler implementations
├── csa.c                # CSA GETMAIN/FREEMAIN
├── console.c            # Operator command parser and execution
└── as_monitor.c         # Address space monitor panel
```

---

## Tests

- `test_as_boot_sequence`: verify all 6 system AS created in correct order with correct ASIDs
- `test_as_mpp_create_terminate`: create MPP AS, schedule PSB, execute DL/I, terminate, verify RC
- `test_svc_dli_routing`: COBOL in batch AS issues SVC 30, verify routes to IMS CTL, returns correct status
- `test_svc_getmain`: allocate private storage, verify bounds, free, verify reusable
- `test_svc_abend_s0c7`: trigger data exception in COBOL, verify S0C7 abend recorded on AS
- `test_jes2_job_flow`: submit JCL job, verify queued, initiator picks up, executes, RC on spool
- `test_console_d_a_l`: `D A,L` command returns all active address spaces
- `test_as_monitor_panel`: AS monitor panel shows correct ASID/NAME/STATUS for all active spaces
