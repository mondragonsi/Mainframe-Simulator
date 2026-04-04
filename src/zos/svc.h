/*
 * z/OS SVC (Supervisor Call) Simulation
 *
 * In real z/OS, the SVC instruction causes a hardware interrupt that
 * transfers control to the MVS SVC handler. The handler dispatches
 * to the appropriate service routine based on the SVC number.
 *
 * Key SVCs simulated:
 *   SVC  1  - WAIT
 *   SVC  2  - POST
 *   SVC  4  - GETMAIN (allocate storage)
 *   SVC  5  - FREEMAIN (free storage)
 *   SVC 13  - ABEND
 *   SVC 19  - OPEN (dataset)
 *   SVC 20  - CLOSE (dataset)
 *   SVC 30  - DL/I (IMS database call - CBLTDLI)
 *   SVC 35  - WTO (Write to Operator)
 *   SVC 93  - TGET (TSO terminal get)
 *   SVC 94  - TPUT (TSO terminal put)
 *
 * IBM Reference: z/OS MVS Programming: Authorized Assembler Services Ref
 *                SA23-1372
 */

#ifndef ZOS_SVC_H
#define ZOS_SVC_H

#include "address_space.h"

/* SVC numbers */
#define SVC_WAIT         1
#define SVC_POST         2
#define SVC_GETMAIN      4
#define SVC_FREEMAIN     5
#define SVC_ABEND        13
#define SVC_OPEN         19
#define SVC_CLOSE        20
#define SVC_DLI          30   /* CBLTDLI - IMS DL/I interface */
#define SVC_WTO          35   /* Write to Operator */
#define SVC_ATTACH       42   /* Create subtask */
#define SVC_DETACH       43   /* Delete subtask */
#define SVC_TGET         93   /* TSO TGET */
#define SVC_TPUT         94   /* TSO TPUT */
#define SVC_SCHEDULE     255  /* Internal: schedule a new address space */

/* Parameter block for SVC 30 (DL/I) */
typedef struct {
    int   func_code;    /* DLI_FUNCTION enum value */
    void *pcb;          /* PCB pointer */
    void *io_area;      /* I/O area */
    void *ssa[15];      /* SSA array */
    int   ssa_count;
    int   return_code;  /* Filled by SVC handler */
} SVC30_PARM;

/* Parameter block for SVC 4 (GETMAIN) */
typedef struct {
    size_t  size;
    int     subpool;
    void   *result;     /* Filled by handler */
} SVC4_PARM;

/* Parameter block for SVC 35 (WTO) */
typedef struct {
    const char *message;
    int         route_codes;   /* Routing codes (ignored in simulator) */
    int         desc_codes;    /* Descriptor codes */
} SVC35_PARM;

/* Parameter block for SVC 13 (ABEND) */
typedef struct {
    int  abend_code;
    bool is_user;
    bool dump;
} SVC13_PARM;

/* Initialize the SVC dispatch table */
void svc_init(void);

/* Issue an SVC (called by simulated programs) */
int svc_issue(int caller_asid, int svc_number, void *parm);

/* Individual SVC handlers */
int svc_handle_wait(int caller_asid, void *parm);
int svc_handle_post(int caller_asid, void *parm);
int svc_handle_getmain(int caller_asid, void *parm);
int svc_handle_freemain(int caller_asid, void *parm);
int svc_handle_abend(int caller_asid, void *parm);
int svc_handle_open(int caller_asid, void *parm);
int svc_handle_close(int caller_asid, void *parm);
int svc_handle_dli(int caller_asid, void *parm);
int svc_handle_wto(int caller_asid, void *parm);
int svc_handle_tget(int caller_asid, void *parm);
int svc_handle_tput(int caller_asid, void *parm);

/* Display SVC statistics */
void svc_display_stats(void);

/* SVC call counter (for educational display) */
extern long svc_call_counts[ZOS_MAX_SVC];

#endif /* ZOS_SVC_H */
