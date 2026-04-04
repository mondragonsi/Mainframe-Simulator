/*
 * Physical Sequential (PS) Dataset Implementation
 *
 * Simulates QSAM (Queued Sequential Access Method) — the standard
 * way COBOL programs access sequential files on z/OS.
 *
 * RECFM=FB (Fixed Blocked): Most common for batch processing.
 *   All records are the same length (LRECL).
 *   Records grouped into blocks (BLKSIZE = multiple of LRECL).
 *   Student analogy: like a fixed-size array written to disk.
 *
 * RECFM=VB (Variable Blocked): Used for variable-length output.
 *   Each record prefixed with 4-byte RDW (Record Descriptor Word).
 *   Each block prefixed with 4-byte BDW (Block Descriptor Word).
 *   Student analogy: like a linked list of variable-length records.
 *
 * COBOL DCB equivalent:
 *   SELECT MY-FILE ASSIGN TO MYFILE
 *     ORGANIZATION IS SEQUENTIAL
 *     ACCESS MODE IS SEQUENTIAL.
 *   FD MY-FILE RECORDING MODE IS F BLOCK CONTAINS 0 RECORDS.
 *
 * IBM Reference: DFSMS Using Data Sets, Chapter 5 (QSAM)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datasets.h"

/* =========================================================================
 * OPEN
 *
 * Equivalent to: OPEN INPUT / OUTPUT / EXTEND in COBOL
 * or the OPEN macro in assembler.
 *
 * Rules:
 *   INPUT:  Dataset must exist. Position at first record.
 *   OUTPUT: Creates or truncates dataset. Position at beginning.
 *   EXTEND: Dataset must exist. Position after last record.
 *   INOUT:  Read/write (BDAM-like, not typical for QSAM).
 * ========================================================================= */

ZOS_DCB *ps_open(const char *dsn, const char *ddname,
                 ZOS_OPEN_MODE mode, ZOS_RECFM recfm, int lrecl, int blksize) {
    /* For OUTPUT, create if not exists */
    ZOS_DATASET *ds = ds_catalog_find(dsn);

    if (mode == OPEN_OUTPUT) {
        if (!ds) {
            ds = ds_catalog_alloc(dsn, DSORG_PS, recfm, lrecl, blksize);
            if (!ds) return NULL;
        } else {
            /* Truncate: discard existing records */
            ds_record_array_free(&ds->records);
            ds_record_array_init(&ds->records);
            /* Update DCB attributes if provided */
            if (recfm != RECFM_UNKNOWN) ds->recfm   = recfm;
            if (lrecl > 0)              ds->lrecl    = lrecl;
            if (blksize > 0)            ds->blksize  = blksize;
        }
    } else {
        /* INPUT / EXTEND: must exist */
        if (!ds) return NULL;
    }

    ZOS_DCB *dcb = ds_dcb_alloc();
    if (!dcb) return NULL;

    if (ddname) {
        strncpy(dcb->ddname, ddname, DS_DDNAME_LEN);
        dcb->ddname[DS_DDNAME_LEN] = '\0';
    }

    dcb->dataset = ds;
    dcb->mode    = mode;
    dcb->is_open = true;
    dcb->recfm   = (recfm != RECFM_UNKNOWN) ? recfm : ds->recfm;
    dcb->lrecl   = (lrecl > 0)              ? lrecl  : ds->lrecl;
    dcb->blksize = (blksize > 0)            ? blksize: ds->blksize;

    /* Position */
    if (mode == OPEN_EXTEND) {
        dcb->ps_record_idx = ds->records.count;  /* After last record */
    } else {
        dcb->ps_record_idx = 0;
    }

    ds->open_count++;
    return dcb;
}

/* =========================================================================
 * READ (GET)
 *
 * Reads the next logical record into buffer.
 *
 * RECFM=FB: Copy LRECL bytes into buffer. len set to LRECL.
 * RECFM=VB: Record has variable length. len set to actual record length.
 *
 * Return codes:
 *   DS_OK       - Record read successfully
 *   DS_EOF (10) - End of file (COBOL: AT END clause triggered)
 *   DS_NOT_OPEN - DCB not open
 *
 * COBOL equivalent:
 *   READ MY-FILE INTO WS-RECORD
 *       AT END MOVE 'Y' TO WS-EOF-FLAG
 *   END-READ.
 * ========================================================================= */

