/*
 * z/OS Dataset Simulation — Master Header (DFSMS)
 *
 * Simulates the z/OS Data Facility Storage Management Subsystem (DFSMS).
 * Covers Physical Sequential (PS), Partitioned (PDS/PDSE), VSAM
 * (KSDS/ESDS/RRDS), and Generation Data Groups (GDG).
 *
 * IBM Reference:
 *   z/OS DFSMS Using Data Sets       SC23-6855
 *   z/OS DFSMS Macro Instructions    SC23-6852
 *   z/OS MVS JCL Reference           SA23-1385
 */

#ifndef ZOS_DATASETS_H
#define ZOS_DATASETS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* =========================================================================
 * LIMITS AND CONSTANTS
 * ========================================================================= */

#define DS_DSN_LEN          44   /* Max dataset name length */
#define DS_MEMBER_LEN        8   /* PDS member name */
#define DS_DDNAME_LEN        8   /* DD name */
#define DS_VOLSER_LEN        6   /* Volume serial */
#define DS_MAX_DATASETS    256   /* Max datasets in catalog */
#define DS_MAX_OPEN         64   /* Max simultaneously open DCBs */
#define DS_MAX_MEMBERS     255   /* Max PDS members */
#define DS_MAX_GDG_GENS     255  /* Max GDG generations */
#define DS_MAX_VSAM_RECS  65536  /* Max VSAM records */
#define DS_MAX_KEY_LEN      255  /* Max VSAM key length */
#define DS_MAX_LRECL      32760  /* Max logical record length */
#define DS_MAX_BLKSIZE    32760  /* Max block size */

/* =========================================================================
 * DATASET ORGANIZATION (DSORG)
 *
 * IBM field in the DSCB (Dataset Control Block on VTOC):
 *   PS  = Physical Sequential (DSORG=PS)
 *   PO  = Partitioned (DSORG=PO) — PDS or PDSE
 *   VS  = VSAM (managed by ICF catalog, not VTOC)
 *   GDG = Generation Data Group base (catalog entry only)
 * ========================================================================= */

typedef enum {
    DSORG_UNKNOWN = 0,
    DSORG_PS,        /* Physical Sequential */
    DSORG_PO,        /* Partitioned (PDS/PDSE) */
    DSORG_VS,        /* VSAM cluster */
    DSORG_GDG,       /* GDG base */
} ZOS_DSORG;

/* =========================================================================
 * RECORD FORMAT (RECFM)
 *
 * Controls how records are laid out in blocks.
 *   F  = Fixed length, unblocked
 *   FB = Fixed Blocked (most common for batch)
 *   V  = Variable length, unblocked (4-byte RDW prefix)
 *   VB = Variable Blocked (VB records in blocks with BDW prefix)
 *   U  = Undefined (used for load modules, executable)
 * ========================================================================= */

typedef enum {
    RECFM_UNKNOWN = 0,
    RECFM_F,         /* Fixed unblocked */
    RECFM_FB,        /* Fixed Blocked */
    RECFM_V,         /* Variable unblocked */
    RECFM_VB,        /* Variable Blocked */
    RECFM_U,         /* Undefined */
} ZOS_RECFM;

/* Open modes (like DISP in JCL + DCB OPEN macro) */
typedef enum {
    OPEN_NONE   = 0,
    OPEN_INPUT  = 1,   /* Read only, from beginning */
    OPEN_OUTPUT = 2,   /* Write, truncate existing */
    OPEN_EXTEND = 3,   /* Write, append to end */
    OPEN_INOUT  = 4,   /* Read/write */
    OPEN_RDBACK = 5,   /* Read backwards (tape) */
} ZOS_OPEN_MODE;

/* VSAM cluster type */
typedef enum {
    VSAM_NONE = 0,
    VSAM_KSDS,       /* Key-Sequenced Data Set */
    VSAM_ESDS,       /* Entry-Sequenced Data Set */
    VSAM_RRDS,       /* Relative-Record Data Set */
    VSAM_LDS,        /* Linear Data Set */
} ZOS_VSAM_TYPE;

/* =========================================================================
 * VARIABLE-LENGTH RECORD DESCRIPTOR WORD (RDW)
 *
 * Bytes 0-1: total record length including RDW (big-endian)
 * Bytes 2-3: segment flags (00 00 for non-spanned)
 *
 * IBM Reference: DFSMS Using Data Sets, Chapter 2
 * ========================================================================= */

typedef struct {
    uint16_t length;    /* Record length INCLUDING these 4 bytes */
    uint8_t  flags;     /* Segment flags (00 = complete record) */
    uint8_t  reserved;  /* Always 0x00 */
} ZOS_RDW;

