/*
 * IDCAMS Utility Simulation
 *
 * IDCAMS (Integrated Data Catalog Access Method Services) is the z/OS
 * utility program for managing VSAM clusters and ICF catalog entries.
 * Job steps that manage datasets run IDCAMS via:
 *
 *   //STEP01  EXEC PGM=IDCAMS
 *   //SYSIN   DD *
 *     DEFINE CLUSTER (NAME(MY.VSAM.FILE) INDEXED KEYS(8 0) -
 *                     RECORDSIZE(80 80) TRACKS(10 5))     -
 *            DATA    (NAME(MY.VSAM.FILE.DATA))            -
 *            INDEX   (NAME(MY.VSAM.FILE.INDEX))
 *   /*
 *
 * Commands simulated:
 *   DEFINE CLUSTER   - Create VSAM KSDS/ESDS/RRDS
 *   DEFINE GDG       - Create GDG base
 *   DELETE           - Delete dataset or cluster
 *   LISTCAT          - List catalog entries
 *   REPRO            - Copy records between datasets
 *   PRINT            - Print dataset records
 *   ALTER            - Modify catalog attributes
 *   VERIFY           - Check dataset integrity (simulated as no-op)
 *
 * IDCAMS return codes:
 *   0  = All commands successful
 *   4  = Warning (partial success)
 *   8  = Error in command
 *   12 = Serious error
 *   16 = Catastrophic error
 *
 * IBM Reference: DFSMS Access Method Services Commands (SC23-6846)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "datasets.h"

/* =========================================================================
 * TOKENIZER
 * ========================================================================= */

#define IDCAMS_MAX_TOKENS 64
#define IDCAMS_TOK_LEN    64

static int idcams_tokenize(const char *input, char tokens[][IDCAMS_TOK_LEN],
                            int max) {
    int count = 0;
    const char *p = input;

    while (*p && count < max) {
        /* Skip whitespace, commas, continuation chars */
        while (*p && (*p == ' ' || *p == '\t' || *p == ',' ||
                      *p == '-' || *p == '\n' || *p == '\r')) p++;
        if (!*p) break;

        /* Skip comments: /* ... (in real IDCAMS, /* ends inline data) */
        if (*p == '/' && *(p+1) == '*') break;

        int i = 0;
        /* Parenthesized group: collect as single token */
        if (*p == '(') {
            int depth = 0;
            while (*p && i < IDCAMS_TOK_LEN - 1) {
                if (*p == '(') depth++;
                else if (*p == ')') { depth--; tokens[count][i++] = *p++; if (!depth) break; }
                tokens[count][i++] = *p++;
            }
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != ',' &&
                   *p != '-' && *p != '\n' && *p != '\r' &&
                   *p != '(' && i < IDCAMS_TOK_LEN - 1) {
                tokens[count][i++] = toupper((unsigned char)*p++);
            }
            /* Attach trailing parenthesized group: NAME(X) stays one token */
            if (*p == '(' && i < IDCAMS_TOK_LEN - 2) {
                int depth = 0;
                while (*p && i < IDCAMS_TOK_LEN - 1) {
                    if (*p == '(') depth++;
                    else if (*p == ')') {
                        depth--;
                        tokens[count][i++] = *p++;
                        if (!depth) break;
                        continue;
                    }
                    tokens[count][i++] = *p++;
                }
            }
        }
        tokens[count][i] = '\0';
        if (i > 0) count++;
    }
    return count;
}

/* Strip outer parens from command string if present.
 * "(NAME(X) INDEXED)" → "NAME(X) INDEXED"
 * Writes into buf; returns buf pointer. */
static const char *strip_outer_parens(const char *cmd, char *buf, int buflen) {
    const char *p = cmd;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '(') return cmd;  /* no outer paren */
    p++;  /* skip opening ( */
    /* find matching closing paren */
    int depth = 1, i = 0;
    while (*p && i < buflen - 1) {
        if (*p == '(') depth++;
        else if (*p == ')') { depth--; if (depth == 0) break; }
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return buf;
}

