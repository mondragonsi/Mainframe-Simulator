/*
 * Partitioned Data Set (PDS/PDSE) Implementation
 *
 * A PDS is like a directory of sequential files (members).
 * Each member has a name (1-8 chars) and is itself a sequential dataset.
 *
 * Real-world uses on z/OS:
 *   SYS1.PROCLIB    - Cataloged JCL procedures (PROC libraries)
 *   SYS1.MACLIB     - Assembler macros
 *   USER.COBOL.SRC  - COBOL source programs
 *   USER.LOAD       - Load modules (executable programs)
 *   USER.JCLLIB     - JCL jobs and procedures
 *   USER.COPYLIB    - COBOL copybooks (shared data definitions)
 *
 * Key operations:
 *   FIND  - Locate a member by name (positions the DCB to that member)
 *   STOW  - Add or update a member directory entry
 *   READ  - Read records from the current member
 *   WRITE - Write records to the current member
 *
 * COBOL equivalent (reading a member):
 *   SELECT MY-LIB ASSIGN TO MYLIB
 *     ORGANIZATION IS SEQUENTIAL.
 *   OPEN INPUT MY-LIB.  (after //MYLIB DD DSN=USER.COPYLIB(MYMBR))
 *
 * IBM Reference: DFSMS Using Data Sets, Chapter 12 (BPAM)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "datasets.h"

/* =========================================================================
 * HELPERS
 * ========================================================================= */

static void member_name_upper(char *name) {
    for (int i = 0; name[i]; i++)
        name[i] = (char)toupper((unsigned char)name[i]);
}

