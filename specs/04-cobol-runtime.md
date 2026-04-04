# Spec 04: COBOL Runtime Simulator

## IBM Reference
- IBM Enterprise COBOL for z/OS Language Reference (SC27-8887)
- IBM Enterprise COBOL for z/OS Programming Guide (SC27-8885)

## Goal

Simulate COBOL program execution so students can write simplified COBOL source, "compile" it, and run it against the IMS DB/DB2 simulator. The runtime handles COBOL's data model (PIC clauses, level numbers, working storage) and the mainframe call interface.

## Approach: Interpreted COBOL (Not Compiled)

Rather than a full COBOL compiler, implement an **interpreter** for a COBOL subset. This lets students write COBOL and see it execute without needing a real compiler.

## COBOL Subset (Phase 1)

### Divisions
```
IDENTIFICATION DIVISION.
  PROGRAM-ID. MYPROG.

ENVIRONMENT DIVISION.
  (minimal — input-output section for file assignment)

DATA DIVISION.
  WORKING-STORAGE SECTION.
    01 WS-HOSP-REC.
       05 WS-HOSPCODE  PIC X(4).
       05 WS-HOSPNAME  PIC X(30).
    01 WS-STATUS       PIC S9(4) COMP.

  LINKAGE SECTION.
    (for called programs)

PROCEDURE DIVISION.
  ...
  STOP RUN.
```

### PIC Clauses to Support
- `PIC X(n)` — alphanumeric, n bytes
- `PIC 9(n)` — unsigned numeric display
- `PIC S9(n)` — signed numeric display
- `PIC S9(n) COMP` — binary (halfword/fullword depending on n)
- `PIC S9(n) COMP-3` — packed decimal (BCD)
- `PIC 9(p)V9(s)` — decimal with implied decimal point

### Level Numbers
- 01 — record level
- 02-49 — group/elementary items
- 66 — RENAMES (Phase 2)
- 77 — independent elementary item
- 88 — condition name (boolean alias for value)

### PROCEDURE DIVISION Statements (Phase 1)
- `MOVE value TO var`
- `MOVE SPACES/ZEROS/HIGH-VALUES/LOW-VALUES TO var`
- `COMPUTE var = expression` (arithmetic)
- `ADD/SUBTRACT/MULTIPLY/DIVIDE`
- `IF condition ... ELSE ... END-IF`
- `PERFORM para-name` / `PERFORM para-name UNTIL cond`
- `PERFORM VARYING i FROM 1 BY 1 UNTIL i > n`
- `CALL 'CBLTDLI' USING func pcb io-area ssa` — IMS DL/I
- `EXEC SQL ... END-EXEC` — DB2 (delegates to DB2 simulator)
- `DISPLAY 'text' [var]` — output to terminal
- `ACCEPT var` — input from terminal
- `STOP RUN` / `GOBACK`
- `EVALUATE var WHEN val ... END-EVALUATE`
- `STRING/UNSTRING` — Phase 2

## Data Representation

```c
/* src/cobol/cobol_data.h */

typedef enum {
    PIC_X,          /* Alphanumeric */
    PIC_9,          /* Numeric display */
    PIC_S9,         /* Signed numeric display */
    PIC_COMP,       /* Binary */
    PIC_COMP3,      /* Packed decimal */
} PIC_TYPE;

typedef struct CobolVar {
    char name[32];
    int level;
    PIC_TYPE pic_type;
    int total_len;      /* Storage bytes */
    int pic_digits;     /* For 9/S9: total digits */
    int pic_decimals;   /* After V */
    unsigned char *storage;  /* Pointer into working storage buffer */
    struct CobolVar *parent;
    struct CobolVar *children;
    struct CobolVar *next_sibling;
} CobolVar;

typedef struct {
    unsigned char *working_storage;
    int ws_size;
    CobolVar *variables;
    int var_count;
    /* ... */
} CobolProgram;
```

## IMS Interface from COBOL

```cobol
* Real COBOL IMS call:
CALL 'CBLTDLI' USING
    WS-DLI-FUNC         *> 'GU  '
    PCB-ADDRESS         *> PCB pointer
    WS-IO-AREA          *> data buffer
    WS-SSA-1.           *> SSA string
```

The simulator must:
1. Recognize `CALL 'CBLTDLI'`
2. Map COBOL variables to C structures (`IMS_PCB`, `IMS_SSA`)
3. Call the appropriate DL/I function
4. Update COBOL variables from the C result

## Example Student Program

```cobol
IDENTIFICATION DIVISION.
PROGRAM-ID. HOSPINQ.

DATA DIVISION.
WORKING-STORAGE SECTION.
01 WS-PCB-MASK.
   05 WS-DB-NAME    PIC X(8).
   05 WS-SEG-LEVEL  PIC 99.
   05 WS-STATUS     PIC XX.
   05 WS-PROC-OPT   PIC X(4).

01 WS-HOSP-SEG.
   05 WS-HOSPCODE   PIC X(4).
   05 WS-HOSPNAME   PIC X(30).

01 WS-SSA.
   05 WS-SSA-SEG    PIC X(8) VALUE 'HOSPITAL'.
   05 FILLER        PIC X(1) VALUE ' '.

PROCEDURE DIVISION.
    CALL 'CBLTDLI' USING 'GU  ' WS-PCB-MASK WS-HOSP-SEG WS-SSA.
    IF WS-STATUS = '  '
        DISPLAY 'Hospital: ' WS-HOSPNAME
    ELSE
        DISPLAY 'Not found: ' WS-STATUS
    END-IF.
    STOP RUN.
```

## Tests

- `test_cobol_move_spaces`: MOVE SPACES fills X field with spaces
- `test_cobol_move_numeric`: MOVE 42 TO S9(4) COMP stores correct binary
- `test_cobol_perform_until`: PERFORM loop executes correct number of times
- `test_cobol_call_cbltdli`: CALL 'CBLTDLI' with GU executes DL/I and updates status
- `test_cobol_if_condition`: IF with 88-level condition name evaluates correctly
- `test_cobol_exec_sql`: EXEC SQL SELECT delegated to DB2 simulator