/* Extract a parameter value: NAME(value) → value */
static int extract_paren_value(const char *token, char *out, int outlen) {
    const char *p = strchr(token, '(');
    if (!p) return -1;
    p++;
    int i = 0;
    while (*p && *p != ')' && i < outlen - 1) out[i++] = *p++;
    out[i] = '\0';
    return i;
}

/* =========================================================================
 * DEFINE CLUSTER
 *
 * Parses a simplified DEFINE CLUSTER command.
 * Supported keywords: NAME, INDEXED/NONINDEXED/NUMBERED,
 *                     KEYS(len offset), RECORDSIZE(avg max), TRACKS, CYLINDERS
 *
 * Examples:
 *   DEFINE CLUSTER (NAME(MY.KSDS) INDEXED KEYS(8 0) RECORDSIZE(100 200))
 *   DEFINE CLUSTER (NAME(MY.ESDS) NONINDEXED RECORDSIZE(80 80))
 *   DEFINE CLUSTER (NAME(MY.RRDS) NUMBERED RECORDSIZE(50 50) RECORDS(100))
 * ========================================================================= */

int idcams_define_cluster(const char *command) {
    char tokens[IDCAMS_MAX_TOKENS][IDCAMS_TOK_LEN];
    char stripped[512];
    const char *cmd = strip_outer_parens(command, stripped, sizeof(stripped));
    int  count = idcams_tokenize(cmd, tokens, IDCAMS_MAX_TOKENS);

    char dsn[DS_DSN_LEN + 1] = {0};
    ZOS_VSAM_TYPE vtype = VSAM_KSDS;  /* default INDEXED */
    int key_offset  = 0;
    int key_len     = 8;
    int avg_len     = 80;
    int max_len     = 80;

    for (int i = 0; i < count; i++) {
        if (strncmp(tokens[i], "NAME(", 5) == 0) {
            extract_paren_value(tokens[i], dsn, sizeof(dsn));
        } else if (strcmp(tokens[i], "INDEXED") == 0) {
            vtype = VSAM_KSDS;
        } else if (strcmp(tokens[i], "NONINDEXED") == 0) {
            vtype = VSAM_ESDS;
        } else if (strcmp(tokens[i], "NUMBERED") == 0) {
            vtype = VSAM_RRDS;
        } else if (strncmp(tokens[i], "KEYS(", 5) == 0) {
            char val[32];
            extract_paren_value(tokens[i], val, sizeof(val));
            sscanf(val, "%d %d", &key_len, &key_offset);
        } else if (strncmp(tokens[i], "RECORDSIZE(", 11) == 0) {
            char val[32];
            extract_paren_value(tokens[i], val, sizeof(val));
            sscanf(val, "%d %d", &avg_len, &max_len);
        }
    }

    if (!dsn[0]) {
        printf(" IDC3009I ** NAME REQUIRED IN CLUSTER DEFINITION\n");
        return 8;
    }

    ZOS_DATASET *ds = NULL;
    switch (vtype) {
        case VSAM_KSDS:
            ds = vsam_define_ksds(dsn, key_offset, key_len, avg_len, max_len);
            break;
        case VSAM_ESDS:
            ds = vsam_define_esds(dsn, avg_len, max_len);
            break;
        case VSAM_RRDS:
            ds = vsam_define_rrds(dsn, avg_len, 100);
            break;
        default:
            break;
    }

    if (!ds) {
        printf(" IDC3013I ** CLUSTER %s ALREADY DEFINED OR CATALOG FULL\n", dsn);
        return 8;
    }

    const char *type_str = vtype == VSAM_KSDS ? "KSDS" :
                           vtype == VSAM_ESDS ? "ESDS" : "RRDS";
    printf(" IDC0508I DATA ALLOCATION STATUS FOR VOLUME SIMVOL IS 0\n");
    printf(" IDC0509I INDEX ALLOCATION STATUS FOR VOLUME SIMVOL IS 0\n");
    printf(" IDC1566I %s CLUSTER DEFINED -- %s\n", type_str, dsn);
    return 0;
}

