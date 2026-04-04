/*
 * IBM IMS Simulator - Main Header
 * 
 * This header defines all core structures and constants for simulating
 * IBM IMS (Information Management System) functionality.
 * 
 * Reference: IBM IMS 15.6 Documentation
 * https://www.ibm.com/docs/en/ims/15.6.0
 */

#ifndef IMS_H
#define IMS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ===========================================================================
 * CONSTANTS
 * ===========================================================================*/

#define IMS_VERSION "15.6.0-SIM"
#define IMS_MAX_SEGMENT_NAME 8
#define IMS_MAX_FIELD_NAME 8
#define IMS_MAX_KEY_LENGTH 256
#define IMS_MAX_SEGMENT_LENGTH 32767
#define IMS_MAX_SEGMENTS_PER_DB 255
#define IMS_MAX_LEVELS 15
#define IMS_MAX_PCBS 255
#define IMS_MAX_SSA 15
#define IMS_MAX_MSG_LENGTH 32767
#define IMS_SPA_HEADER_SIZE 13

/* ===========================================================================
 * STATUS CODES
 * Based on IBM IMS Status Codes Reference
 * ===========================================================================*/

typedef enum {
    /* Successful completion */
    IMS_STATUS_OK = 0,           /* Blank - Successful call */
    
    /* Informational codes */
    IMS_STATUS_GA = 'A',         /* Moved to different hierarchical level */
    IMS_STATUS_GK = 'K',         /* Different segment type at same level */
    IMS_STATUS_QC = 'C',         /* No more messages in queue */
    IMS_STATUS_QD = 'D',         /* No more segments for this message */
    
    /* End conditions */
    IMS_STATUS_GB = 'B',         /* End of database reached */
    IMS_STATUS_GE = 'E',         /* Segment not found */
    
    /* Error codes - Program errors (using unique values > 256) */
    IMS_STATUS_AC = 0x0101,      /* SSA not in hierarchical sequence */
    IMS_STATUS_AH = 0x0102,      /* Required SSA missing */
    IMS_STATUS_AI = 0x0103,      /* Invalid SSA */
    IMS_STATUS_AJ = 0x0104,      /* Invalid SSA combination */
    IMS_STATUS_AK = 0x0105,      /* Invalid field name in SSA */
    IMS_STATUS_AM = 0x0106,      /* Invalid call before insert */
    
    /* Insert/Update errors */
    IMS_STATUS_II = 0x0201,      /* Segment already exists (duplicate key) */
    IMS_STATUS_LB = 0x0202,      /* Segment already loaded */
    IMS_STATUS_LC = 0x0203,      /* Key out of sequence */
    IMS_STATUS_LD = 0x0204,      /* No parent for dependent */
    IMS_STATUS_LE = 0x0205,      /* Hierarchical sequence error */
    
    /* I/O errors */
    IMS_STATUS_AO = 0x0301,      /* I/O error on database */
    
    /* Transaction Manager codes */
    IMS_STATUS_XA = 0x0401,      /* Transaction abended */
    IMS_STATUS_XG = 0x0402,      /* PSB scheduling error */
} IMS_STATUS;

/* Get status code as 2-character string */
#define IMS_STATUS_STR(code) \
    ((code) == IMS_STATUS_OK ? "  " : \
     (code) == IMS_STATUS_GA ? "GA" : \
     (code) == IMS_STATUS_GK ? "GK" : \
     (code) == IMS_STATUS_GB ? "GB" : \
     (code) == IMS_STATUS_GE ? "GE" : \
     (code) == IMS_STATUS_QC ? "QC" : \
     (code) == IMS_STATUS_QD ? "QD" : "??")

/* ===========================================================================
 * DL/I FUNCTION CODES
 * ===========================================================================*/

