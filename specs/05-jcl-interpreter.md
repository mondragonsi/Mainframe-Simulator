# Spec 05: JCL Interpreter

## IBM Reference
- IBM z/OS MVS JCL Reference (SA23-1385)
- IBM z/OS MVS JCL User's Guide (SA23-1386)

## Goal

Allow students to submit JCL jobs that execute simulated COBOL programs against IMS DB, DB2, and z/OS datasets — just like real mainframe batch processing.

## JCL Syntax Subset

### Job Card
```jcl
//MYJOB    JOB (ACCT),'MY JOB',CLASS=A,MSGCLASS=X,
//             NOTIFY=&SYSUID
```

### EXEC Statement
```jcl
//STEP01   EXEC PGM=MYPROG
//STEP02   EXEC PROC=MYPROC
//STEP03   EXEC PGM=IKJEFT01    (TSO in batch — run REXX/CLIST)
```

### DD Statement
```jcl
//INPUT    DD   DSN=MY.INPUT.FILE,DISP=SHR
//OUTPUT   DD   DSN=MY.OUTPUT.FILE,DISP=(NEW,CATLG,DELETE),
//              DCB=(RECFM=FB,LRECL=80,BLKSIZE=800),
//              SPACE=(TRK,(10,5))
//SYSIN    DD   *
  inline data here
/*
//SYSOUT   DD   SYSOUT=*
//DUMMY    DD   DUMMY
```

### DISP Parameter
- `DISP=(status,normal-end,abnormal-end)`
- Status: `NEW`, `OLD`, `SHR`, `MOD`
- Disposition: `KEEP`, `DELETE`, `CATLG`, `UNCATLG`, `PASS`

### Cataloged Procedures (PROC)
```jcl
//MYPROC   PROC PARM1=DEFAULT
//STEP1    EXEC PGM=MYPROG,PARM=&PARM1
//INPUT    DD   DSN=MY.INPUT,DISP=SHR
//         PEND
```

## JCL Parser Data Structures

```c
/* src/jcl/jcl.h */

#define JCL_NAME_LEN    8
#define JCL_DSN_LEN     44
#define JCL_STMT_MAX    256

typedef enum {
    JCL_JOB, JCL_EXEC, JCL_DD, JCL_PROC, JCL_PEND,
    JCL_COMMENT, JCL_INLINE_DATA, JCL_DELIMITER,
} JCL_STMT_TYPE;

typedef struct {
    char name[JCL_NAME_LEN + 1];     /* Statement name */
    JCL_STMT_TYPE type;
    char operands[JCL_STMT_MAX];     /* Raw operand string */
    int line_number;
} JCL_STATEMENT;

typedef struct {
    char dsn[JCL_DSN_LEN + 1];
    char ddname[JCL_NAME_LEN + 1];
    char disp_status[4];              /* NEW/OLD/SHR/MOD */
    char disp_normal[8];              /* KEEP/DELETE/CATLG/PASS */
    char disp_abend[8];
    int  lrecl;
    char recfm[4];
    int  blksize;
    bool is_dummy;
    bool is_sysout;
    bool is_inline;
    char *inline_data;
} JCL_DD;

typedef struct {
    char stepname[JCL_NAME_LEN + 1];
    char pgm_name[JCL_NAME_LEN + 1];
    char proc_name[JCL_NAME_LEN + 1];
    char parm[256];
    int  cond_code;                   /* Return code after step runs */
    bool cond_even;
    JCL_DD dds[64];
    int dd_count;
} JCL_STEP;

typedef struct {
    char jobname[JCL_NAME_LEN + 1];
    char account[32];
    char description[32];
    char class[2];
    char msgclass[2];
    JCL_STEP steps[32];
    int step_count;
} JCL_JOB;
```

## Execution Engine

```
JCL text
  → JCL Lexer (line-by-line, handle continuation)
  → JCL Parser (build JCL_JOB structure)
  → JCL Executor:
      for each step:
        1. Allocate datasets (DD statements → ZOS_DCB)
        2. If PGM=: look up program in load library simulation
        3. Execute program (COBOL runtime or built-in utility)
        4. Check return code vs COND parameter
        5. Disposition datasets per DISP(normal/abend)
  → Job log → SDSF-like output
```

## Built-in Utilities to Simulate

| Program | Purpose |
|---------|---------|
| `IEFBR14` | Do-nothing program (allocate/delete datasets via DD DISP) |
| `IDCAMS` | VSAM utility: DEFINE CLUSTER, DELETE, REPRO, LISTCAT |
| `SORT` | IBM DFSORT: sort a dataset by fields |
| `IEBGENER` | Copy sequential datasets |
| `IEBCOPY` | Copy PDS members |
| `DSNUPROC` | DB2 utilities: LOAD, UNLOAD, REORG, RUNSTATS |

## SYSOUT / Job Log

After job execution, produce a simulated SYSOUT listing:
```
JOB MYJOB     JOB00123   2024/01/15  STARTED   09:00:00
JOB MYJOB     JOB00123   STEP01  PGM=MYPROG   RC=0000
JOB MYJOB     JOB00123   STEP02  PGM=SORT     RC=0000
JOB MYJOB     JOB00123   2024/01/15  ENDED     09:00:02  RC=0000
```

## Tests

- `test_jcl_parse_job_card`: parse JOB statement, verify jobname/class
- `test_jcl_parse_exec`: parse EXEC PGM=, verify program name
- `test_jcl_parse_dd_dsn`: parse DD with DSN/DISP, verify all fields
- `test_jcl_parse_dd_inline`: parse DD * with inline data, capture data
- `test_jcl_parse_continuation`: multi-line statement with // continuation
- `test_jcl_exec_iefbr14`: run IEFBR14, verify RC=0
- `test_jcl_disp_new_catlg`: DD DISP=(NEW,CATLG) creates and catalogs dataset
- `test_jcl_cond_skip_step`: COND parameter skips step when RC condition met
