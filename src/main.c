/*
 * IBM z/OS Mainframe Simulator - Main Program
 *
 * Entry point. Boots the simulated z/OS system (IPL sequence),
 * initializes all subsystem address spaces, and launches the
 * TSO/ISPF-like terminal for interactive use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/ims.h"
#include "core/database.h"
#include "tm/msgqueue.h"
#include "tm/mpp.h"
#include "tm/bmp.h"
#include "ui/terminal.h"
#include "zos/address_space.h"
#include "zos/zos_init.h"
#include "zos/as_monitor.h"

/* Forward declarations */
void print_banner(void);
void print_usage(void);
int  run_batch_demo(void);

int main(int argc, char *argv[]) {
    bool interactive  = true;
    bool load_sample  = false;
    bool show_zos     = false;

    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--batch") == 0) {
            interactive = false;
        }
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--load") == 0) {
            load_sample = true;
        }
        if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--zos") == 0) {
            show_zos = true;
        }
    }

    print_banner();

    /*
     * BOOT z/OS ADDRESS SPACES
     *
     * This is the IPL (Initial Program Load) sequence. All subsystem
     * address spaces are created and activated in correct dependency order:
     *   MVS → RACF → JES2 → IMS CTL → IMS DBRC → DB2
     *
     * After this call, zos_system contains all active address spaces and
     * the IMS engine (ims_system) is initialized as part of IMS CTL startup.
     */
    if (zos_init("SYS1", "PLEX1") != 0) {
        fprintf(stderr, "FATAL: z/OS IPL FAILED - SYSTEM CANNOT START\n");
        return 1;
    }

    /* Initialize IMS TM subsystems */
    msgqueue_init();
    mpp_init();
    bmp_init();

    /* Load sample database if requested */
    if (load_sample) {
        load_hospital_database();

        /* Log the database load to JES2 sysout */
        zos_jes2_spool_write(0, "SYSTEM  ", "SYSOUT  ", 'X',
                              "IMS0001I HOSPITAL DATABASE LOADED SUCCESSFULLY");
    }

    /* If -z flag, go straight to AS monitor */
    if (show_zos) {
        as_monitor_run();
    }

    int result = 0;

    if (interactive) {
        result = terminal_run();
    } else {
        result = run_batch_demo();
    }

    /* Shutdown in reverse order */
    bmp_shutdown();
    mpp_shutdown();
    msgqueue_shutdown();

    zos_shutdown();   /* Shuts down all AS including IMS */

    return result;
}

/* =========================================================================
 * BANNER
 * ========================================================================= */

void print_banner(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("                                                                                \n");
    printf("  ██╗██████╗ ███╗   ███╗    ███████╗    ██╗ ██████╗ ███████╗                  \n");
    printf("  ██║██╔══██╗████╗ ████║    ╚════██║   ██╔╝██╔═══██╗██╔════╝                  \n");
    printf("  ██║██████╔╝██╔████╔██║        ██╔╝  ██╔╝ ██║   ██║███████╗                  \n");
    printf("  ██║██╔══██╗██║╚██╔╝██║       ██╔╝  ██╔╝  ██║   ██║╚════██║                  \n");
    printf("  ██║██████╔╝██║ ╚═╝ ██║       ██║  ██╔╝   ╚██████╔╝███████║                  \n");
    printf("  ╚═╝╚═════╝ ╚═╝     ╚═╝       ╚═╝  ╚═╝     ╚═════╝ ╚══════╝                  \n");
    printf("                                                                                \n");
    printf("           IBM z/OS Mainframe Simulator  (IMS %s)                        \n",
           IMS_VERSION);
    printf("           IMS DB/TM  │  DB2  │  COBOL  │  JCL  │  DFSMS                      \n");
    printf("                                                                                \n");
    printf("================================================================================\n");
    printf("\n");
}

void print_usage(void) {
    printf("IBM z/OS Mainframe Simulator\n\n");
    printf("  ims [options]\n\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -b, --batch    Run batch demo (non-interactive)\n");
    printf("  -l, --load     Load HOSPITAL sample IMS database\n");
    printf("  -z, --zos      Show z/OS IPL and Address Space Monitor\n");
    printf("\n");
    printf("Interactive Commands (TSO/ISPF terminal):\n");
    printf("  /DB            IMS Database Manager panel\n");
    printf("  /TM            IMS Transaction Manager panel\n");
    printf("  /DLI           DL/I call interface\n");
    printf("  /ZOS           z/OS Address Space Monitor\n");
    printf("  /LOAD          Load HOSPITAL sample database\n");
    printf("  /DISPLAY       Show system status\n");
    printf("  /HELP          Show help\n");
    printf("  /END           Exit simulator\n");
    printf("\n");
    printf("Address Space Monitor Commands:\n");
    printf("  D A,L          Display all address spaces\n");
    printf("  D IMS,STATUS   IMS control region status\n");
    printf("  D JES,STATUS   JES2 job queue status\n");
    printf("  D DB2,THREAD   DB2 active threads\n");
    printf("  D SVC          SVC call statistics\n");
    printf("  D CSA          Common Service Area map\n");
    printf("  DIAGRAM        Architecture diagram\n");
    printf("  SPOOL          JES2 spool (SDSF)\n");
    printf("  SYSLOG         System log (WTO messages)\n");
    printf("  STORAGE xxxx   Virtual storage map for ASID xxxx\n");
    printf("\n");
}