/* =========================================================================
 * IN-MEMORY RECORD STORAGE
 *
 * We store records as a dynamic array of variable-length entries.
 * Each entry holds the raw record data (without RDW — we add RDW on I/O).
 * ========================================================================= */

typedef struct {
    unsigned char *data;  /* Record data */
    int            len;   /* Data length (without RDW) */
} DS_RECORD;

typedef struct {
    DS_RECORD *records;
    int        count;
    int        capacity;
} DS_RECORD_ARRAY;

/* =========================================================================
 * PDS DIRECTORY ENTRY
 *
 * IBM PDS directory block format (256 bytes per block):
 *   Bytes 0-7:   member name (EBCDIC, space-padded)
 *   Bytes 8-10:  TTR (track/record pointer) — simulated as index
 *   Byte  11:    indicator (alias flag, user data length)
 *   Bytes 12-n:  user data (ISPF stats, etc.)
 *
 * End marker: 8 bytes of 0xFF
 * ========================================================================= */

#define PDS_TTR_SIZE  3   /* 3-byte TTR */
#define PDS_USER_DATA 62  /* Max user data bytes per directory entry */

typedef struct {
    char          name[DS_MEMBER_LEN + 1]; /* Null-terminated internally */
    int           record_start;            /* Index into record array */
    int           record_count;
    unsigned char user_data[PDS_USER_DATA];
    int           user_data_len;
    bool          is_alias;
    char          alias_of[DS_MEMBER_LEN + 1];
} ZOS_PDS_MEMBER;

/* =========================================================================
 * VSAM RECORD (in-memory)
 *
 * KSDS: sorted by key field
 * ESDS: insertion order, addressed by RBA
 * RRDS: addressed by relative record number (1-based)
 * ========================================================================= */

typedef struct {
    unsigned char *data;         /* Full record data */
    int            len;          /* Record length */
    unsigned char  key[DS_MAX_KEY_LEN]; /* Extracted key (KSDS) */
    int            key_len;
    long           rba;          /* Relative Byte Address (ESDS) */
    int            rrn;          /* Relative Record Number (RRDS) */
    bool           deleted;      /* Logical delete (slot empty for RRDS) */
} ZOS_VSAM_RECORD;

/* =========================================================================
 * GDG GENERATION ENTRY
 * ========================================================================= */

typedef struct {
    int  gen_number;             /* Absolute: 1, 2, 3, ... */
    char dsn[DS_DSN_LEN + 1];   /* Full DSN: BASE.G0001V00 */
    bool is_active;
    bool is_cataloged;
    int  dataset_idx;            /* Index into catalog */
} ZOS_GDG_GEN;

/* =========================================================================
 * DATASET DEFINITION
 *
 * The central structure. One per allocated dataset in the catalog.
 * Mirrors the real z/OS DSCB (Dataset Control Block) on VTOC,
 * plus VSAM cluster definition in the ICF catalog.
 * ========================================================================= */

typedef struct ZOS_DATASET {
    /* Identity */
    char dsn[DS_DSN_LEN + 1];   /* e.g., "SYS1.PROCLIB" */
    char volser[DS_VOLSER_LEN + 1]; /* e.g., "SYSRES" (simulated) */

    /* Physical attributes */
    ZOS_DSORG dsorg;
    ZOS_RECFM recfm;
    int       lrecl;             /* Logical record length */
    int       blksize;           /* Block size (0 = system default) */

    /* State */
    bool exists;
    bool is_cataloged;
    int  open_count;             /* How many DCBs have it open */

    /* Record storage (PS and PDS data) */
    DS_RECORD_ARRAY records;

    /* PDS: member directory */
    ZOS_PDS_MEMBER members[DS_MAX_MEMBERS];
    int            member_count;

    /* VSAM attributes */
    ZOS_VSAM_TYPE  vsam_type;
    int            key_offset;   /* Offset of key within record (KSDS) */
    int            key_len;      /* Key length (KSDS) */
    int            avg_rec_len;  /* Average record length */
    int            max_rec_len;  /* Maximum record length */

    /* VSAM records */
    ZOS_VSAM_RECORD *vsam_records;
    int              vsam_count;
    int              vsam_capacity;
    long             next_rba;   /* Next RBA for ESDS */

    /* VSAM RPL status codes (last operation) */
    int rtncd;                   /* Return code: 0/8/12 */
    int fdbk;                    /* Feedback code */

    /* GDG base info */
    int  gdg_limit;              /* Max generations to keep */
    bool gdg_empty;              /* EMPTY on limit exceeded */
    bool gdg_scratch;            /* SCRATCH/NOSCRATCH */
    ZOS_GDG_GEN gdg_gens[DS_MAX_GDG_GENS];
    int          gdg_gen_count;

} ZOS_DATASET;