typedef enum {
    /* Database calls - Get */
    DLI_GU,          /* Get Unique */
    DLI_GN,          /* Get Next */
    DLI_GNP,         /* Get Next within Parent */
    DLI_GHU,         /* Get Hold Unique */
    DLI_GHN,         /* Get Hold Next */
    DLI_GHNP,        /* Get Hold Next within Parent */
    
    /* Database calls - Update */
    DLI_ISRT,        /* Insert */
    DLI_DLET,        /* Delete */
    DLI_REPL,        /* Replace */
    
    /* System service calls */
    DLI_PCB,         /* Schedule PSB */
    DLI_TERM,        /* Terminate PSB */
    DLI_CHKP,        /* Checkpoint */
    DLI_XRST,        /* Extended Restart */
    DLI_ROLB,        /* Rollback */
    DLI_ROLL,        /* Roll */
    DLI_ROLS,        /* Rollback to SETS */
    DLI_SETS,        /* Set point */
    DLI_SETU,        /* Set unconditional */
    
    /* Message calls (IMS TM) */
    DLI_GU_MSG,      /* Get Unique (message) */
    DLI_GN_MSG,      /* Get Next (message segment) */
    DLI_ISRT_MSG,    /* Insert (send message) */
    DLI_PURG,        /* Purge messages */
    DLI_CMD,         /* Issue IMS command */
    DLI_GCMD,        /* Get command response */
    DLI_LOG,         /* Write to IMS log */
} DLI_FUNCTION;

/* ===========================================================================
 * DATABASE STRUCTURES (IMS DB)
 * ===========================================================================*/

/* Field definition within a segment */
typedef struct {
    char name[IMS_MAX_FIELD_NAME + 1];
    int offset;
    int length;
    bool is_key;
    char type;      /* 'C' = char, 'P' = packed, 'X' = hex */
} IMS_FIELD;

/* Segment type definition (from DBD) */
typedef struct IMS_SEGMENT_DEF {
    char name[IMS_MAX_SEGMENT_NAME + 1];
    int level;
    int max_length;
    int min_length;
    int field_count;
    IMS_FIELD fields[32];
    struct IMS_SEGMENT_DEF *parent;
    struct IMS_SEGMENT_DEF *first_child;
    struct IMS_SEGMENT_DEF *next_sibling;
} IMS_SEGMENT_DEF;

/* Database definition (DBD) */
typedef struct {
    char name[9];
    char access_method[5];     /* HISAM, HDAM, HIDAM, HSAM, etc. */
    IMS_SEGMENT_DEF *root;
    IMS_SEGMENT_DEF segments[IMS_MAX_SEGMENTS_PER_DB];
    int segment_count;
} IMS_DBD;

/* Actual segment instance in memory */
typedef struct IMS_SEGMENT {
    IMS_SEGMENT_DEF *definition;
    int length;
    char data[IMS_MAX_SEGMENT_LENGTH];
    struct IMS_SEGMENT *parent;
    struct IMS_SEGMENT *first_child;
    struct IMS_SEGMENT *next_sibling;
    struct IMS_SEGMENT *prev_sibling;   /* For twin segments */
    struct IMS_SEGMENT *next_twin;      /* Next twin segment */
    bool is_held;                        /* Held for update */
} IMS_SEGMENT;

/* ===========================================================================
 * PCB/PSB STRUCTURES
 * ===========================================================================*/

/* Program Communication Block (PCB) */
typedef struct {
    char db_name[9];                     /* Database name */
    int level;                           /* Segment level number */
    IMS_STATUS status_code;              /* Status code from last call */
    char proc_options[5];                /* Processing options: A, G, I, R, D, etc. */
    int sens_seg_count;                  /* Sensitive segment count */
    char segment_name[IMS_MAX_SEGMENT_NAME + 1];  /* Current segment name */
    int key_feedback_length;             /* Length of key feedback area */
    char key_feedback[IMS_MAX_KEY_LENGTH];        /* Concatenated key */
    
    /* Internal pointers (not visible to application) */
    IMS_DBD *dbd;
    IMS_SEGMENT *current_position;
    IMS_SEGMENT *parentage[IMS_MAX_LEVELS];
    int parentage_level;
} IMS_PCB;

