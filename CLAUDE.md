# IBM Mainframe Simulator — Claude Code Project Guide

## Project Vision

This project is an **IBM z/OS Mainframe Simulator** for education. Students learn real mainframe concepts — IMS DB, DB2, COBOL, z/OS datasets, JCL — in a native Linux/Windows/Mac environment without needing actual mainframe access.

Target learners: students new to mainframe, COBOL developers, z/OS operators in training.

---

## Architecture Overview

```
Mainframe-Simulator/
├── src/
│   ├── core/           # IMS engine: DB engine, DL/I calls, SSA parser, PSB/PCB
│   ├── tm/             # IMS Transaction Manager: MPP, BMP regions, message queues
│   ├── ui/             # Terminal UI (TSO/ISPF-like)
│   ├── db2/            # DB2 relational simulator (planned)
│   ├── cobol/          # COBOL runtime simulator (planned)
│   ├── datasets/       # z/OS dataset simulation: PS, PDS, VSAM (planned)
│   ├── jcl/            # JCL interpreter (planned)
│   └── main.c
├── specs/              # Feature specifications (read before implementing)
├── examples/           # Sample databases and programs
├── tests/              # Test suite
├── docs/               # User-facing documentation
└── .claude/            # Claude Code configuration
```

### Key Source Files
- [src/core/ims.h](src/core/ims.h) — master header: all structs, enums, constants
- [src/core/database.c](src/core/database.c) — hierarchical DB engine
- [src/core/dli_calls.c](src/core/dli_calls.c) — DL/I call implementations (GU, GN, ISRT, etc.)
- [src/core/ssa_parser.c](src/core/ssa_parser.c) — SSA parsing and qualification matching
- [src/core/ims_system.c](src/core/ims_system.c) — system init, PSB/DBD registry
- [src/tm/mpp.c](src/tm/mpp.c) — MPP region (online transaction processing)
- [src/tm/bmp.c](src/tm/bmp.c) — BMP region (batch processing)
- [src/ui/terminal.c](src/ui/terminal.c) — TSO/ISPF-like terminal interface

---

## Build System

```bash
make              # build mainframe-simulator.exe
make run          # interactive mode
make demo         # load HOSPITAL DB and run demo
make batch        # batch mode demo
make clean        # remove build artifacts
make test         # run test suite (when implemented)
```

Compiler: GCC (MinGW on Windows). Standard: C99. No external dependencies.

---

## IBM Mainframe Domain Reference

### IMS DB Fundamentals
- **DBD** (Database Definition) — schema: segment types, fields, hierarchy
- **PSB** (Program Specification Block) — program's view of databases; contains PCBs
- **PCB** (Program Communication Block) — access rules for one DBD from one program
- **DL/I** — Data Language/I, the call interface: `GU`, `GN`, `GNP`, `GHU`, `ISRT`, `DLET`, `REPL`
- **SSA** (Segment Search Argument) — navigation and qualification: `SEGMENT (FIELD=VALUE)`
- **Status codes** — IMS response: blank=OK, `GE`=not found, `GB`=end of DB, etc.

### z/OS Dataset Types
- **PS** (Physical Sequential) — flat file, like a Unix file
- **PDS/PDSE** (Partitioned Data Set) — like a directory of members, used for source code, JCL, load modules
- **VSAM** — Virtual Storage Access Method: KSDS (keyed), ESDS (entry-seq), RRDS (relative-record)
- **GDG** (Generation Data Group) — versioned dataset family

### JCL Basics
- `//JOBNAME  JOB` — job card
- `//STEPNAME EXEC PGM=PROGRAM` — execute a program
- `//DDNAME   DD   DSN=MY.DATASET,DISP=SHR` — define a dataset
- `//SYSIN    DD   *` — inline data

### COBOL on z/OS
- Division structure: IDENTIFICATION, ENVIRONMENT, DATA, PROCEDURE
- `CALL 'CBLTDLI' USING func pcb io-area ssa` — DL/I call from COBOL
- Working storage: PIC clauses, level numbers
- Copybooks: shared data definitions (like header files)

### DB2 Concepts
- Embedded SQL in COBOL/PL/I: `EXEC SQL SELECT ... END-EXEC`
- SQLCA (SQL Communication Area) — return codes
- Plans and packages — compiled SQL access paths

---

## Development Standards

### C Code Style
- C99, no C++ features
- Function names: `module_verb_noun` (e.g., `dli_parse_ssa`, `db_find_segment`)
- Types: `UPPER_CASE` for enums/structs following IMS naming (`IMS_PCB`, `DLI_FUNCTION`)
- Constants: `#define` with `IMS_` prefix
- Error handling: return `IMS_STATUS` codes, never `exit()` from library functions
- Memory: caller allocates, functions fill. No hidden malloc in library functions unless documented
- Max line length: 100 characters
- Comments: explain IBM mainframe behavior being simulated, cite IBM docs when relevant

### File Organization
- One subsystem per directory (core, tm, ui, db2, cobol, datasets, jcl)
- Each subsystem: one `.h` public interface + one or more `.c` implementation files
- New subsystem header must be included in `src/core/ims.h` or its own top-level header

### Testing
- Tests live in `tests/` mirroring `src/` structure
- Test filenames: `test_<module>.c`
- Each test function: `test_<behavior>()` returns 0=pass, 1=fail
- Test data: sample DBDs, PSBs, messages in `tests/data/`

### IBM Accuracy Over Simplicity
When simulating IBM behavior, **accuracy to the IBM specification takes priority** over simpler implementation. Document deviations explicitly with `/* DEVIATION: ... */` comments and the reason.

---

## Planned Feature Roadmap (see specs/)

1. **z/OS Address Spaces** (`specs/06-address-spaces.md`) — MVS address space model, SVC routing, IMS regions, JES2, DB2 AS, operator console
2. **z/OS Datasets / DFSMS** (`specs/02-zos-datasets.md`) — PS, PDS, VSAM (KSDS/ESDS/RRDS), GDG, IDCAMS
3. **DB2 Simulator** (`specs/03-db2-simulator.md`) — embedded SQL, SQLCA, cursors, DB2 catalog
4. **COBOL Runtime** (`specs/04-cobol-runtime.md`) — COBOL interpreter, PIC clauses, CBLTDLI, EXEC SQL
5. **JCL Interpreter** (`specs/05-jcl-interpreter.md`) — job submission, step execution, JES2 flow

---

## Sub-Agents

Use specialized agents for these tasks (see `.claude/agents/`):

| Agent | Use for |
|-------|---------|
| **mainframe-expert** | General z/OS accuracy, IMS status codes, IBM doc references |
| **zos-systems-expert** | Address spaces, SVC, MVS internals, JES2, abend codes, operator commands |
| **db2-expert** | DB2 embedded SQL, SQLCA/SQLCODE, cursors, bind, DB2 catalog |
| **cobol-ims-expert** | COBOL syntax, PIC clauses, CBLTDLI call interface, COBOL-DB2 interface |
| **dfsms-expert** | z/OS datasets (PS/PDS/VSAM/GDG), IDCAMS, DFSMS, record formats, ICF catalog |
| **c-systems-developer** | C implementation, data structures, memory layout, portability |
| **qa-tester** | Writing and reviewing tests, edge cases, error path coverage |

---

## Important Links

- IBM IMS 15.6 Docs: https://www.ibm.com/docs/en/ims/15.6.0
- IBM z/OS Concepts: https://www.ibm.com/docs/en/zos
- IBM DB2 for z/OS: https://www.ibm.com/docs/en/db2-for-zos