/* =========================================================================
 * DEFINE GDG
 *
 * Example:
 *   DEFINE GDG (NAME(MY.BACKUP) LIMIT(7) NOEMPTY SCRATCH)
 * ========================================================================= */

static int idcams_define_gdg(const char *command) {
    char tokens[IDCAMS_MAX_TOKENS][IDCAMS_TOK_LEN];
    char stripped[512];
    const char *cmd = strip_outer_parens(command, stripped, sizeof(stripped));
    int  count = idcams_tokenize(cmd, tokens, IDCAMS_MAX_TOKENS);

    char dsn[DS_DSN_LEN + 1] = {0};
    int  limit   = 7;
    bool empty   = false;
    bool scratch = false;

    for (int i = 0; i < count; i++) {
        if (strncmp(tokens[i], "NAME(", 5) == 0) {
            extract_paren_value(tokens[i], dsn, sizeof(dsn));
        } else if (strncmp(tokens[i], "LIMIT(", 6) == 0) {
            char val[16];
            extract_paren_value(tokens[i], val, sizeof(val));
            limit = atoi(val);
        } else if (strcmp(tokens[i], "EMPTY")   == 0) { empty   = true; }
        else if (strcmp(tokens[i], "NOEMPTY")   == 0) { empty   = false; }
        else if (strcmp(tokens[i], "SCRATCH")   == 0) { scratch = true; }
        else if (strcmp(tokens[i], "NOSCRATCH") == 0) { scratch = false; }
    }

    if (!dsn[0]) { printf(" IDC3009I ** NAME REQUIRED IN GDG DEFINITION\n"); return 8; }

    ZOS_DATASET *base = gdg_define_base(dsn, limit, empty, scratch);
    if (!base) {
        printf(" IDC3013I ** GDG BASE %s ALREADY EXISTS\n", dsn);
        return 8;
    }

    printf(" IDC1566I GDG BASE DEFINED -- %s  LIMIT(%d)\n", dsn, limit);
    return 0;
}

/* =========================================================================
 * DELETE
 *
 * Examples:
 *   DELETE (MY.VSAM.FILE) CLUSTER
 *   DELETE (MY.SEQ.FILE) NONVSAM
 *   DELETE (MY.GDG.BASE) GDG PURGE
 * ========================================================================= */

int idcams_delete(const char *dsn, bool purge) {
    ZOS_DATASET *ds = ds_catalog_find(dsn);
    if (!ds) {
        printf(" IDC3012I ENTRY %s NOT FOUND\n", dsn);
        return 8;
    }

    /* For GDG base: first delete all generations */
    if (ds->dsorg == DSORG_GDG) {
        for (int i = 0; i < ds->gdg_gen_count; i++) {
            if (purge || ds->gdg_scratch) {
                ds_catalog_delete(ds->gdg_gens[i].dsn);
            }
            ds->gdg_gens[i].is_cataloged = false;
            ds->gdg_gens[i].is_active    = false;
        }
    }

    if (ds_catalog_delete(dsn) != DS_OK) {
        printf(" IDC3011I ENTRY %s IN USE\n", dsn);
        return 8;
    }

    printf(" IDC0550I ENTRY %s DELETED\n", dsn);
    return 0;
}

/* =========================================================================
 * LISTCAT
 * ========================================================================= */

int idcams_listcat(const char *filter) {
    ds_catalog_listcat(filter);
    return 0;
}

/* =========================================================================
 * REPRO
 *
 * Copies records from one dataset to another.
 * Works for PS→PS, VSAM→VSAM, PS→VSAM, VSAM→PS.
 *
 * Example:
 *   REPRO INFILE(INDD) OUTFILE(OUTDD)
 *   REPRO INDATASET(MY.INPUT) OUTDATASET(MY.OUTPUT)
 * ========================================================================= */

