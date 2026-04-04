# Spec 02: z/OS Dataset Simulation

## IBM Reference
- IBM z/OS DFSMS Using Data Sets (SC23-6855)
- IBM z/OS MVS JCL Reference (SA23-1385)

## Goal

Simulate z/OS dataset types so that simulated COBOL programs, JCL, and IMS can read/write data using IBM-standard DD names, DSNs, and record formats.

## Dataset Types to Implement

### Phase 1: Physical Sequential (PS)
File: `src/datasets/ps.c`

- Fixed Block (FB): all records same length, blocked
- Variable Block (VB): records have RDW (record descriptor word, 4 bytes)
- RECFM=F/FB/V/VB
- LRECL: logical record length
- BLKSIZE: physical block size

Operations:
- OPEN (INPUT, OUTPUT, EXTEND, INOUT)
- READ (GET): read next record into buffer
- WRITE (PUT): write record from buffer
- CLOSE
- Point (BSAM: position to specific block)

### Phase 2: Partitioned Data Set (PDS/PDSE)
File: `src/datasets/pds.c`

- Directory: up to 255 members
- Each member: name (8 chars), pointer to data
- Operations: OPEN member, READ, WRITE, STOW (add directory entry), FIND

Use cases:
- Source libraries (COBOL, JCL, copybooks)
- Load module libraries (executable programs)
- Procedure libraries (cataloged JCL procs)

### Phase 3: VSAM
File: `src/datasets/vsam.c`

#### KSDS (Key-Sequenced Data Set)
- Records stored by key order
- Index + data component
- Operations: GET (by key), PUT, DELETE, POINT (position)
- Alternate indexes (AIX): access by alternate key
- Status codes: VSAM return/reason codes in RPL

#### ESDS (Entry-Sequenced Data Set)
- Records in insertion order (no key)
- RBA (Relative Byte Address) is the "address"
- Append-only writes

#### RRDS (Relative-Record Data Set)
- Fixed-length records by relative record number (RRN)
- Slot-based: slots can be empty

### Phase 4: GDG (Generation Data Group)
File: `src/datasets/gdg.c`
- Version management: MY.BACKUP.G0001V00, G0002V00, etc.
- Relative generation: MY.BACKUP(0) = current, (-1) = previous, (+1) = new
- Limit and rolloff

## Data Structures

```c
/* src/datasets/datasets.h */

typedef enum {
    DSORG_PS,    /* Physical Sequential */
    DSORG_PO,    /* Partitioned (PDS) */
    DSORG_VS,    /* VSAM */
    DSORG_GDG,   /* Generation Data Group */
} ZOS_DSORG;

typedef enum {
    RECFM_F,    /* Fixed */
    RECFM_FB,   /* Fixed Blocked */
    RECFM_V,    /* Variable */
    RECFM_VB,   /* Variable Blocked */
    RECFM_U,    /* Undefined */
} ZOS_RECFM;

typedef struct {
    char dsn[45];           /* Dataset name: HLQ.MID.LLQ */
    ZOS_DSORG dsorg;
    ZOS_RECFM recfm;
    int lrecl;
    int blksize;
    bool exists;
    /* Internal: pointer to host file or in-memory buffer */
    void *storage;
} ZOS_DATASET;

typedef struct {
    char ddname[9];         /* DD name from JCL */
    ZOS_DATASET *dataset;
    int open_mode;          /* INPUT, OUTPUT, EXTEND, INOUT */
    int position;           /* Current record position */
    bool is_open;
} ZOS_DCB;                 /* Data Control Block */
```

## Status Codes

Follow IBM VSAM return/reason codes:
- 00/00 — successful
- 08/-- — logical error
- 12/-- — physical error

For PS/PDS use QSAM-style: file status codes (00=OK, 10=EOF, 22=dup key for indexed)

## Integration with IMS

IMS DB access methods (HISAM, HDAM, HIDAM) are implemented on top of VSAM:
- HISAM: KSDS primary + ESDS overflow
- HDAM: ESDS with hashing
- HIDAM: KSDS
The dataset layer must be stable before implementing real IMS access methods.

## Tests to Write

- `test_ps_fb_write_read`: write 10 FB records, read them back, verify content and count
- `test_ps_vb_rdw`: write VB records, verify RDW bytes, read back correctly
- `test_pds_member_stow_find`: create PDS, STOW member, FIND it, read it
- `test_vsam_ksds_insert_get`: KSDS insert by key, GET by key, verify data
- `test_vsam_ksds_key_sequence`: insert out-of-order keys, verify stored in sequence
- `test_vsam_ksds_duplicate_key`: insert duplicate, verify correct error code
- `test_gdg_relative_gen`: create GDG base, add generations, access by (0) and (-1)