/* =========================================================================
 * DATA CONTROL BLOCK (DCB)
 *
 * One DCB per open dataset. The program works with the DCB, not
 * directly with the dataset definition.
 *
 * In real z/OS: DCB is a 96-byte storage area built from JCL DD
 * attributes + DCB macro in the program + dataset label (DSCB).
 * ========================================================================= */

typedef struct {
    char          ddname[DS_DDNAME_LEN + 1];
    ZOS_DATASET  *dataset;
    ZOS_OPEN_MODE mode;
    bool          is_open;

    /* Current position */
    int  ps_record_idx;          /* Next record to read (PS) */
    char member[DS_MEMBER_LEN + 1]; /* Currently open PDS member */
    int  pds_record_idx;         /* Next record in member */

    /* VSAM positioning */
    int  vsam_pos;               /* Current record index in vsam_records */
    bool vsam_positioned;        /* Has a POINT/GET established position? */

    /* Effective DCB attributes (merged JCL + DSCB + program DCB) */
    ZOS_RECFM recfm;
    int       lrecl;
    int       blksize;

    /* Stats */
    long reads;
    long writes;
} ZOS_DCB;

/* =========================================================================
 * VSAM RPL (Request Parameter List)
 *
 * The control block programs use for VSAM macro calls (GET, PUT, ERASE, POINT).
 * In real z/OS this is the RPL macro. We simulate the key fields.
 * ========================================================================= */

typedef enum {
    RPL_REQ_GET   = 1,
    RPL_REQ_PUT   = 2,
    RPL_REQ_ERASE = 3,
    RPL_REQ_POINT = 4,
} ZOS_RPL_REQ;

typedef enum {
    RPL_OPT_KEY = 1,  /* Access by key (KSDS) */
    RPL_OPT_ADR = 2,  /* Access by RBA (ESDS) */
    RPL_OPT_CNH = 3,  /* Access by current position */
    RPL_OPT_RRN = 4,  /* Access by RRN (RRDS) */
} ZOS_RPL_OPT;

typedef enum {
    RPL_LOC_KEY  = 1, /* Direct: locate by key */
    RPL_LOC_FWD  = 2, /* Sequential forward */
    RPL_LOC_BWD  = 3, /* Sequential backward */
    RPL_LOC_FIRST= 4, /* First record in dataset */
    RPL_LOC_LAST = 5, /* Last record in dataset */
} ZOS_RPL_LOC;

typedef struct {
    ZOS_DCB    *acb;             /* Associated DCB (simulated ACB) */
    ZOS_RPL_REQ request;
    ZOS_RPL_OPT option;
    ZOS_RPL_LOC locate;

    unsigned char *area;         /* I/O buffer */
    int            arealen;      /* Buffer length */
    int            reclen;       /* Actual record length after GET */

    unsigned char *arg;          /* Key or RBA for direct access */
    int            arglen;

    /* Result */
    int  rtncd;                  /* 0=OK, 8=logical error, 12=physical */
    int  fdbk;                   /* Reason code */
} ZOS_RPL;

/* =========================================================================
 * ICF CATALOG
 *
 * The Integrated Catalog Facility (ICF) is the z/OS dataset registry.
 * Every allocated dataset has an entry. LISTCAT shows the catalog.
 * ========================================================================= */

typedef struct {
    ZOS_DATASET datasets[DS_MAX_DATASETS];
    int         count;
    char        catalog_name[DS_DSN_LEN + 1]; /* e.g., "ICFCAT.USERCAT" */
} ZOS_CATALOG;

/* Global catalog instance */
extern ZOS_CATALOG zos_catalog;

/* =========================================================================
 * FUNCTION PROTOTYPES — CATALOG
 * ========================================================================= */

void         ds_catalog_init(const char *catalog_name);
ZOS_DATASET *ds_catalog_find(const char *dsn);
ZOS_DATASET *ds_catalog_alloc(const char *dsn, ZOS_DSORG dsorg,
                               ZOS_RECFM recfm, int lrecl, int blksize);
int          ds_catalog_delete(const char *dsn);
void         ds_catalog_listcat(const char *filter);
const char  *ds_dsorg_name(ZOS_DSORG dsorg);
const char  *ds_recfm_name(ZOS_RECFM recfm);
ZOS_RECFM    ds_recfm_parse(const char *s);

/* =========================================================================
 * FUNCTION PROTOTYPES — PS (Physical Sequential)
 * ========================================================================= */

ZOS_DCB *ps_open(const char *dsn, const char *ddname,
                 ZOS_OPEN_MODE mode, ZOS_RECFM recfm, int lrecl, int blksize);
int      ps_read(ZOS_DCB *dcb, void *buffer, int *len);
int      ps_write(ZOS_DCB *dcb, const void *buffer, int len);
int      ps_point(ZOS_DCB *dcb, int record_num);
void     ps_close(ZOS_DCB *dcb);