/* =========================================================================
 * BATCH DEMO
 * ========================================================================= */

int run_batch_demo(void) {
    printf("Running z/OS batch demo...\n\n");

    /* Simulate a JCL job submission */
    int job_num = zos_jes2_submit_job("HOSPINQ ", 'A', 'X');

    /* Create a batch address space for the job */
    ZOS_ADDRESS_SPACE *batch_as = zos_start_batch_job("HOSPINQ ", "HOSPINQ ", 'A');
    if (!batch_as) {
        fprintf(stderr, "Failed to create batch address space\n");
        return 1;
    }

    /* Load sample database */
    if (load_hospital_database() != 0) {
        fprintf(stderr, "Failed to load sample database\n");
        return 1;
    }

    /* Schedule PSB */
    IMS_PSB *psb = NULL;
    if (dli_pcb("HOSPPGM", &psb) != 0) {
        fprintf(stderr, "Failed to schedule PSB\n");
        return 1;
    }

    IMS_PCB *pcb = psb->db_pcbs[0];

    /* Write job header to sysout */
    zos_jes2_spool_write(job_num, "STEP01  ", "SYSOUT  ", 'X',
                          "//HOSPINQ  JOB (DEMO),'HOSPITAL INQUIRY',CLASS=A");
    zos_jes2_spool_write(job_num, "STEP01  ", "SYSOUT  ", 'X',
                          "//STEP01   EXEC PGM=HOSPINQ");
    zos_jes2_spool_write(job_num, "STEP01  ", "SYSOUT  ", 'X',
                          "//SYSOUT   DD SYSOUT=*");
    zos_jes2_spool_write(job_num, "STEP01  ", "SYSOUT  ", 'X', " ");

    printf("=== z/OS BATCH JOB: HOSPINQ (JOB%05d) ===\n\n", job_num);
    printf("Simulating COBOL program executing DL/I calls via SVC 30:\n\n");

    char io_area[256];
    memset(io_area, 0, sizeof(io_area));

    /* Use SVC 30 to issue DL/I (educational — routes through zos_system) */
    SVC30_PARM parm;
    memset(&parm, 0, sizeof(parm));
    parm.func_code = (int)DLI_GU;
    parm.pcb       = pcb;
    parm.io_area   = io_area;
    parm.ssa_count = 0;

    printf("COBOL: CALL 'CBLTDLI' USING 'GU  ' PCB-MASK WS-HOSP-SEG\n");
    printf("       → SVC 30 issued → IMS CTL (ASID %04X) executes DL/I\n",
           ASID_IMS_CTL);

    svc_issue(batch_as->asid, SVC_DLI, &parm);

    printf("       ← Return to COBOL: PCB STATUS = '%s' (%s)\n\n",
           IMS_STATUS_STR(pcb->status_code),
           ims_status_desc(pcb->status_code));

    if (parm.return_code == 0) {
        char hospcode[5] = {0};
        char hospname[31] = {0};
        memcpy(hospcode, io_area, 4);
        memcpy(hospname, io_area + 4, 30);
        printf("       DATA: HOSPCODE='%s'  HOSPNAME='%s'\n\n", hospcode, hospname);

        char spool_line[133];
        snprintf(spool_line, sizeof(spool_line),
                 "HOSPITAL: CODE=%-4s  NAME=%-30s", hospcode, hospname);
        zos_jes2_spool_write(job_num, "STEP01  ", "SYSPRINT", 'X', spool_line);
    }

    /* GN for WARD */
    memset(io_area, 0, sizeof(io_area));
    parm.func_code = (int)DLI_GN;
    printf("COBOL: CALL 'CBLTDLI' USING 'GN  ' PCB-MASK WS-SEG-AREA\n");
    printf("       → SVC 30 → IMS CTL → GN executed\n");
    svc_issue(batch_as->asid, SVC_DLI, &parm);
    printf("       ← STATUS='%s'  SEG='%.8s'  LEVEL=%d\n\n",
           IMS_STATUS_STR(pcb->status_code),
           pcb->segment_name, pcb->level);

    /* Complete the batch job */
    batch_as->return_code = 0;
    zos_as_terminate(batch_as, 0);
    zos_jes2_complete_job(job_num, 0);

    printf("=== JOB %05d ENDED  RC=0000 ===\n\n", job_num);

    /* Show final AS status */
    printf("=== FINAL ADDRESS SPACE STATUS ===\n\n");
    CONSOLE_RESULT r = console_process("D A,L");
    printf("%s\n", r.response);

    printf("=== SVC CALL SUMMARY ===\n");
    svc_display_stats();

    return 0;
}
