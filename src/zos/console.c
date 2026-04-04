/*
 * z/OS Operator Console Implementation
 *
 * Processes MVS operator commands. These are the commands a systems
 * programmer would type at the Hardware Management Console (HMC) or
 * z/OS SDSF (System Display and Search Facility) to monitor and
 * control the system.
 *
 * Students learn: how operators interact with subsystems, what information
 * is available at the console, and how IMS/DB2/JES2 respond to commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "console.h"
#include "address_space.h"
#include "svc.h"
#include "../core/ims.h"

/* =========================================================================
 * TOKENIZER
 * ========================================================================= */

static int tokenize(const char *input, char tokens[][CONSOLE_TOKEN_LEN],
                    int max_tokens) {
    int count = 0;
    const char *p = input;

    while (*p && count < max_tokens) {
        /* Skip whitespace and commas */
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;

        /* Copy token */
        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && i < CONSOLE_TOKEN_LEN - 1) {
            tokens[count][i++] = toupper((unsigned char)*p++);
        }
        tokens[count][i] = '\0';
        if (i > 0) count++;
    }
    return count;
}

/* =========================================================================
 * CONSOLE INIT
 * ========================================================================= */

void console_init(void) {
    /* Nothing to initialize yet */
}

/* =========================================================================
 * MAIN DISPATCH
 * ========================================================================= */

CONSOLE_RESULT console_process(const char *command) {
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = false;

    if (!command || !*command) {
        snprintf(result.response, sizeof(result.response),
                 "IEE305I INVALID COMMAND - EMPTY INPUT");
        return result;
    }

    char tokens[CONSOLE_MAX_TOKENS][CONSOLE_TOKEN_LEN];
    memset(tokens, 0, sizeof(tokens));
    int count = tokenize(command, tokens, CONSOLE_MAX_TOKENS);

    if (count == 0) {
        snprintf(result.response, sizeof(result.response),
                 "IEE305I INVALID COMMAND");
        return result;
    }

    const char *ptokens[CONSOLE_MAX_TOKENS];
    for (int i = 0; i < count; i++) ptokens[i] = tokens[i];

    /* Dispatch by verb */
    if (strcmp(tokens[0], "D") == 0 || strcmp(tokens[0], "DISPLAY") == 0) {
        return console_cmd_display(ptokens, count);
    } else if (strcmp(tokens[0], "F") == 0 || strcmp(tokens[0], "MODIFY") == 0) {
        return console_cmd_modify(ptokens, count);
    } else if (strcmp(tokens[0], "S") == 0 || strcmp(tokens[0], "START") == 0) {
        return console_cmd_start(ptokens, count);
    } else if (strcmp(tokens[0], "P") == 0 || strcmp(tokens[0], "STOP") == 0) {
        return console_cmd_stop(ptokens, count);
    } else if (strcmp(tokens[0], "CANCEL") == 0 || strcmp(tokens[0], "C") == 0) {
        return console_cmd_cancel(ptokens, count);
    } else if (strcmp(tokens[0], "LOG") == 0 || strcmp(tokens[0], "SYSLOG") == 0) {
        return console_cmd_syslog(ptokens, count);
    } else {
        snprintf(result.response, sizeof(result.response),
                 "IEE305I %.32s  COMMAND INVALID", tokens[0]);
        return result;
    }
}

/* =========================================================================
 * D (DISPLAY) COMMAND
 *
 * D A,L           — display all address spaces (list)
 * D A,ASID=xxxx   — display specific address space
 * D IMS,STATUS    — IMS control region status
 * D IMS,REGION    — IMS region information
 * D JES,STATUS    — JES2 status
 * D DB2,THREAD    — DB2 active threads
 * D SVC           — SVC call statistics
 * D CSA           — CSA usage
 * D SYSLOG        — recent system log entries
 * ========================================================================= */