/* I/O PCB for message processing */
typedef struct {
    char lterm_name[9];                  /* Logical terminal name */
    char reserved1[2];
    IMS_STATUS status_code;
    char date[4];                        /* Current date (packed) */
    char time[4];                        /* Current time (packed) */
    char input_msg_seq[4];               /* Input message sequence */
    char mod_name[9];                    /* MOD name (MFS) */
    char userid[9];                      /* User ID */
    char group_name[9];                  /* Group name */
    
    /* Internal state */
    int current_msg_id;
    int current_segment;
} IMS_IOPCB;

/* Alternate PCB for sending messages */
typedef struct {
    char dest_name[9];                   /* Destination name (LTERM or TXN) */
    char reserved1[2];
    IMS_STATUS status_code;
    /* Additional fields as needed */
} IMS_ALTPCB;

/* Program Specification Block (PSB) */
typedef struct {
    char name[9];
    char language;                       /* 'C', 'B' (COBOL), 'P' (PL/I) */
    int pcb_count;
    IMS_IOPCB *io_pcb;                   /* First PCB is always I/O PCB */
    IMS_PCB *db_pcbs[IMS_MAX_PCBS];      /* Database PCBs */
    IMS_ALTPCB *alt_pcbs[IMS_MAX_PCBS];  /* Alternate PCBs */
    bool is_conversational;
    int spa_size;
} IMS_PSB;

/* ===========================================================================
 * SSA (SEGMENT SEARCH ARGUMENT) STRUCTURES
 * ===========================================================================*/

typedef enum {
    SSA_OP_EQ,      /* = Equal */
    SSA_OP_NE,      /* != Not equal */
    SSA_OP_GT,      /* > Greater than */
    SSA_OP_GE,      /* >= Greater than or equal */
    SSA_OP_LT,      /* < Less than */
    SSA_OP_LE,      /* <= Less than or equal */
} SSA_OPERATOR;

typedef enum {
    SSA_BOOL_NONE,
    SSA_BOOL_AND,   /* *AND - Multiple conditions AND */
    SSA_BOOL_OR,    /* *OR - Multiple conditions OR */
} SSA_BOOLEAN;

/* Single qualification in an SSA */
typedef struct {
    char field_name[IMS_MAX_FIELD_NAME + 1];
    SSA_OPERATOR op;
    char value[256];
    SSA_BOOLEAN bool_op;  /* Connector to next qualification */
} SSA_QUALIFICATION;

/* Segment Search Argument */
typedef struct {
    char segment_name[IMS_MAX_SEGMENT_NAME + 1];
    bool is_qualified;
    int qualification_count;
    SSA_QUALIFICATION qualifications[8];
    bool command_codes[26];  /* A-Z command codes */
} IMS_SSA;

/* ===========================================================================
 * TRANSACTION MANAGER STRUCTURES (IMS TM)
 * ===========================================================================*/

/* Message types */
typedef enum {
    MSG_TYPE_INPUT,
    MSG_TYPE_OUTPUT,
    MSG_TYPE_CMD,
    MSG_TYPE_RESPONSE,
} IMS_MSG_TYPE;

/* Message in the queue */
typedef struct IMS_MESSAGE {
    int id;
    IMS_MSG_TYPE type;
    char transaction_code[9];
    char lterm[9];
    char userid[9];
    int priority;
    int segment_count;
    int total_length;
    char *segments[100];      /* Message segments */
    int segment_lengths[100];
    struct IMS_MESSAGE *next;
    struct IMS_MESSAGE *prev;
} IMS_MESSAGE;

/* Message Queue */
typedef struct {
    IMS_MESSAGE *head;
    IMS_MESSAGE *tail;
    int count;
    char queue_name[9];
} IMS_MSG_QUEUE;

/* Scratch Pad Area (SPA) for conversational transactions */
typedef struct {
    char header[IMS_SPA_HEADER_SIZE];    /* IMS-defined header */
    int total_size;
    char data[1024];                      /* User data area */
    char transaction_code[9];
    char lterm[9];
} IMS_SPA;

/* Region types */
typedef enum {
    REGION_MPP,     /* Message Processing Program */
    REGION_BMP,     /* Batch Message Processing */
    REGION_IFP,     /* IMS Fast Path */
} IMS_REGION_TYPE;

