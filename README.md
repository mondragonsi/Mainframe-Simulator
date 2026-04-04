# IBM IMS Simulator

A learning-focused simulator for IBM Information Management System (IMS) that replicates the mainframe IMS experience for training purposes.

## Features

### IMS Database Manager (IMS DB)
- Hierarchical database structure
- DL/I calls: GU, GN, GNP, GHU, GHN, GHNP, ISRT, DLET, REPL
- SSA (Segment Search Arguments) with qualifications
- PCB/PSB management
- Sample HOSPITAL database

### IMS Transaction Manager (IMS TM)
- Message queuing system
- MPP region simulation (online transactions)
- BMP region simulation (batch processing)
- Conversational transactions with SPA
- Transaction scheduling

### Terminal Interface
- Mainframe-like interface similar to TSO/ISPF
- Interactive DL/I call execution
- Database and transaction panels
- Help system

## Building

### Prerequisites
- GCC compiler (MinGW on Windows)
- Make

### Compile
```bash
cd c:\dev\IMS
make
```

### Run
```bash
# Interactive mode
make run

# With sample database loaded
make demo

# Batch demo mode
make batch
```

## Usage

### Interactive Commands
| Command   | Description                        |
|-----------|------------------------------------|
| /DB       | Database Manager panel             |
| /TM       | Transaction Manager panel          |
| /DLI      | DL/I call interface                |
| /LOAD     | Load HOSPITAL sample database      |
| /DISPLAY  | Show system status                 |
| /HELP     | Show help                          |
| /END      | Exit simulator                     |

### DL/I Commands (in /DLI mode)
```
GU segname [(field=value)]    - Get Unique
GN [segname]                  - Get Next
GNP [segname]                 - Get Next in Parent
ISRT segname                  - Insert
DLET                          - Delete current segment
REPL                          - Replace current segment
PCB psbname                   - Schedule PSB
```

### Examples
```
# Get root segment
GU HOSPITAL

# Get specific hospital
GU HOSPITAL (HOSPCODE=H001)

# Navigate to children
GN WARD
GNP PATIENT
```

## Sample Database: HOSPITAL

```
HOSPITAL (root)
├── WARD
│   └── PATIENT
│       ├── TREATMNT
│       └── DOCTOR
└── FACILITY
```

### Segment Fields

**HOSPITAL** (root):
- HOSPCODE (Key, 4 bytes)
- HOSPNAME (30 bytes)
- HOSPADDR (50 bytes)
- HOSPPHON (15 bytes)

**WARD**:
- WARDNO (Key, 4 bytes)
- WARDNAME (20 bytes)
- WARDTYPE (10 bytes)
- NUMBEDS (4 bytes)

**PATIENT**:
- PATNO (Key, 6 bytes)
- PATNAME (30 bytes)
- PATADDR (50 bytes)
- DATADMIT (10 bytes)
- PATDOC (20 bytes)

## Project Structure

```
IMS/
├── src/
│   ├── core/
│   │   ├── ims.h           # Main header
│   │   ├── ims_system.c    # System core
│   │   ├── database.c      # DB engine
│   │   ├── dli_calls.c     # DL/I implementation
│   │   └── ssa_parser.c    # SSA parsing
│   ├── tm/
│   │   ├── msgqueue.c/h    # Message queues
│   │   ├── mpp.c/h         # MPP region
│   │   └── bmp.c/h         # BMP region
│   ├── ui/
│   │   └── terminal.c/h    # Terminal interface
│   └── main.c
├── examples/
├── tests/
├── Makefile
└── README.md
```

## Reference

Based on IBM IMS 15.6 Documentation:
https://www.ibm.com/docs/en/ims/15.6.0

## License

Educational use only. This is a simulator for learning purposes.