int idcams_repro(const char *indsn, const char *outdsn) {
    ZOS_DATASET *in_ds  = ds_catalog_find(indsn);
    ZOS_DATASET *out_ds = ds_catalog_find(outdsn);

    if (!in_ds) {
        printf(" IDC3012I INPUT ENTRY %s NOT FOUND\n", indsn);
        return 8;
    }
    if (!out_ds) {
        printf(" IDC3012I OUTPUT ENTRY %s NOT FOUND\n", outdsn);
        return 8;
    }

    int copied = 0;

    /* PS → PS or PS → VSAM(ESDS/KSDS) */
    if (in_ds->dsorg == DSORG_PS) {
        ZOS_DCB *in_dcb = ps_open(indsn, "REPRO-IN", OPEN_INPUT,
                                   in_ds->recfm, in_ds->lrecl, in_ds->blksize);
        if (!in_dcb) { printf(" IDC3011I CANNOT OPEN INPUT\n"); return 8; }

        char buf[DS_MAX_LRECL];
        int  len;

        if (out_ds->dsorg == DSORG_PS) {
            ZOS_DCB *out_dcb = ps_open(outdsn, "REPRO-OUT", OPEN_OUTPUT,
                                        in_ds->recfm, in_ds->lrecl, in_ds->blksize);
            if (!out_dcb) { ps_close(in_dcb); return 8; }
            while (ps_read(in_dcb, buf, &len) == DS_OK) {
                ps_write(out_dcb, buf, len);
                copied++;
            }
            ps_close(out_dcb);
        } else if (out_ds->dsorg == DSORG_VS) {
            ZOS_DCB *out_dcb = vsam_open(outdsn, "REPRO-OUT", OPEN_OUTPUT);
            if (!out_dcb) { ps_close(in_dcb); return 8; }
            ZOS_RPL rpl;
            memset(&rpl, 0, sizeof(rpl));
            rpl.acb    = out_dcb;
            rpl.option = RPL_OPT_KEY;
            while (ps_read(in_dcb, buf, &len) == DS_OK) {
                rpl.area    = (unsigned char *)buf;
                rpl.arealen = len;
                vsam_put(out_dcb, &rpl);
                copied++;
            }
            vsam_close(out_dcb);
        }
        ps_close(in_dcb);
    }
    /* VSAM → PS */
    else if (in_ds->dsorg == DSORG_VS && out_ds->dsorg == DSORG_PS) {
        ZOS_DCB *in_dcb  = vsam_open(indsn,  "REPRO-IN",  OPEN_INPUT);
        ZOS_DCB *out_dcb = ps_open(outdsn, "REPRO-OUT", OPEN_OUTPUT,
                                    RECFM_VB, in_ds->avg_rec_len, 0);
        if (!in_dcb || !out_dcb) {
            if (in_dcb)  vsam_close(in_dcb);
            if (out_dcb) ps_close(out_dcb);
            return 8;
        }
        char buf[DS_MAX_LRECL];
        ZOS_RPL rpl;
        memset(&rpl, 0, sizeof(rpl));
        rpl.acb     = in_dcb;
        rpl.area    = (unsigned char *)buf;
        rpl.arealen = sizeof(buf);
        rpl.option  = RPL_OPT_KEY;
        rpl.locate  = RPL_LOC_FIRST;
        while (vsam_get(in_dcb, &rpl) == 0) {
            ps_write(out_dcb, buf, rpl.reclen);
            rpl.locate = RPL_LOC_FWD;
            copied++;
        }
        vsam_close(in_dcb);
        ps_close(out_dcb);
    }

    printf(" IDC1176I %d RECORDS PROCESSED\n", copied);
    return 0;
}

/* =========================================================================
 * PRINT
 *
 * Prints records from a dataset to SYSPRINT (stdout in simulator).
 * Format: CHARACTER (text), HEX (hex dump), or DUMP (both).
 *
 * Example:
 *   PRINT INDATASET(MY.SEQ.FILE) CHARACTER COUNT(10)
 * ========================================================================= */