CONSOLE_RESULT console_cmd_display(const char **tokens, int count) {
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = true;

    char *out = result.response;
    int   rem = sizeof(result.response);
    int   n;

#define APPEND(...) do { n = snprintf(out, rem, __VA_ARGS__); if(n>0){out+=n;rem-=n;} } while(0)

    if (count < 2) {
        APPEND("IEE305I D COMMAND REQUIRES OPERAND\n");
        APPEND("        D A,L          - List address spaces\n");
        APPEND("        D IMS,STATUS   - IMS status\n");
        APPEND("        D JES,STATUS   - JES2 status\n");
        APPEND("        D DB2,THREAD   - DB2 threads\n");
        APPEND("        D SVC          - SVC statistics\n");
        APPEND("        D CSA          - CSA storage map\n");
        APPEND("        D SYSLOG       - Recent log entries\n");
        return result;
    }

    /* D A,L — Display all address spaces */
    if (strcmp(tokens[1], "A") == 0) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char ts[10];
        strftime(ts, sizeof(ts), "%H:%M:%S", t);

        APPEND("IEE114I %s\n", ts);
        APPEND(" ASID  NAME      TYPE          STATUS     CPU(ms)  PROGRAM  RC\n");
        APPEND(" ----  --------  ------------  ---------  -------  -------  --\n");

        for (int i = 0; i < zos_system.space_count; i++) {
            ZOS_ADDRESS_SPACE *as = zos_system.spaces[i];
            if (!as) continue;

            long elapsed_ms = (long)(now - as->start_time) * 1000 + as->cpu_time_ms;
            char rc_str[8];
            if (as->status == AS_STATUS_TERMINATED) {
                if (as->abended)
                    snprintf(rc_str, sizeof(rc_str), "%s",
                             zos_abend_name(as->abend_code, as->is_user_abend,
                                            as->user_abend_code));
                else
                    snprintf(rc_str, sizeof(rc_str), "%04d", as->return_code);
            } else {
                strcpy(rc_str, "--  ");
            }

            APPEND(" %04X  %-8s  %-12s  %-9s  %7ld  %-8s %s\n",
                   as->asid, as->name,
                   zos_as_type_name(as->type),
                   zos_as_status_name(as->status),
                   elapsed_ms,
                   as->pgm_name,
                   rc_str);
        }
        APPEND(" %d ADDRESS SPACE(S) DISPLAYED\n", zos_system.space_count);
    }

    /* D IMS,STATUS */
    else if (strcmp(tokens[1], "IMS") == 0) {
        ZOS_ADDRESS_SPACE *ims_ctl = zos_as_find_asid(ASID_IMS_CTL);
        if (!ims_ctl) {
            APPEND("IEE341I IMS CONTROL REGION NOT FOUND\n");
            return result;
        }
        ZOS_IMS_CTL_CTX *ctx = (ZOS_IMS_CTL_CTX *)ims_ctl->subsystem_ctx;

        APPEND("DFS000I IMS CONTROL REGION STATUS\n");
        APPEND(" IMSID:        %s\n", ctx ? ctx->imsid : "????");
        APPEND(" STATUS:       %s\n", ims_ctl->status == AS_STATUS_ACTIVE ? "ACTIVE" : "INACTIVE");
        APPEND(" ASID:         %04X\n", ims_ctl->asid);
        APPEND(" DB BUFFER:    %zu KB\n", ctx ? ctx->db_buffer_pool_size / 1024 : 0);
        APPEND(" MSG BUFFER:   %zu KB\n", ctx ? ctx->msg_buffer_pool_size / 1024 : 0);
        APPEND(" DL/I CALLS:   %ld total, %ld OK, %ld errors\n",
               ctx ? ctx->dli_calls_total : 0L,
               ctx ? ctx->dli_calls_ok    : 0L,
               ctx ? ctx->dli_calls_err   : 0L);
        APPEND(" MPP REGIONS:  %d active\n", ctx ? ctx->mpp_count : 0);
        APPEND(" BMP REGIONS:  %d active\n", ctx ? ctx->bmp_count : 0);
        APPEND(" IMS DBDs:     %d registered\n", ims_system.dbd_count);
        APPEND(" IMS PSBs:     %d registered\n", ims_system.psb_count);
        APPEND(" TRANSACTIONS: %d defined\n", ims_system.transaction_count);
        APPEND(" INPUT QUEUE:  %d messages\n", ims_system.input_queue.count);
        APPEND(" OUTPUT QUEUE: %d messages\n", ims_system.output_queue.count);
    }

    /* D JES,STATUS */
    else if (strcmp(tokens[1], "JES") == 0) {
        ZOS_JES2_CTX *jes = &zos_system.jes2;
        APPEND("$HASP890 JES2 STATUS\n");
        APPEND(" JES2 ASID:    %04X\n", ASID_JES2);
        APPEND(" NEXT JOB#:    JOB%05d\n", jes->next_job_number);
        APPEND(" JOBS IN QUEUE: %d\n", jes->job_count);
        APPEND(" SPOOL ENTRIES: %d\n", jes->spool_count);
        APPEND("\n");
        APPEND(" INITIATORS:\n");
        APPEND(" ID  CLASSES  STATUS\n");
        APPEND(" --  -------  --------\n");
        for (int i = 0; i < jes->initiator_count; i++) {
            APPEND(" %02d  %-7s  %s\n",
                   jes->initiators[i].id,
                   jes->initiators[i].classes,
                   jes->initiators[i].is_active ? "ACTIVE" : "INACTIVE");
        }
        if (jes->job_count > 0) {
            APPEND("\n RECENT JOBS:\n");
            APPEND(" JOB#     JOBNAME   CL STATUS   RC\n");
            APPEND(" ------   --------  -- --------  ----\n");
            int start = jes->job_count > 10 ? jes->job_count - 10 : 0;
            for (int i = start; i < jes->job_count; i++) {
                struct JES_JOB_ENTRY *j = &jes->jobs[i];
                const char *status = j->is_complete ? "ENDED    " :
                                     j->is_running  ? "RUNNING  " : "WAITING  ";
                char rc_str[8];
                if (j->is_complete) snprintf(rc_str, sizeof(rc_str), "%04d", j->return_code);
                else strcpy(rc_str, "----");
                APPEND(" JOB%05d %-8s  %c  %s  %s\n",
                       j->job_number, j->jobname, j->job_class, status, rc_str);
            }
        }
    }

    /* D DB2,THREAD */
    else if (strcmp(tokens[1], "DB2") == 0) {
        ZOS_ADDRESS_SPACE *db2_as = zos_as_find_asid(ASID_DB2);
        if (!db2_as) {
            APPEND("DSNC006E DB2 ADDRESS SPACE NOT FOUND\n");
            return result;
        }
        ZOS_DB2_CTX *ctx = (ZOS_DB2_CTX *)db2_as->subsystem_ctx;
        APPEND("DSNV401I DB2 STATUS\n");
        APPEND(" SUBSYSTEM:    %s\n", ctx ? ctx->subsystem_name : "????");
        APPEND(" ASID:         %04X\n", db2_as->asid);
        APPEND(" STATUS:       %s\n",
               db2_as->status == AS_STATUS_ACTIVE ? "ACTIVE" : "INACTIVE");
        APPEND(" BUFFER POOL:  %zu KB\n", ctx ? ctx->buffer_pool_size / 1024 : 0);
        APPEND(" THREADS:      %d active\n", ctx ? ctx->thread_count : 0);
        if (ctx && ctx->thread_count > 0) {
            APPEND("\n THREAD  ASID  PLAN      CORRELATION   UOW\n");
            APPEND(" ------  ----  --------  ------------  ---\n");
            for (int i = 0; i < ctx->thread_count; i++) {
                APPEND(" %6d  %04X  %-8s  %-12s  %s\n",
                       i + 1,
                       ctx->threads[i].caller_asid,
                       ctx->threads[i].plan_name,
                       ctx->threads[i].correlation_id,
                       ctx->threads[i].in_uow ? "YES" : "NO");
            }
        }
    }

    /* D SVC — SVC statistics */
    else if (strcmp(tokens[1], "SVC") == 0) {
        APPEND("IEE174I SVC CALL STATISTICS\n");
        APPEND(" SVC#  NAME                  CALLS\n");
        APPEND(" ----  --------------------  -----\n");

        struct { int num; const char *name; } svc_names[] = {
            {SVC_WAIT,     "WAIT"         },
            {SVC_POST,     "POST"         },
            {SVC_GETMAIN,  "GETMAIN"      },
            {SVC_FREEMAIN, "FREEMAIN"     },
            {SVC_ABEND,    "ABEND"        },
            {SVC_OPEN,     "OPEN"         },
            {SVC_CLOSE,    "CLOSE"        },
            {SVC_DLI,      "DLI (CBLTDLI)"},
            {SVC_WTO,      "WTO"          },
            {SVC_TGET,     "TGET"         },
            {SVC_TPUT,     "TPUT"         },
            {0, NULL}
        };
        for (int i = 0; svc_names[i].name; i++) {
            int sn = svc_names[i].num;
            APPEND("   %3d  %-20s  %ld\n", sn, svc_names[i].name, svc_call_counts[sn]);
        }
    }

    /* D CSA — CSA storage map */
    else if (strcmp(tokens[1], "CSA") == 0) {
        APPEND("IEE129I CSA STORAGE MAP\n");
        APPEND(" TOTAL:  %d KB\n", ZOS_CSA_SIZE / 1024);
        APPEND(" USED:   %zu KB\n", zos_system.csa_used / 1024);
        APPEND(" FREE:   %zu KB\n", (ZOS_CSA_SIZE - zos_system.csa_used) / 1024);
        APPEND("\n ALLOCATIONS:\n");
        APPEND(" OFFSET    SIZE    OWNER    NAME\n");
        APPEND(" -------   -----   ------   --------\n");
        for (int i = 0; i < zos_system.csa_alloc_count; i++) {
            ZOS_CSA_BLOCK *b = &zos_system.csa_allocs[i];
            if (b->in_use) {
                APPEND(" %6zuK   %4zuK   %04X     %s\n",
                       b->offset / 1024, b->size / 1024,
                       b->owner_asid, b->owner_name);
            }
        }
    }

    /* D SYSLOG */
    else if (strcmp(tokens[1], "SYSLOG") == 0) {
        console_display_syslog(20);
        APPEND("[See above for syslog output]\n");
    }

    else {
        APPEND("IEE305I DISPLAY OPERAND %.32s NOT RECOGNIZED\n", tokens[1]);
        APPEND("        VALID: A,L  IMS  JES  DB2  SVC  CSA  SYSLOG\n");
        result.success = false;
    }

