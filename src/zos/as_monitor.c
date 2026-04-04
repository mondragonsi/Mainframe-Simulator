/*
 * z/OS Address Space Monitor Panel Implementation
 *
 * Provides an ISPF-style interactive panel for monitoring address spaces,
 * issuing operator commands, and viewing the system syslog.
 *
 * 3270 terminal conventions used:
 *   - Header: reverse-video title bar with date/time
 *   - Function keys: F1=HELP F3=END F5=REFRESH F7=UP F8=DOWN
 *   - Command line: COMMAND ===>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "as_monitor.h"
#include "address_space.h"
#include "console.h"
#include "svc.h"
#include "../core/ims.h"
#include "../ui/terminal.h"

/* =========================================================================
 * HELPERS
 * ========================================================================= */

static void print_header(const char *title) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y/%m/%d %H:%M:%S", t);

    printf("%s", TERM_REVERSE);
    printf(" z/OS Simulator %-8s  %-36s  %s ",
           zos_system.system_name, title, ts);
    printf("%s\n", TERM_RESET);
}

static void print_footer(const char *extra) {
    printf("%s", TERM_REVERSE);
    if (extra) {
        printf(" F1=HELP  F3=END  F5=REFRESH  %s", extra);
    } else {
        printf(" F1=HELP  F3=END  F5=REFRESH  F7=UP  F8=DOWN");
    }
    /* Pad to 80 */
    printf("%*s", 1, " ");
    printf("%s\n", TERM_RESET);
}

/* =========================================================================
 * ADDRESS SPACE MONITOR PANEL
 *
 * Mirrors the SDSF (System Display and Search Facility) DA panel,
 * which operators use to view active address spaces.
 * ========================================================================= */

void as_monitor_show(void) {
    terminal_clear();
    print_header("ADDRESS SPACE MONITOR (D A,L)");

    printf("\n");
    printf("%s COMMAND ===> %s                                                      \n",
           TERM_CYAN, TERM_RESET);
    printf("\n");
    printf("%s", TERM_BOLD);
    printf(" ASID  NAME      TYPE          STATUS     PRI  KEY  CPU(ms)  PROGRAM  \n");
    printf("%s", TERM_RESET);
    printf(" ----  --------  ------------  ---------  ---  ---  -------  -------- \n");

    time_t now = time(NULL);

    for (int i = 0; i < zos_system.space_count; i++) {
        ZOS_ADDRESS_SPACE *as = zos_system.spaces[i];
        if (!as) continue;

        long elapsed_ms = (long)(now - as->start_time) * 1000;
        const char *color = TERM_RESET;

        switch (as->status) {
            case AS_STATUS_ACTIVE:      color = TERM_GREEN;  break;
            case AS_STATUS_WAITING:     color = TERM_YELLOW; break;
            case AS_STATUS_TERMINATED:  color = TERM_RED;    break;
            case AS_STATUS_TERMINATING: color = TERM_RED;    break;
            default:                    color = TERM_RESET;  break;
        }

        printf(" %s%04X%s  %-8s  %-12s  %s%-9s%s  %3d  %3d  %7ld  %-8s\n",
               TERM_CYAN, as->asid, TERM_RESET,
               as->name,
               zos_as_type_name(as->type),
               color,
               zos_as_status_name(as->status),
               TERM_RESET,
               as->dispatch_priority,
               as->storage_key,
               elapsed_ms,
               as->pgm_name);
    }

    printf("\n");
    printf(" %d ADDRESS SPACE(S)   CSA: %zu KB / %d KB USED\n",
           zos_system.space_count,
           zos_system.csa_used / 1024,
           ZOS_CSA_SIZE / 1024);
    printf("\n");

    print_footer("Enter console command or DIAGRAM/SPOOL/SYSLOG/SVC");
}

/* =========================================================================
 * VIRTUAL STORAGE MAP
 *
 * Shows the classic z/OS virtual storage layout for a given address space.
 * This is one of the most important concepts for students to understand.
 * ========================================================================= */