int idcams_print(const char *dsn) {
    ZOS_DATASET *ds = ds_catalog_find(dsn);
    if (!ds) {
        printf(" IDC3012I ENTRY %s NOT FOUND\n", dsn);
        return 8;
    }

    printf("\n LISTING OF DATA SET --%s\n\n", dsn);

    if (ds->dsorg == DSORG_PS) {
        ZOS_DCB *dcb = ps_open(dsn, "PRINT", OPEN_INPUT,
                                ds->recfm, ds->lrecl, ds->blksize);
        if (!dcb) return 8;

        char buf[DS_MAX_LRECL + 1];
        int  len, recno = 0;
        while (ps_read(dcb, buf, &len) == DS_OK) {
            buf[len] = '\0';
            printf(" %06d  %.*s\n", ++recno, len, buf);
        }
        ps_close(dcb);
        printf("\n IDC1176I %d RECORDS PROCESSED\n", recno);
    } else if (ds->dsorg == DSORG_VS) {
        ZOS_DCB *dcb = vsam_open(dsn, "PRINT", OPEN_INPUT);
        if (!dcb) return 8;

        char buf[DS_MAX_LRECL];
        ZOS_RPL rpl;
        memset(&rpl, 0, sizeof(rpl));
        rpl.acb     = dcb;
        rpl.area    = (unsigned char *)buf;
        rpl.arealen = sizeof(buf);
        rpl.option  = RPL_OPT_KEY;
        rpl.locate  = RPL_LOC_FIRST;

        int recno = 0;
        while (vsam_get(dcb, &rpl) == 0) {
            printf(" %06d  %-.*s\n", ++recno, rpl.reclen, buf);
            rpl.locate = RPL_LOC_FWD;
        }
        vsam_close(dcb);
        printf("\n IDC1176I %d RECORDS PROCESSED\n", recno);
    } else {
        printf(" (PRINT: unsupported DSORG for %s)\n", dsn);
        return 4;
    }
    return 0;
}

/* =========================================================================
 * IDCAMS COMMAND STREAM RUNNER
 *
 * Processes multi-line IDCAMS SYSIN. Each command may span multiple
 * lines with '-' continuation. Lines starting with /* are end-of-data.
 *
 * Example stream:
 *   DEFINE CLUSTER (NAME(MY.FILE) INDEXED KEYS(8 0) RECORDSIZE(80 80))
 *   LISTCAT ENTRIES(MY.FILE) ALL
 * ========================================================================= */