/* Lookup member in the dataset's directory */
ZOS_PDS_MEMBER *pds_find_member(ZOS_DATASET *ds, const char *name) {
    if (!ds || !name) return NULL;
    char upper[DS_MEMBER_LEN + 1];
    strncpy(upper, name, DS_MEMBER_LEN);
    upper[DS_MEMBER_LEN] = '\0';
    member_name_upper(upper);

    for (int i = 0; i < ds->member_count; i++) {
        if (strcmp(ds->members[i].name, upper) == 0) {
            return &ds->members[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * PDS OPEN
 *
 * Opens a PDS. The DDname in JCL may include a member:
 *   //MYLIB DD DSN=USER.SRCLIB(MYPROG),DISP=SHR
 * In that case, member is already resolved — pass it as member_name.
 *
 * If no member is specified, the PDS directory is open for browsing
 * or for STOW operations.
 * ========================================================================= */

ZOS_DCB *pds_open(const char *dsn, const char *ddname, ZOS_OPEN_MODE mode) {
    ZOS_DATASET *ds = ds_catalog_find(dsn);

    if (mode == OPEN_OUTPUT && !ds) {
        /* Create new PDS */
        ds = ds_catalog_alloc(dsn, DSORG_PO, RECFM_FB, 80, 800);
        if (!ds) return NULL;
    } else if (!ds) {
        return NULL;   /* DS not found */
    }

    if (ds->dsorg != DSORG_PO) return NULL;  /* Not a PDS */

    ZOS_DCB *dcb = ds_dcb_alloc();
    if (!dcb) return NULL;

    if (ddname) {
        strncpy(dcb->ddname, ddname, DS_DDNAME_LEN);
        dcb->ddname[DS_DDNAME_LEN] = '\0';
    }

    dcb->dataset       = ds;
    dcb->mode          = mode;
    dcb->is_open       = true;
    dcb->recfm         = ds->recfm;
    dcb->lrecl         = ds->lrecl;
    dcb->blksize       = ds->blksize;
    dcb->pds_record_idx = 0;
    dcb->member[0]     = '\0';

    ds->open_count++;
    return dcb;
}

/* =========================================================================
 * FIND
 *
 * Positions the DCB to read from the named member.
 * Equivalent to the FIND macro in assembler BPAM.
 *
 * Return codes:
 *   DS_OK        - Member found, DCB positioned
 *   DS_MEMBER_NF - Member not found (like IEC143I)
 *
 * Educational note: in real z/OS, FIND reads the PDS directory
 * (which is itself a sequential dataset of 256-byte blocks) and
 * stores the TTR (track/record address) into the DCB so the next
 * READ fetches from the correct location on disk.
 * ========================================================================= */

int pds_find(ZOS_DCB *dcb, const char *member) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;

    ZOS_PDS_MEMBER *m = pds_find_member(dcb->dataset, member);
    if (!m) return DS_MEMBER_NF;

    strncpy(dcb->member, m->name, DS_MEMBER_LEN);
    dcb->member[DS_MEMBER_LEN] = '\0';
    dcb->pds_record_idx = m->record_start;
    return DS_OK;
}

/* =========================================================================
 * STOW
 *
 * Adds or updates a member directory entry.
 * Equivalent to the STOW macro in assembler (STOW ADD/REPLACE/DELETE).
 *
 * Creates a new member using currently written records.
 * After STOW, writing to the DCB starts a new member's data.
 *
 * Educational note: in real z/OS, STOW:
 *   1. Calculates the TTR (last record written's address)
 *   2. Updates the PDS directory with name → TTR mapping
 *   3. Compresses the directory if needed (PDS, not PDSE)
 * ========================================================================= */

int pds_stow(ZOS_DCB *dcb, const char *member) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;
    if (!member || !member[0]) return DS_BAD_RECFM;

    ZOS_DATASET *ds = dcb->dataset;
    if (ds->member_count >= DS_MAX_MEMBERS) return -1;

    char upper[DS_MEMBER_LEN + 1];
    strncpy(upper, member, DS_MEMBER_LEN);
    upper[DS_MEMBER_LEN] = '\0';
    member_name_upper(upper);

    /* Check if member already exists — if so, update it */
    ZOS_PDS_MEMBER *existing = pds_find_member(ds, upper);

    /* Count how many records were written since last STOW */
    int start = (int)dcb->writes == 0 ? ds->records.count : 0;
    /* We track start from the current position in records array */
    int cur_rec_count = ds->records.count;

    /* The member's records start where writing began */
    /* We use a simple approach: track start position in dcb */
    int member_start = cur_rec_count - (int)dcb->writes;
    if (member_start < 0) member_start = 0;

    if (existing) {
        /* Update existing member */
        existing->record_start = member_start;
        existing->record_count = (int)dcb->writes;
    } else {
        /* New member */
        ZOS_PDS_MEMBER *m = &ds->members[ds->member_count++];
        memset(m, 0, sizeof(ZOS_PDS_MEMBER));
        strncpy(m->name, upper, DS_MEMBER_LEN);
        m->name[DS_MEMBER_LEN] = '\0';
        m->record_start = member_start;
        m->record_count = (int)dcb->writes;
    }

    /* Reset write counter for next member */
    dcb->writes = 0;

    strncpy(dcb->member, upper, DS_MEMBER_LEN);
    dcb->member[DS_MEMBER_LEN] = '\0';

    (void)start;
    return DS_OK;
}

/* =========================================================================
 * READ
 *
 * Reads the next record from the current member.
 * Member must be positioned via pds_find() first.
 * ========================================================================= */

int pds_read(ZOS_DCB *dcb, void *buffer, int *len) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;
    if (!dcb->member[0]) return DS_MEMBER_NF;

    ZOS_PDS_MEMBER *m = pds_find_member(dcb->dataset, dcb->member);
    if (!m) return DS_MEMBER_NF;

    int local_idx = dcb->pds_record_idx - m->record_start;
    if (local_idx < 0 || local_idx >= m->record_count) {
        return DS_EOF;  /* End of member */
    }

    DS_RECORD_ARRAY *ra = &dcb->dataset->records;
    if (dcb->pds_record_idx >= ra->count) return DS_EOF;

    DS_RECORD *rec = &ra->records[dcb->pds_record_idx++];
    if (buffer) {
        int copy_len = rec->len;
        if (dcb->lrecl > 0 &&
            (dcb->recfm == RECFM_FB || dcb->recfm == RECFM_F)) {
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
 * WRITE
 *
 * Writes a record to the current member being built.
 * Records accumulate until STOW is called to register the member.
 * ========================================================================= */

int pds_write(ZOS_DCB *dcb, const void *buffer, int len) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;
    if (dcb->mode != OPEN_OUTPUT && dcb->mode != OPEN_INOUT)
        return DS_NOT_OPEN;
    if (!buffer || len <= 0) return DS_BAD_RECFM;

    if ((dcb->recfm == RECFM_FB || dcb->recfm == RECFM_F) &&
        dcb->lrecl > 0 && len != dcb->lrecl) {
        char padded[DS_MAX_LRECL];
        memset(padded, ' ', (size_t)dcb->lrecl);
        int copy = len < dcb->lrecl ? len : dcb->lrecl;
        memcpy(padded, buffer, (size_t)copy);
        ds_record_array_add(&dcb->dataset->records, padded, dcb->lrecl);
    } else {
        ds_record_array_add(&dcb->dataset->records, buffer, len);
    }

    dcb->writes++;
    return DS_OK;
}

/* =========================================================================
 * LIST MEMBERS
 *
 * Shows the PDS directory — like TSO LISTDS 'USER.PROCLIB' MEMBERS
 * or ISPF option 3.4.
 * ========================================================================= */

int pds_list_members(ZOS_DCB *dcb) {
    if (!dcb || !dcb->is_open) return DS_NOT_OPEN;

    ZOS_DATASET *ds = dcb->dataset;
    printf("\n MEMBERS OF %s\n", ds->dsn);
    printf(" NAME      RECORDS\n");
    printf(" --------  -------\n");

    for (int i = 0; i < ds->member_count; i++) {
        ZOS_PDS_MEMBER *m = &ds->members[i];
        printf(" %-8s  %7d\n", m->name, m->record_count);
    }

    if (ds->member_count == 0) printf(" (NO MEMBERS)\n");
    printf("\n %d MEMBER(S) IN DIRECTORY\n\n", ds->member_count);
    return DS_OK;
}

/* =========================================================================
 * CLOSE
 * ========================================================================= */

void pds_close(ZOS_DCB *dcb) {
    if (!dcb || !dcb->is_open) return;
    if (dcb->dataset) dcb->dataset->open_count--;
    dcb->is_open = false;
    ds_dcb_free(dcb);
}
