/*
 * z/OS SVC Handler Implementation
 *
 * When a COBOL program calls CBLTDLI, it ultimately issues SVC 30.
 * MVS routes SVC 30 to the DL/I handler which switches execution
 * context to the IMS Control Region.
 *
 * Educational note for students:
 *   Real z/OS SVC processing:
 *   1. Program executes SVC instruction (e.g., SVC 30)
 *   2. Hardware causes Program Interrupt → SVC Old PSW saved
 *   3. MVS SVC interrupt handler runs in supervisor state, key 0
 *   4. Handler indexes into SVC table, calls service routine
 *   5. Service runs (may switch address spaces via cross-memory)
 *   6. Returns to caller via LPSW from SVC New PSW
 *
 *   In our simulator: this is a direct C function dispatch.
 *   The educational value is understanding WHAT each SVC does,
 *   not the actual interrupt mechanism.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "svc.h"
#include "address_space.h"
#include "../core/ims.h"

/* SVC call counters for display */
long svc_call_counts[ZOS_MAX_SVC];

/* =========================================================================
 * SVC TABLE INITIALIZATION
 * ========================================================================= */

void svc_init(void) {
    memset(svc_call_counts, 0, sizeof(svc_call_counts));
    memset(zos_system.svc_table, 0, sizeof(zos_system.svc_table));

    zos_system.svc_table[SVC_WAIT]     = svc_handle_wait;
    zos_system.svc_table[SVC_POST]     = svc_handle_post;
    zos_system.svc_table[SVC_GETMAIN]  = svc_handle_getmain;
    zos_system.svc_table[SVC_FREEMAIN] = svc_handle_freemain;
    zos_system.svc_table[SVC_ABEND]    = svc_handle_abend;
    zos_system.svc_table[SVC_OPEN]     = svc_handle_open;
    zos_system.svc_table[SVC_CLOSE]    = svc_handle_close;
    zos_system.svc_table[SVC_DLI]      = svc_handle_dli;
    zos_system.svc_table[SVC_WTO]      = svc_handle_wto;
    zos_system.svc_table[SVC_TGET]     = svc_handle_tget;
    zos_system.svc_table[SVC_TPUT]     = svc_handle_tput;
}

/* =========================================================================
 * SVC DISPATCH (entry point for all SVC calls)
 * ========================================================================= */

int svc_issue(int caller_asid, int svc_number, void *parm) {
    if (svc_number < 0 || svc_number >= ZOS_MAX_SVC) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "IEA000E INVALID SVC %d FROM ASID %04X", svc_number, caller_asid);
        zos_wto(caller_asid, msg);
        return -1;
    }

    svc_call_counts[svc_number]++;

    if (!zos_system.svc_table[svc_number]) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "IEA001E SVC %d NOT INSTALLED (ASID %04X)", svc_number, caller_asid);
        zos_wto(caller_asid, msg);
        return -1;
    }

    return zos_system.svc_table[svc_number](caller_asid, parm);
}

/* =========================================================================
 * SVC 1 — WAIT
 *
 * Suspends the calling task until a specified ECB is posted.
 * Simulation: mark AS as waiting, immediately re-activate (single-threaded).
 * ========================================================================= */

int svc_handle_wait(int caller_asid, void *parm) {
    (void)parm;
    ZOS_ADDRESS_SPACE *as = zos_as_find_asid(caller_asid);
    if (as && as->status == AS_STATUS_ACTIVE) {
        as->status = AS_STATUS_WAITING;
        /* In a real OS, dispatcher would switch. Here we immediately resume. */
        as->status = AS_STATUS_ACTIVE;
    }
    return 0;
}

/* =========================================================================
 * SVC 2 — POST
 *
 * Posts an ECB to wake a waiting task.
 * ========================================================================= */

int svc_handle_post(int caller_asid, void *parm) {
    (void)caller_asid;
    (void)parm;
    /* Simulation: no-op (WAIT immediately resumes in single-threaded sim) */
    return 0;
}

/* =========================================================================
 * SVC 4 — GETMAIN
 *
 * Allocates virtual storage in the caller's address space.
 *
 * Educational note: GETMAIN parameters include:
 *   R=VU  - Variable unconditional (abend if fails)
 *   R=VC  - Variable conditional (return code if fails)
 *   SP=   - Subpool number (0-127 key-8, 229-255 common)
 *   LV=   - Length of requested storage
 * ========================================================================= */