/* =========================================================================
 * FUNCTION PROTOTYPES — PDS
 * ========================================================================= */

ZOS_DCB      *pds_open(const char *dsn, const char *ddname, ZOS_OPEN_MODE mode);
int           pds_find(ZOS_DCB *dcb, const char *member);
int           pds_stow(ZOS_DCB *dcb, const char *member);
int           pds_read(ZOS_DCB *dcb, void *buffer, int *len);
int           pds_write(ZOS_DCB *dcb, const void *buffer, int len);
void          pds_close(ZOS_DCB *dcb);
int           pds_list_members(ZOS_DCB *dcb);
ZOS_PDS_MEMBER *pds_find_member(ZOS_DATASET *ds, const char *name);

/* =========================================================================
 * FUNCTION PROTOTYPES — VSAM
 * ========================================================================= */

ZOS_DATASET *vsam_define_ksds(const char *dsn, int key_offset, int key_len,
                               int avg_len, int max_len);
ZOS_DATASET *vsam_define_esds(const char *dsn, int avg_len, int max_len);
ZOS_DATASET *vsam_define_rrds(const char *dsn, int rec_len, int slots);

ZOS_DCB *vsam_open(const char *dsn, const char *ddname, ZOS_OPEN_MODE mode);
void     vsam_close(ZOS_DCB *dcb);
int      vsam_get(ZOS_DCB *dcb, ZOS_RPL *rpl);
int      vsam_put(ZOS_DCB *dcb, ZOS_RPL *rpl);
int      vsam_erase(ZOS_DCB *dcb, ZOS_RPL *rpl);
int      vsam_point(ZOS_DCB *dcb, ZOS_RPL *rpl);

/* VSAM helper: compare keys */
int vsam_key_cmp(const unsigned char *a, const unsigned char *b, int len);
const char *vsam_rtncd_desc(int rtncd, int fdbk);

/* =========================================================================
 * FUNCTION PROTOTYPES — GDG
 * ========================================================================= */

ZOS_DATASET *gdg_define_base(const char *base_dsn, int limit,
                              bool empty, bool scratch);
int          gdg_new_gen(const char *base_dsn);
ZOS_DATASET *gdg_resolve(const char *base_dsn, int relative);
int          gdg_rolloff(ZOS_DATASET *base);
void         gdg_listcat_base(const char *base_dsn);

/* =========================================================================
 * FUNCTION PROTOTYPES — IDCAMS
 * ========================================================================= */

int idcams_define_cluster(const char *command);
int idcams_delete(const char *dsn, bool purge);
int idcams_listcat(const char *filter);
int idcams_repro(const char *indsn, const char *outdsn);
int idcams_print(const char *dsn);
int idcams_run(const char *command_stream);

/* =========================================================================
 * UTILITY HELPERS
 * ========================================================================= */

/* Record array helpers */
void ds_record_array_init(DS_RECORD_ARRAY *ra);
void ds_record_array_free(DS_RECORD_ARRAY *ra);
int  ds_record_array_add(DS_RECORD_ARRAY *ra, const void *data, int len);

/* RDW encode/decode */
void ds_rdw_encode(ZOS_RDW *rdw, int data_len);
int  ds_rdw_decode(const ZOS_RDW *rdw);

/* DCB pool */
void     ds_dcb_pool_init(void);
ZOS_DCB *ds_dcb_alloc(void);
void     ds_dcb_free(ZOS_DCB *dcb);

/* VSAM return code */
#define VSAM_OK           0   /* Successful */
#define VSAM_EOF          4   /* End of dataset on sequential GET */
#define VSAM_NOTFOUND     8   /* Record not found (direct GET) */
#define VSAM_DUPKEY       8   /* Duplicate key (PUT insert) — fdbk=8 */
#define VSAM_NOTOPEN     12   /* Dataset not open */
#define VSAM_IOERR       12   /* Physical I/O error */

/* VSAM fdbk codes (paired with RTNCD=8) */
#define VSAM_FDBK_DUPKEY   8   /* PUT: duplicate key */
#define VSAM_FDBK_NOTFOUND 8   /* GET direct: not found */
#define VSAM_FDBK_EOF     16   /* GET sequential: end of data */
#define VSAM_FDBK_NOSLOT  28   /* RRDS: slot out of range */

/* PS/PDS return codes */
#define DS_OK              0
#define DS_EOF            10   /* QSAM end of file */
#define DS_NOT_FOUND      16
#define DS_ALREADY_EXISTS 20
#define DS_NOT_OPEN       24
#define DS_BAD_RECFM      28
#define DS_MEMBER_NF      36   /* PDS member not found */

#endif /* ZOS_DATASETS_H */