void as_monitor_show_storage(int asid) {
    ZOS_ADDRESS_SPACE *as = zos_as_find_asid(asid);
    terminal_clear();

    if (!as) {
        printf("%s ERROR: ASID %04X NOT FOUND %s\n", TERM_RED, asid, TERM_RESET);
        return;
    }

    print_header("VIRTUAL STORAGE MAP");
    printf("\n");
    printf(" Address Space: %04X  %-8s  %s\n",
           as->asid, as->name, zos_as_type_name(as->type));
    printf("\n");

    size_t total = as->storage.nucleus_size + as->storage.sqa_size +
                   as->storage.csa_size + as->storage.lsqa_size +
                   as->storage.swa_size + as->storage.private_size;
    size_t addr = total;

    /*
     * Show virtual storage from high to low address,
     * matching how IBM documentation represents the layout.
     */
    printf(" High address (%zuMB)\n", total / (1024 * 1024));
    printf(" %s┌──────────────────────────────────────────┐%s\n",
           TERM_CYAN, TERM_RESET);

    addr -= as->storage.private_size;
    printf(" │ PRIVATE AREA                    %6zuKB │  ← User programs\n",
           as->storage.private_size / 1024);
    printf(" │   Used: %6zuKB  Free: %6zuKB          │     Working Storage\n",
           as->storage.private_used / 1024,
           (as->storage.private_size - as->storage.private_used) / 1024);
    printf(" %s├──────────────────────────────────────────┤%s\n",
           TERM_CYAN, TERM_RESET);

    addr -= as->storage.swa_size;
    printf(" │ SWA (Scheduler Work Area)       %6zuKB │  ← JCL, DD names\n",
           as->storage.swa_size / 1024);
    printf(" %s├──────────────────────────────────────────┤%s\n",
           TERM_CYAN, TERM_RESET);

    addr -= as->storage.lsqa_size;
    printf(" │ LSQA (Local System Queue Area)  %6zuKB │  ← Per-AS system data\n",
           as->storage.lsqa_size / 1024);
    printf(" %s├──────────────────────────────────────────┤%s\n",
           TERM_CYAN, TERM_RESET);

    printf(" │ CSA (Common Service Area)       %6dKB │  ← Shared: ALL AS\n",
           ZOS_CSA_SIZE / 1024);
    printf(" │   IMS pools, DB2 pools, control blocks   │\n");
    printf(" %s├──────────────────────────────────────────┤%s\n",
           TERM_CYAN, TERM_RESET);

    addr -= as->storage.sqa_size;
    printf(" │ SQA (System Queue Area)         %6zuKB │  ← System control\n",
           as->storage.sqa_size / 1024);
    printf(" %s├──────────────────────────────────────────┤%s  16MB line\n",
           TERM_YELLOW, TERM_RESET);

    printf(" │ MVS NUCLEUS (key 0, fetch-prot) %6zuKB │  ← Interrupt handlers\n",
           as->storage.nucleus_size / 1024);
    printf(" │ PSA (Prefixed Storage Area)     %6zuKB │  ← CPU-specific\n",
           as->storage.psa_size / 1024);
    printf(" %s└──────────────────────────────────────────┘%s\n",
           TERM_CYAN, TERM_RESET);
    printf(" Low address (0)\n");
    printf("\n");

    /* GETMAIN table */
    if (as->getmain_count > 0) {
        printf(" PRIVATE GETMAIN ALLOCATIONS:\n");
        printf(" SUB  SIZE     ADDRESS\n");
        printf(" ---  -------  -------\n");
        for (int i = 0; i < as->getmain_count; i++) {
            printf("  %3d  %6zuKB  %p\n",
                   as->getmain_table[i].subpool,
                   as->getmain_table[i].size / 1024,
                   as->getmain_table[i].ptr);
        }
    }

    print_footer("F3=END");
    (void)addr;
}

/* =========================================================================
 * ARCHITECTURE DIAGRAM
 *
 * The most educational panel: shows how all address spaces relate to each
 * other and how communication flows (SVC, cross-memory, CSA).
 * ========================================================================= */