#undef APPEND
    return result;
}

/* =========================================================================
 * F (MODIFY) COMMAND
 *
 * F IMSCTL,STATUS           — same as D IMS,STATUS
 * F IMSCTL,PSTOP TRN txncode — stop accepting a transaction
 * F IMSCTL,RESUME TRN txncode — resume a transaction
 * F IMSCTL,CHECKPOINT        — take an IMS checkpoint
 * ========================================================================= */

CONSOLE_RESULT console_cmd_modify(const char **tokens, int count) {
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = true;

    char *out = result.response;
    int   rem = sizeof(result.response);
    int   n;

#define APPEND(...) do { n = snprintf(out, rem, __VA_ARGS__); if(n>0){out+=n;rem-=n;} } while(0)

    if (count < 3) {
        APPEND("IEE305I F COMMAND: F target,action\n");
        APPEND("        F IMSCTL,STATUS\n");
        APPEND("        F IMSCTL,CHECKPOINT\n");
        return result;
    }

    const char *target = tokens[1];
    const char *action = tokens[2];

    /* IMSCTL commands */
    if (strncmp(target, "IMSCTL", 6) == 0) {
        ZOS_ADDRESS_SPACE *ims_as = zos_as_find_asid(ASID_IMS_CTL);
        ZOS_IMS_CTL_CTX *ctx = ims_as ?
            (ZOS_IMS_CTL_CTX *)ims_as->subsystem_ctx : NULL;

        if (strcmp(action, "STATUS") == 0) {
            /* Delegate to D IMS,STATUS */
            const char *disp_tokens[] = {"D", "IMS"};
            return console_cmd_display(disp_tokens, 2);
        }
        else if (strcmp(action, "CHECKPOINT") == 0) {
            if (ctx) ctx->checkpoint_count++;
            APPEND("DFS058I IMS CHECKPOINT TAKEN\n");
            zos_wto(ASID_IMS_CTL, "DFS058I IMS CHECKPOINT TAKEN");
        }
        else if (strcmp(action, "PSTOP") == 0) {
            const char *txn = (count >= 4) ? tokens[3] : "????????";
            APPEND("DFS058I TRANSACTION %.8s STOPPED\n", txn);
            zos_wto(ASID_IMS_CTL, "DFS058I TRANSACTION STOPPED");
        }
        else if (strcmp(action, "RESUME") == 0) {
            const char *txn = (count >= 4) ? tokens[3] : "????????";
            APPEND("DFS058I TRANSACTION %.8s RESUMED\n", txn);
        }
        else {
            APPEND("DFS000E UNKNOWN IMS COMMAND: %.32s\n", action);
            result.success = false;
        }
    }
    else {
        APPEND("IEE341I TARGET %.32s NOT FOUND\n", target);
        result.success = false;
    }

#undef APPEND
    return result;
}

