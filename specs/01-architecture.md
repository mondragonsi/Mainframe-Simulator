# Spec 01: Overall Architecture

## Goal

A modular IBM z/OS mainframe simulator where each subsystem is independent but integrates through a central IMS system context (`IMS_SYSTEM`). Students interact via a TSO/ISPF-like terminal UI.

## Subsystem Map

```
┌─────────────────────────────────────────────────────────────┐
│                    Terminal UI (ISPF/TSO)                    │
│                     src/ui/terminal.c                        │
└──────┬──────────┬──────────┬──────────┬──────────┬──────────┘
       │          │          │          │          │
   ┌───▼───┐  ┌──▼───┐  ┌───▼──┐  ┌───▼──┐  ┌───▼───┐
   │IMS DB │  │IMS TM│  │ DB2  │  │COBOL │  │  JCL  │
   │  Core │  │      │  │      │  │Runtime│  │Interp.│
   └───┬───┘  └──────┘  └──────┘  └──────┘  └───┬───┘
       │                                          │
   ┌───▼──────────────────────────────────────────▼───┐
   │              z/OS Dataset Layer                   │
   │        src/datasets/ (PS, PDS, VSAM, GDG)        │
   └───────────────────────────────────────────────────┘
```

## Integration Points

### IMS_SYSTEM (Central Context)
- Owns all active DBDs, PSBs, regions, transaction definitions
- All subsystems receive a pointer to `IMS_SYSTEM`; no globals except the one `ims_system` instance
- Thread model: single-threaded simulation (no mutexes needed now, but design for easy future extension)

### Dataset Layer (Foundation)
- All subsystems that do I/O use the dataset layer
- IMS DB stores its hierarchical data in simulated z/OS datasets
- JCL drives program execution; programs access datasets by DD name
- VSAM is used by IMS for HISAM/HDAM access methods

### Event Flow
```
JCL Job Submit
  → JCL Interpreter parses JOB/EXEC/DD
  → Allocates datasets (dataset layer)
  → Executes COBOL program (COBOL runtime)
    → COBOL calls CBLTDLI (IMS DB)
    → COBOL executes EXEC SQL (DB2)
  → Writes output to dataset / sysout
  → SDSF-like output viewer
```

## Milestones

| # | Feature | Status | Spec |
|---|---------|--------|------|
| 1 | IMS DB Core (DL/I, SSA, PCB) | Done | — |
| 2 | IMS TM (MPP/BMP, queues) | Done | — |
| 3 | Terminal UI | Done | — |
| 4 | z/OS Datasets (PS, PDS, VSAM) | Planned | spec 02 |
| 5 | DB2 Simulator | Planned | spec 03 |
| 6 | COBOL Runtime | Planned | spec 04 |
| 7 | JCL Interpreter | Planned | spec 05 |
| 8 | ISPF Panel Editor | Planned | spec 06 |

## Design Constraints

- **No external libraries** — pure C99, standard library only
- **Cross-platform** — must build on Windows (MinGW), Linux, Mac with GCC
- **Educational fidelity** — prefer IBM-accurate behavior over simplicity; document all deviations
- **Self-contained** — the simulator runs entirely in-memory; optional persistence to host files