/* Transaction definition */
typedef struct {
    char code[9];                /* Transaction code */
    char psb_name[9];            /* PSB to use */
    IMS_REGION_TYPE region_type;
    int priority;
    bool is_conversational;
    int spa_size;
    int max_response_time;       /* in seconds, 0 = no limit */
} IMS_TRANSACTION_DEF;

/* Region (execution environment) */
typedef struct {
    int id;
    IMS_REGION_TYPE type;
    char name[9];
    IMS_PSB *current_psb;
    IMS_SPA *spa;
    IMS_MESSAGE *current_msg;
    bool is_active;
    bool in_conversation;
} IMS_REGION;

/* ===========================================================================
 * MAIN IMS SYSTEM CONTEXT
 * ===========================================================================*/

typedef struct {
    /* System identification */
    char imsid[5];
    bool is_running;
    
    /* Database Manager */
    IMS_DBD *dbds[64];
    int dbd_count;
    
    /* Transaction Manager */
    IMS_MSG_QUEUE input_queue;
    IMS_MSG_QUEUE output_queue;
    IMS_REGION regions[16];
    int region_count;
    IMS_TRANSACTION_DEF transactions[256];
    int transaction_count;
    
    /* PSB definitions */
    IMS_PSB *psbs[256];
    int psb_count;
    
    /* Current context for application */
    IMS_REGION *current_region;
    IMS_PSB *current_psb;
    
    /* Statistics */
    int total_calls;
    int successful_calls;
    int failed_calls;
} IMS_SYSTEM;

/* Global IMS system instance */
extern IMS_SYSTEM ims_system;

/* ===========================================================================
 * FUNCTION PROTOTYPES
 * ===========================================================================*/

/* System initialization */
int ims_init(const char *imsid);
void ims_shutdown(void);

/* DL/I Call Interface (main entry point, like CBLTDLI) */
int CBLDLI(DLI_FUNCTION func, void *pcb, void *io_area, ...);

/* Database Manager calls */
int dli_gu(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_gn(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_gnp(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_ghu(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_ghn(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_ghnp(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_isrt(IMS_PCB *pcb, void *io_area, IMS_SSA *ssa[], int ssa_count);
int dli_dlet(IMS_PCB *pcb, void *io_area);
int dli_repl(IMS_PCB *pcb, void *io_area);

/* Transaction Manager calls */
int dli_gu_msg(IMS_IOPCB *pcb, void *io_area);
int dli_gn_msg(IMS_IOPCB *pcb, void *io_area);
int dli_isrt_msg(IMS_IOPCB *pcb, void *io_area, int length);
int dli_isrt_alt(IMS_ALTPCB *pcb, void *io_area, int length);
int dli_purg(IMS_IOPCB *pcb);

/* PSB scheduling */
int dli_pcb(const char *psb_name, IMS_PSB **psb);
int dli_term(IMS_PSB *psb);

/* SSA parsing */
int ssa_parse(const char *ssa_string, IMS_SSA *ssa);
bool ssa_match(IMS_SSA *ssa, IMS_SEGMENT *segment);

/* Message queue operations */
int msg_queue_add(IMS_MSG_QUEUE *queue, IMS_MESSAGE *msg);
IMS_MESSAGE *msg_queue_get(IMS_MSG_QUEUE *queue, const char *txn_code);
void msg_queue_remove(IMS_MSG_QUEUE *queue, IMS_MESSAGE *msg);

/* Region management */
IMS_REGION *region_create(IMS_REGION_TYPE type, const char *name);
int region_schedule(IMS_REGION *region, const char *psb_name);
void region_terminate(IMS_REGION *region);

/* Utility functions */
void ims_log(const char *level, const char *format, ...);
const char *ims_status_desc(IMS_STATUS status);
void ims_display_status(void);
int ims_register_dbd(IMS_DBD *dbd);
int ims_register_psb(IMS_PSB *psb);
int ims_register_transaction(IMS_TRANSACTION_DEF *txn);
IMS_DBD *ims_find_dbd(const char *name);
IMS_PSB *ims_find_psb(const char *name);

#endif /* IMS_H */