/* =========================================================================
 * S (START) COMMAND
 *
 * S IMSMPP01       — start an MPP region
 * S IMSBMP01       — start a BMP region
 * ========================================================================= */

CONSOLE_RESULT console_cmd_start(const char **tokens, int count) {
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = true;

    if (count < 2) {
        snprintf(result.response, sizeof(result.response),
                 "IEE305I S COMMAND: S procname");
        return result;
    }

    const char *proc = tokens[1];
    snprintf(result.response, sizeof(result.response),
             "IEF695I START %s COMMAND ACCEPTED", proc);
    zos_wto(0, result.response);
    return result;
}

/* =========================================================================
 * P (STOP) COMMAND
 *
 * P IMSMPP01       — stop an MPP region
 * ========================================================================= */

CONSOLE_RESULT console_cmd_stop(const char **tokens, int count) {
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = true;

    if (count < 2) {
        snprintf(result.response, sizeof(result.response),
                 "IEE305I P COMMAND: P asname");
        return result;
    }

    const char *target = tokens[1];
    ZOS_ADDRESS_SPACE *as = zos_as_find_name(target);
    if (!as) {
        snprintf(result.response, sizeof(result.response),
                 "IEE341I %.32s NOT FOUND", target);
        result.success = false;
        return result;
    }

    if (as->type == AS_TYPE_MVS || as->type == AS_TYPE_RACF) {
        snprintf(result.response, sizeof(result.response),
                 "IEE334I CANNOT STOP SYSTEM ADDRESS SPACE %.8s", as->name);
        result.success = false;
        return result;
    }

    zos_as_terminate(as, 0);
    snprintf(result.response, sizeof(result.response),
             "IEF196I IEF142I %.8s - STOP INITIATED", as->name);
    return result;
}