void as_monitor_show_diagram(void) {
    terminal_clear();
    print_header("z/OS ADDRESS SPACE ARCHITECTURE");
    printf("\n");

    printf("%s", TERM_CYAN);
    printf(" ┌─────────────────────────────────────────────────────────────────┐\n");
    printf(" │                     z/OS MVS KERNEL (ASID 0001)                │\n");
    printf(" │   Dispatcher │ SVC Router │ I/O Subsystem │ Storage Manager     │\n");
    printf(" └───────┬──────────────────────────────────────────────┬──────────┘\n");
    printf("%s", TERM_RESET);
    printf("         │ Job Submit                                    │ SVC 30 (DL/I)\n");
    printf("         ▼                                               ▼\n");

    printf("%s", TERM_YELLOW);
    printf(" ┌───────────────┐    ┌──────────────────────────────────────────┐\n");
    printf(" │  JES2 (0003)  │    │       IMS CONTROL REGION (0004)          │\n");
    printf(" │ Job Entry     │    │ DL/I Nucleus │ Buffer Pools │ Log Manager│\n");
    printf(" │ Spool Manager │    │ Lock Manager │ PSB Scheduler│ DBRC Link  │\n");
    printf(" │ Initiators    │    └──────────────────────────────────────────┘\n");
    printf(" └───────────────┘       ▲ SVC 30 cross-AS call ▲\n");
    printf("%s", TERM_RESET);
    printf("                         │                       │\n");

    printf("%s", TERM_GREEN);
    printf(" ┌───────────────────────┴─────┐   ┌────────────┴───────────────┐\n");
    printf(" │  MPP REGION (ASID dynamic)  │   │  BMP REGION (ASID dynamic) │\n");
    printf(" │  COBOL Online Transaction   │   │  COBOL Batch + DB Access   │\n");
    printf(" │  - PSB scheduled here       │   │  - PSB scheduled here      │\n");
    printf(" │  - Issues SVC 30 → IMS CTL │   │  - Issues SVC 30 → IMS CTL│\n");
    printf(" └─────────────────────────────┘   └────────────────────────────┘\n");
    printf("%s", TERM_RESET);
    printf("\n");

    printf("%s", TERM_BLUE);
    printf(" ┌──────────────────────────────────────────────────────────────┐\n");
    printf(" │  DB2 ADDRESS SPACE - DSNMSTR (ASID 0006)                    │\n");
    printf(" │  SQL Engine │ Buffer Pool BP0 │ Lock Manager │ Log Manager  │\n");
    printf(" │  Attached via DB2 CAF (Cross-Memory PC instruction)         │\n");
    printf(" └──────────────────────────────────────────────────────────────┘\n");
    printf("%s", TERM_RESET);
    printf("\n");

    printf("%s", TERM_REVERSE);
    printf(" ┌──────────────────────────────────────────────────────────────┐\n");
    printf(" │         CSA - COMMON SERVICE AREA (visible to ALL AS)        │\n");
    printf(" │   IMS Buffer Pools │ DB2 Buffer Pools │ IMS Control Blocks  │\n");
    printf(" │   %4zuKB used of %4dKB total                                │\n",
           zos_system.csa_used / 1024, ZOS_CSA_SIZE / 1024);
    printf(" └──────────────────────────────────────────────────────────────┘\n");
    printf("%s", TERM_RESET);

    printf("\n");
    printf(" %sCOMMUNICATION FLOWS:%s\n", TERM_BOLD, TERM_RESET);
    printf("  COBOL → CALL 'CBLTDLI' → SVC 30 → IMS CTL → DL/I executed → return\n");
    printf("  COBOL → EXEC SQL        → DB2 CAF (PC) → DB2 AS → SQL executed → return\n");
    printf("  JCL   → JES2 initiator → batch AS created → COBOL runs → RC to JES2\n");
    printf("\n");

    print_footer("F3=END  F5=REFRESH");
}

/* =========================================================================
 * JES2 SPOOL VIEWER (SDSF-like)
 * ========================================================================= */