int idcams_run(const char *command_stream) {
    if (!command_stream) return 0;

    int max_rc = 0;
    char line[2048];
    char cmd[4096] = {0};
    int  cmd_len   = 0;
    const char *p  = command_stream;

    while (*p) {
        /* Read until newline */
        int i = 0;
        while (*p && *p != '\n' && i < (int)sizeof(line) - 1)
            line[i++] = *p++;
        line[i] = '\0';
        if (*p == '\n') p++;

        /* Strip leading spaces */
        char *ln = line;
        while (*ln == ' ' || *ln == '\t') ln++;

        /* End of inline data */
        if (ln[0] == '/' && ln[1] == '*') break;

        /* Comment */
        if (ln[0] == '*') continue;

        /* Continuation: append to cmd buffer */
        int ln_len = (int)strlen(ln);
        bool continued = (ln_len > 0 && ln[ln_len - 1] == '-');
        if (continued) ln[--ln_len] = '\0';

        if (cmd_len + ln_len + 2 < (int)sizeof(cmd)) {
            if (cmd_len > 0) cmd[cmd_len++] = ' ';
            memcpy(cmd + cmd_len, ln, (size_t)ln_len);
            cmd_len += ln_len;
            cmd[cmd_len] = '\0';
        }

        if (continued) continue;  /* More lines coming */

        if (cmd_len == 0) continue;

        /* Dispatch command */
        int rc = 0;
        char upper[4096];
        strncpy(upper, cmd, sizeof(upper) - 1);
        upper[sizeof(upper) - 1] = '\0';
        for (int j = 0; upper[j]; j++)
            upper[j] = (char)toupper((unsigned char)upper[j]);

        if (strncmp(upper, "DEFINE CLUSTER", 14) == 0) {
            rc = idcams_define_cluster(cmd + 14);
        } else if (strncmp(upper, "DEFINE GDG", 10) == 0) {
            rc = idcams_define_gdg(cmd + 10);
        } else if (strncmp(upper, "DELETE", 6) == 0) {
            /* DELETE (dsn) [PURGE] */
            char dsn[DS_DSN_LEN + 1] = {0};
            bool purge = (strstr(upper, "PURGE") != NULL);
            char *start = strchr(cmd, '(');
            if (start) {
                start++;
                char *end = strchr(start, ')');
                if (end) {
                    int dlen = (int)(end - start);
                    if (dlen > DS_DSN_LEN) dlen = DS_DSN_LEN;
                    strncpy(dsn, start, (size_t)dlen);
                    dsn[dlen] = '\0';
                }
            }
            rc = dsn[0] ? idcams_delete(dsn, purge) : 8;
        } else if (strncmp(upper, "LISTCAT", 7) == 0) {
            /* LISTCAT [ENTRIES(filter)] */
            char filter[DS_DSN_LEN + 1] = "*";
            char *ep = strstr(upper, "ENTRIES(");
            if (ep) {
                char *orig_ep = cmd + (ep - upper) + 8;
                char *end = strchr(orig_ep, ')');
                if (end) {
                    int flen = (int)(end - orig_ep);
                    if (flen > DS_DSN_LEN) flen = DS_DSN_LEN;
                    strncpy(filter, orig_ep, (size_t)flen);
                    filter[flen] = '\0';
                }
            }
            rc = idcams_listcat(strcmp(filter, "*") == 0 ? NULL : filter);
        } else if (strncmp(upper, "REPRO", 5) == 0) {
            /* REPRO INDATASET(in) OUTDATASET(out) */
            char indsn[DS_DSN_LEN+1]={0}, outdsn[DS_DSN_LEN+1]={0};
            char *ip = strstr(cmd, "NDATASET(");
            if (!ip) ip = strstr(upper, "NDATASET(");
            char *ip2 = ip ? strchr(ip, '(') : NULL;
            if (ip2) {
                ip2++; char *e = strchr(ip2, ')');
                if (e) { int n = (int)(e-ip2); if(n>DS_DSN_LEN)n=DS_DSN_LEN; strncpy(indsn, ip2, (size_t)n); indsn[n]='\0'; }
            }
            char *op = strstr(upper, "OUTDATASET(");
            char *op2 = op ? (cmd + (op - upper) + 11) : NULL;
            if (op2) { char *e = strchr(op2, ')'); if(e){int n=(int)(e-op2);if(n>DS_DSN_LEN)n=DS_DSN_LEN;strncpy(outdsn,op2,(size_t)n);outdsn[n]='\0';}}
            rc = (indsn[0] && outdsn[0]) ? idcams_repro(indsn, outdsn) : 8;
        } else if (strncmp(upper, "PRINT", 5) == 0) {
            char dsn[DS_DSN_LEN+1]={0};
            char *dp = strstr(upper, "INDATASET(");
            char *dp2 = dp ? (cmd + (dp - upper) + 10) : NULL;
            if (dp2) { char *e=strchr(dp2,')'); if(e){int n=(int)(e-dp2);if(n>DS_DSN_LEN)n=DS_DSN_LEN;strncpy(dsn,dp2,(size_t)n);dsn[n]='\0';}}
            rc = dsn[0] ? idcams_print(dsn) : 8;
        } else if (strncmp(upper, "VERIFY", 6) == 0) {
            printf(" IDC1164I VERIFY PROCESSED\n");  /* No-op in simulator */
            rc = 0;
        } else if (strncmp(upper, "ALTER", 5) == 0) {
            printf(" IDC1203I ALTER NOT FULLY IMPLEMENTED\n");
            rc = 4;
        } else if (cmd_len > 0) {
            printf(" IDC3009I ** UNKNOWN IDCAMS COMMAND: %.32s\n", cmd);
            rc = 8;
        }

        if (rc > max_rc) max_rc = rc;

        /* Reset for next command */
        cmd[0]  = '\0';
        cmd_len = 0;
    }

    printf("\n IDC0002I IDCAMS PROCESSING COMPLETE. MAXIMUM CONDITION CODE WAS %d\n\n",
           max_rc);
    return max_rc;
}