int svc_handle_getmain(int caller_asid, void *parm) {
    SVC4_PARM *p = (SVC4_PARM *)parm;
    if (!p) return -1;

    ZOS_ADDRESS_SPACE *as = zos_as_find_asid(caller_asid);
    if (!as) return -1;

    p->result = zos_getmain(as, p->size, p->subpool);
    return p->result ? 0 : 8;
}

/* =========================================================================
 * SVC 5 — FREEMAIN
 * ========================================================================= */

int svc_handle_freemain(int caller_asid, void *parm) {
    if (!parm) return 0;
    ZOS_ADDRESS_SPACE *as = zos_as_find_asid(caller_asid);
    if (!as) return -1;
    zos_freemain(as, parm);
    return 0;
}

/* =========================================================================
 * SVC 13 — ABEND
 *
 * Abnormally terminates the calling program.
 * Produces a dump (simulated) and terminates the address space.
 *
 * Educational note: ABEND macro parameters:
 *   ABEND 0C7         → system abend S0C7 (data exception)
 *   ABEND 100,USER    → user abend U0100
 *   ABEND 4095,DUMP   → abend with full dump to SYS1.DUMP
 * ========================================================================= */

int svc_handle_abend(int caller_asid, void *parm) {
    SVC13_PARM *p = (SVC13_PARM *)parm;
    ZOS_ADDRESS_SPACE *as = zos_as_find_asid(caller_asid);
    if (!as) return -1;

    ZOS_ABEND_CODE code = ABEND_NONE;
    bool is_user = false;
    int  user_code = 0;

    if (p) {
        is_user   = p->is_user;
        user_code = p->abend_code;
        if (!is_user) code = (ZOS_ABEND_CODE)p->abend_code;
    }

    zos_as_abend(as, code, is_user, user_code);
    return 0;
}

/* =========================================================================
 * SVC 19/20 — OPEN / CLOSE
 *
 * Opens/closes a data control block (DCB) for dataset access.
 * Full implementation deferred to DFSMS dataset module (spec 02).
 * ========================================================================= */

int svc_handle_open(int caller_asid, void *parm) {
    (void)parm;
    /* Placeholder — full implementation in src/datasets/ */
    char msg[80];
    snprintf(msg, sizeof(msg),
             "IEC143I OPEN ISSUED FROM ASID %04X (DFSMS not yet implemented)",
             caller_asid);
    zos_wto(caller_asid, msg);
    return 0;
}

int svc_handle_close(int caller_asid, void *parm) {
    (void)parm;
    (void)caller_asid;
    return 0;
}

/* =========================================================================
 * SVC 30 — DL/I  (CBLTDLI)
 *
 * THE most important SVC for IMS programmers. When a COBOL program
 * calls CBLTDLI, it issues SVC 30. MVS dispatches to the IMS SVC
 * handler which switches to the IMS Control Region's context and
 * executes the DL/I request.
 *
 * Educational flow:
 *   COBOL  →  CALL 'CBLTDLI'
 *          →  CBLTDLI stub issues SVC 30
 *          →  MVS routes to IMS SVC handler
 *          →  IMS CTL processes DL/I (GU, GN, ISRT, etc.)
 *          →  Results returned in PCB and I/O area
 *          →  Control returns to COBOL program
 *
 * In our simulator: direct C call (no actual SVC/interrupt hardware).
 * ========================================================================= */