void as_monitor_show_spool(void) {
    terminal_clear();
    print_header("SDSF - SPOOL DISPLAY (JES2)");

    ZOS_JES2_CTX *jes = &zos_system.jes2;

    printf("\n");
    printf("%s", TERM_BOLD);
    printf(" JOB#     JOBNAME   ST CL SYSOUT  LINES\n");
    printf("%s", TERM_RESET);
    printf(" ------   --------  -- -- ------  -----\n");

    if (jes->spool_count == 0) {
        printf(" (no spool output)\n");
    } else {
        for (int i = 0; i < jes->spool_count; i++) {
            ZOS_SPOOL_ENTRY *e = &jes->spool[i];
            /* Find job status */
            const char *st = "??";
            char cls = '?';
            for (int j = 0; j < jes->job_count; j++) {
                if (jes->jobs[j].job_number == e->job_number) {
                    st = jes->jobs[j].is_complete ? "CO" : "AC";
                    cls = jes->jobs[j].job_class;
                    break;
                }
            }
            printf(" JOB%05d %-8s  %s  %c  %6c   %5d\n",
                   e->job_number, e->jobname, st, cls,
                   e->sysout_class, e->line_count);
        }
    }

    printf("\n");
    if (jes->spool_count > 0) {
        printf(" Enter spool entry number to view (or ENTER to return): ");
        char buf[32];
        if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
            int idx = atoi(buf);
            if (idx > 0 && idx <= jes->spool_count) {
                ZOS_SPOOL_ENTRY *e = &jes->spool[idx - 1];
                terminal_clear();
                print_header("SDSF - OUTPUT BROWSER");
                printf(" Job: JOB%05d  %-8s  DD: %.8s\n\n",
                       e->job_number, e->jobname, e->ddname);
                for (int i = 0; i < e->line_count; i++) {
                    printf(" %s\n", e->lines[i]);
                }
                printf("\n");
                print_footer("F3=END");
                printf(" Press ENTER...");
                fgets(buf, sizeof(buf), stdin);
            }
        }
    }
}

/* =========================================================================
 * SYSLOG VIEWER
 * ========================================================================= */

void as_monitor_show_syslog(void) {
    terminal_clear();
    print_header("SYSTEM LOG (SYSLOG) - RECENT MESSAGES");
    printf("\n");
    console_display_syslog(40);
    printf("\n");
    print_footer("F3=END");
}

/* =========================================================================
 * INTERACTIVE MONITOR LOOP
 * ========================================================================= */

int as_monitor_run(void) {
    char cmd[256];
    bool running = true;

    while (running) {
        as_monitor_show();

        printf("\n COMMAND ===> ");
        fflush(stdout);

        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        /* Strip newline */
        int len = (int)strlen(cmd);
        while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == '\r'))
            cmd[--len] = '\0';

        /* Uppercase */
        for (int i = 0; i < len; i++)
            cmd[i] = (char)toupper((unsigned char)cmd[i]);

        if (len == 0 || strcmp(cmd, "F3") == 0 || strcmp(cmd, "END") == 0 ||
            strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "QUIT") == 0) {
            running = false;
        }
        else if (strcmp(cmd, "DIAGRAM") == 0 || strcmp(cmd, "ARCH") == 0) {
            as_monitor_show_diagram();
            printf(" Press ENTER to continue...");
            fgets(cmd, sizeof(cmd), stdin);
        }
        else if (strcmp(cmd, "SPOOL") == 0 || strcmp(cmd, "SDSF") == 0) {
            as_monitor_show_spool();
        }
        else if (strcmp(cmd, "SYSLOG") == 0 || strcmp(cmd, "LOG") == 0) {
            as_monitor_show_syslog();
            printf(" Press ENTER to continue...");
            fgets(cmd, sizeof(cmd), stdin);
        }
        else if (strcmp(cmd, "SVC") == 0) {
            terminal_clear();
            print_header("SVC CALL STATISTICS");
            printf("\n");
            svc_display_stats();
            print_footer("F3=END");
            printf(" Press ENTER to continue...");
            fgets(cmd, sizeof(cmd), stdin);
        }
        else if (strncmp(cmd, "STORAGE", 7) == 0) {
            int asid = 0;
            if (sscanf(cmd + 7, "%x", (unsigned int *)&asid) == 1) {
                as_monitor_show_storage(asid);
                printf(" Press ENTER to continue...");
                fgets(cmd, sizeof(cmd), stdin);
            } else {
                printf(" Usage: STORAGE xxxx (ASID in hex)\n");
                printf(" Press ENTER...");
                fgets(cmd, sizeof(cmd), stdin);
            }
        }
        else if (strcmp(cmd, "F5") == 0 || strcmp(cmd, "REFRESH") == 0) {
            /* Loop again = refresh */
        }
        else {
            /* Try as a console command */
            CONSOLE_RESULT res = console_process(cmd);
            terminal_clear();
            print_header("CONSOLE COMMAND RESULT");
            printf("\n");
            printf("%s%s%s\n", TERM_CYAN, res.response, TERM_RESET);
            printf("\n");
            print_footer("F3=END");
            printf(" Press ENTER to continue...");
            fgets(cmd, sizeof(cmd), stdin);
        }
    }

    return 0;
}