int ps_read(ZOS_DCB *dcb, void *buffer, int *len) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;
    if (dcb->mode != OPEN_INPUT && dcb->mode != OPEN_INOUT) return DS_NOT_OPEN;

    DS_RECORD_ARRAY *ra = &dcb->dataset->records;

    if (dcb->ps_record_idx >= ra->count) {
        return DS_EOF;  /* AT END */
    }

    DS_RECORD *rec = &ra->records[dcb->ps_record_idx++];

    if (buffer) {
        int copy_len = rec->len;
        /* For FB: pad short records to LRECL with spaces */
        if (dcb->recfm == RECFM_FB || dcb->recfm == RECFM_F) {
            if (copy_len > dcb->lrecl) copy_len = dcb->lrecl;
            memcpy(buffer, rec->data, (size_t)copy_len);
            if (copy_len < dcb->lrecl)
                memset((char *)buffer + copy_len, ' ',
                       (size_t)(dcb->lrecl - copy_len));
            if (len) *len = dcb->lrecl;
        } else {
            memcpy(buffer, rec->data, (size_t)copy_len);
            if (len) *len = copy_len;
        }
    } else if (len) {
        *len = rec->len;
    }

    dcb->reads++;
    return DS_OK;
}

/* =========================================================================
 * WRITE (PUT)
 *
 * Writes a logical record from buffer.
 *
 * RECFM=FB: Writes exactly LRECL bytes. Buffer must be LRECL long.
 * RECFM=VB: Writes len bytes. RDW added internally on I/O (not stored).
 *
 * Return codes:
 *   DS_OK       - Record written
 *   DS_NOT_OPEN - DCB not open for output
 *   DS_BAD_RECFM - Record length exceeds LRECL for FB
 *
 * COBOL equivalent:
 *   MOVE WS-DATA TO MY-RECORD.
 *   WRITE MY-RECORD.
 * ========================================================================= */

int ps_write(ZOS_DCB *dcb, const void *buffer, int len) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;
    if (dcb->mode != OPEN_OUTPUT && dcb->mode != OPEN_EXTEND &&
        dcb->mode != OPEN_INOUT) return DS_NOT_OPEN;
    if (!buffer || len <= 0) return DS_BAD_RECFM;

    /* For FB: validate length */
    if ((dcb->recfm == RECFM_FB || dcb->recfm == RECFM_F) &&
        dcb->lrecl > 0 && len != dcb->lrecl) {
        /* Truncate or pad to LRECL */
        char padded[DS_MAX_LRECL];
        memset(padded, ' ', (size_t)dcb->lrecl);
        int copy = len < dcb->lrecl ? len : dcb->lrecl;
        memcpy(padded, buffer, (size_t)copy);
        if (ds_record_array_add(&dcb->dataset->records, padded, dcb->lrecl) != 0)
            return -1;
    } else {
        if (ds_record_array_add(&dcb->dataset->records, buffer, len) != 0)
            return -1;
    }

    dcb->writes++;
    dcb->ps_record_idx = dcb->dataset->records.count;
    return DS_OK;
}

/* =========================================================================
 * POINT
 *
 * Positions to a specific record number (0-based).
 * Used for BSAM random access (BSP, POINT macros).
 * In QSAM this is less common but used for restart scenarios.
 * ========================================================================= */

int ps_point(ZOS_DCB *dcb, int record_num) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;
    if (record_num < 0 || record_num > dcb->dataset->records.count)
        return DS_NOT_FOUND;
    dcb->ps_record_idx = record_num;
    return DS_OK;
}

/* =========================================================================
 * CLOSE
 *
 * Closes the DCB. In real z/OS, CLOSE flushes buffers, updates
 * the DSCB on VTOC (end-of-extent pointer, record count, etc.).
 *
 * COBOL equivalent:
 *   CLOSE MY-FILE.
 * ========================================================================= */

void ps_close(ZOS_DCB *dcb) {
    if (!dcb || !dcb->is_open) return;
    if (dcb->dataset) {
        dcb->dataset->open_count--;
        /* Update dataset LRECL/RECFM from DCB if this was output */
        if (dcb->mode == OPEN_OUTPUT || dcb->mode == OPEN_EXTEND) {
            if (dcb->recfm   != RECFM_UNKNOWN) dcb->dataset->recfm   = dcb->recfm;
            if (dcb->lrecl   > 0)              dcb->dataset->lrecl   = dcb->lrecl;
            if (dcb->blksize > 0)              dcb->dataset->blksize = dcb->blksize;
        }
    }
    dcb->is_open = false;
    ds_dcb_free(dcb);
}