int svc_handle_dli(int caller_asid, void *parm) {
    (void)caller_asid;
    SVC30_PARM *p = (SVC30_PARM *)parm;
    if (!p) return -1;

    /* Simulate the cross-AS call: switch to IMS CTL context */
    int prev_asid = zos_system.current_asid;
    zos_system.current_asid = ASID_IMS_CTL;

    /* Update IMS CTL statistics */
    ZOS_ADDRESS_SPACE *ims_ctl = zos_as_find_asid(ASID_IMS_CTL);
    if (ims_ctl && ims_ctl->subsystem_ctx) {
        ZOS_IMS_CTL_CTX *ctx = (ZOS_IMS_CTL_CTX *)ims_ctl->subsystem_ctx;
        ctx->dli_calls_total++;
        ims_ctl->cpu_time_ms += 1; /* Simulated CPU cost */
    }

    /*
     * Dispatch to the appropriate DL/I function.
     * The DLI_FUNCTION enum and actual implementations live in src/core/.
     * This SVC handler is the bridge between z/OS AS model and IMS DB.
     */
    int rc = 0;
    IMS_PCB *pcb = (IMS_PCB *)p->pcb;

    switch ((DLI_FUNCTION)p->func_code) {
        case DLI_GU:
            rc = dli_gu(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_GN:
            rc = dli_gn(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_GNP:
            rc = dli_gnp(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_GHU:
            rc = dli_ghu(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_GHN:
            rc = dli_ghn(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_GHNP:
            rc = dli_ghnp(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_ISRT:
            rc = dli_isrt(pcb, p->io_area, (IMS_SSA **)p->ssa, p->ssa_count);
            break;
        case DLI_DLET:
            rc = dli_dlet(pcb, p->io_area);
            break;
        case DLI_REPL:
            rc = dli_repl(pcb, p->io_area);
            break;
        default:
            rc = -1;
            break;
    }

    p->return_code = rc;

    /* Update stats */
    if (ims_ctl && ims_ctl->subsystem_ctx) {
        ZOS_IMS_CTL_CTX *ctx = (ZOS_IMS_CTL_CTX *)ims_ctl->subsystem_ctx;
        if (rc == 0) ctx->dli_calls_ok++;
        else         ctx->dli_calls_err++;
    }

    /* Restore caller's AS context */
    zos_system.current_asid = prev_asid;

    return rc;
}

/* =========================================================================
 * SVC 35 — WTO (Write to Operator)
 *
 * Sends a message to the operator console and system log.
 * Every subsystem uses WTO to report status, errors, and informational
 * messages. Message IDs follow the format: PPP000X where:
 *   PPP = product prefix (IEA=MVS, $HASP=JES2, DFS=IMS, DSN=DB2)
 *   000 = message number
 *   X   = type (I=informational, W=warning, E=error, A=action required)
 * ========================================================================= */

int svc_handle_wto(int caller_asid, void *parm) {
    SVC35_PARM *p = (SVC35_PARM *)parm;
    if (!p || !p->message) return 0;
    zos_wto(caller_asid, p->message);
    return 0;
}

/* =========================================================================
 * SVC 93/94 — TGET / TPUT (TSO Terminal I/O)
 *
 * Used by TSO applications to read from / write to the TSO terminal.
 * In batch, TPUT writes to SYSTSPRT; TGET reads from SYSTSIN.
 * ========================================================================= */

int svc_handle_tget(int caller_asid, void *parm) {
    (void)caller_asid;
    char *buf = (char *)parm;
    if (!buf) return 0;
    /* Read from stdin in simulator */
    if (!fgets(buf, 256, stdin)) return 4;  /* RC=4: no data */
    return 0;
}

int svc_handle_tput(int caller_asid, void *parm) {
    (void)caller_asid;
    const char *msg = (const char *)parm;
    if (msg) printf("%s\n", msg);
    return 0;
}

/* =========================================================================
 * SVC STATISTICS DISPLAY
 * ========================================================================= */

void svc_display_stats(void) {
    printf("\n");
    printf(" SVC CALL STATISTICS\n");
    printf(" %-5s  %-20s  %-10s\n", "SVC#", "Name", "Call Count");
    printf(" %-5s  %-20s  %-10s\n", "----", "--------------------", "----------");

    struct { int num; const char *name; } svc_names[] = {
        {SVC_WAIT,     "WAIT"},
        {SVC_POST,     "POST"},
        {SVC_GETMAIN,  "GETMAIN"},
        {SVC_FREEMAIN, "FREEMAIN"},
        {SVC_ABEND,    "ABEND"},
        {SVC_OPEN,     "OPEN"},
        {SVC_CLOSE,    "CLOSE"},
        {SVC_DLI,      "DLI (CBLTDLI)"},
        {SVC_WTO,      "WTO"},
        {SVC_TGET,     "TGET"},
        {SVC_TPUT,     "TPUT"},
        {0, NULL}
    };

    for (int i = 0; svc_names[i].name; i++) {
        int n = svc_names[i].num;
        if (svc_call_counts[n] > 0 || 1) {
            printf(" %-5d  %-20s  %ld\n",
                   n, svc_names[i].name, svc_call_counts[n]);
        }
    }
    printf("\n");
}