/* =========================================================================
 * CANCEL COMMAND
 *
 * CANCEL JOB00123   — cancel a running job (causes S222 abend)
 * CANCEL IMSMPP01   — cancel an MPP region
 * ========================================================================= */

CONSOLE_RESULT console_cmd_cancel(const char **tokens, int count) {
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = true;

    if (count < 2) {
        snprintf(result.response, sizeof(result.response),
                 "IEE305I CANCEL COMMAND: CANCEL asname");
        return result;
    }

    const char *target = tokens[1];
    ZOS_ADDRESS_SPACE *as = zos_as_find_name(target);
    if (!as) {
        snprintf(result.response, sizeof(result.response),
                 "IEE341I %.32s NOT ACTIVE", target);
        result.success = false;
        return result;
    }

    /* S222 abend = operator cancel */
    zos_as_abend(as, ABEND_S222, false, 0);
    snprintf(result.response, sizeof(result.response),
             "IEF450I %.8s CANCEL COMMAND ACCEPTED - ASID %04X",
             as->name, as->asid);
    return result;
}

/* =========================================================================
 * SYSLOG DISPLAY
 * ========================================================================= */

CONSOLE_RESULT console_cmd_syslog(const char **tokens, int count) {
    (void)tokens; (void)count;
    CONSOLE_RESULT result;
    memset(&result, 0, sizeof(result));
    result.success = true;
    console_display_syslog(30);
    snprintf(result.response, sizeof(result.response),
             "IEE141I SYSLOG DISPLAY COMPLETE");
    return result;
}

void console_display_syslog(int last_n_lines) {
    if (zos_system.syslog_len == 0) {
        printf(" (syslog empty)\n");
        return;
    }

    /* Count lines, find starting point */
    const char *p = zos_system.syslog;
    int total_lines = 0;
    while (*p) {
        if (*p == '\n') total_lines++;
        p++;
    }

    int skip = total_lines > last_n_lines ? total_lines - last_n_lines : 0;
    p = zos_system.syslog;
    int skipped = 0;
    while (*p && skipped < skip) {
        if (*p == '\n') skipped++;
        p++;
    }

    printf("\033[36m");
    while (*p) putchar(*p++);
    printf("\033[0m");
}
