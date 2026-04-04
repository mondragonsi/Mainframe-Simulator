/*
 * VSAM (Virtual Storage Access Method) Implementation
 *
 * VSAM is z/OS's high-performance indexed access method. IMS uses VSAM
 * internally for its access methods (HISAM=KSDS, HIDAM=KSDS, etc.).
 *
 * Three cluster types:
 *
 *   KSDS (Key-Sequenced Data Set):
 *     Records stored in key order. Both sequential and direct (by key)
 *     access. The most common VSAM type. IMS HIDAM uses KSDS.
 *     Analogy: like a sorted array with binary search.
 *
 *   ESDS (Entry-Sequenced Data Set):
 *     Records in insertion order, addressed by RBA (Relative Byte Address).
 *     Cannot delete; can only add at end. Used by IMS HISAM overflow.
 *     Analogy: like an append-only log.
 *
 *   RRDS (Relative-Record Data Set):
 *     Fixed-length records addressed by slot number (RRN).
 *     Slots can be empty. Like an array with sparse slots.
 *     Analogy: like an array where index = record number.
 *
 * The RPL (Request Parameter List) is the control block that drives
 * all VSAM I/O — similar to DCB for QSAM but with more options.
 *
 * IBM Reference: DFSMS Using Data Sets, Part 3 (VSAM)
 *                DFSMS Macro Instructions, Chapter 5
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datasets.h"

/* =========================================================================
 * VSAM KEY COMPARISON
 * ========================================================================= */

int vsam_key_cmp(const unsigned char *a, const unsigned char *b, int len) {
    return memcmp(a, b, (size_t)len);
}

/* =========================================================================
 * RETURN CODE DESCRIPTIONS (for student education)
 * ========================================================================= */

const char *vsam_rtncd_desc(int rtncd, int fdbk) {
    if (rtncd == 0)                        return "Successful";
    if (rtncd == 8 && fdbk == VSAM_FDBK_DUPKEY)  return "Duplicate key";
    if (rtncd == 8 && fdbk == VSAM_FDBK_NOTFOUND) return "Record not found";
    if (rtncd == 8 && fdbk == VSAM_FDBK_EOF)      return "End of dataset";
    if (rtncd == 8 && fdbk == VSAM_FDBK_NOSLOT)   return "RRN out of range";
    if (rtncd == 12)                       return "Physical I/O error";
    return "Unknown VSAM error";
}

/* =========================================================================
 * DEFINE KSDS
 *
 * Equivalent to IDCAMS:
 *   DEFINE CLUSTER (NAME(MY.VSAM.FILE) -
 *                   INDEXED                  -
 *                   KEYS(8 0)                -
 *                   RECORDSIZE(80 80)        -
 *                   TRACKS(1 1))             -
 *          DATA    (NAME(MY.VSAM.FILE.DATA)) -
 *          INDEX   (NAME(MY.VSAM.FILE.INDEX))
 * ========================================================================= */

ZOS_DATASET *vsam_define_ksds(const char *dsn, int key_offset, int key_len,
                               int avg_len, int max_len) {
    if (ds_catalog_find(dsn)) return NULL;  /* Already exists */

    ZOS_DATASET *ds = ds_catalog_alloc(dsn, DSORG_VS,
                                        RECFM_UNKNOWN, avg_len, 0);
    if (!ds) return NULL;

    ds->vsam_type   = VSAM_KSDS;
    ds->key_offset  = key_offset;
    ds->key_len     = key_len;
    ds->avg_rec_len = avg_len;
    ds->max_rec_len = max_len > 0 ? max_len : avg_len;
    ds->lrecl       = avg_len;

    /* Pre-allocate record array */
    ds->vsam_capacity = 64;
    ds->vsam_records = (ZOS_VSAM_RECORD *)calloc((size_t)ds->vsam_capacity,
                                                   sizeof(ZOS_VSAM_RECORD));
    ds->vsam_count = 0;
    return ds;
}

/* =========================================================================
 * DEFINE ESDS
 * ========================================================================= */

ZOS_DATASET *vsam_define_esds(const char *dsn, int avg_len, int max_len) {
    if (ds_catalog_find(dsn)) return NULL;

    ZOS_DATASET *ds = ds_catalog_alloc(dsn, DSORG_VS,
                                        RECFM_UNKNOWN, avg_len, 0);
    if (!ds) return NULL;

    ds->vsam_type   = VSAM_ESDS;
    ds->avg_rec_len = avg_len;
    ds->max_rec_len = max_len > 0 ? max_len : avg_len;
    ds->lrecl       = avg_len;
    ds->next_rba    = 0;

    ds->vsam_capacity = 64;
    ds->vsam_records = (ZOS_VSAM_RECORD *)calloc((size_t)ds->vsam_capacity,
                                                   sizeof(ZOS_VSAM_RECORD));
    ds->vsam_count = 0;
    return ds;
}

/* =========================================================================
 * DEFINE RRDS
 * ========================================================================= */

ZOS_DATASET *vsam_define_rrds(const char *dsn, int rec_len, int slots) {
    if (ds_catalog_find(dsn)) return NULL;

    ZOS_DATASET *ds = ds_catalog_alloc(dsn, DSORG_VS,
                                        RECFM_F, rec_len, 0);
    if (!ds) return NULL;

    ds->vsam_type   = VSAM_RRDS;
    ds->avg_rec_len = rec_len;
    ds->max_rec_len = rec_len;
    ds->lrecl       = rec_len;

    int cap = slots > 0 ? slots : 64;
    ds->vsam_capacity = cap;
    ds->vsam_records = (ZOS_VSAM_RECORD *)calloc((size_t)cap,
                                                   sizeof(ZOS_VSAM_RECORD));
    /* Pre-fill slots as empty */
    for (int i = 0; i < cap; i++) {
        ds->vsam_records[i].rrn     = i + 1;
        ds->vsam_records[i].deleted = true;  /* Empty slot */
    }
    ds->vsam_count = cap;
    return ds;
}

/* =========================================================================
 * OPEN / CLOSE
 * ========================================================================= */

ZOS_DCB *vsam_open(const char *dsn, const char *ddname, ZOS_OPEN_MODE mode) {
    ZOS_DATASET *ds = ds_catalog_find(dsn);
    if (!ds || ds->dsorg != DSORG_VS) return NULL;

    ZOS_DCB *dcb = ds_dcb_alloc();
    if (!dcb) return NULL;

    if (ddname) {
        strncpy(dcb->ddname, ddname, DS_DDNAME_LEN);
        dcb->ddname[DS_DDNAME_LEN] = '\0';
    }

    dcb->dataset          = ds;
    dcb->mode             = mode;
    dcb->is_open          = true;
    dcb->vsam_pos         = 0;
    dcb->vsam_positioned  = false;
    dcb->lrecl            = ds->avg_rec_len;

    ds->open_count++;
    return dcb;
}

void vsam_close(ZOS_DCB *dcb) {
    if (!dcb || !dcb->is_open) return;
    if (dcb->dataset) dcb->dataset->open_count--;
    dcb->is_open = false;
    ds_dcb_free(dcb);
}

/* =========================================================================
 * INTERNAL: GROW VSAM RECORD ARRAY
 * ========================================================================= */

static int vsam_grow(ZOS_DATASET *ds) {
    int newcap = ds->vsam_capacity * 2;
    ZOS_VSAM_RECORD *newrecs = (ZOS_VSAM_RECORD *)realloc(ds->vsam_records,
                                 (size_t)newcap * sizeof(ZOS_VSAM_RECORD));
    if (!newrecs) return -1;
    memset(newrecs + ds->vsam_capacity, 0,
           (size_t)(newcap - ds->vsam_capacity) * sizeof(ZOS_VSAM_RECORD));
    ds->vsam_records  = newrecs;
    ds->vsam_capacity = newcap;
    return 0;
}

/* =========================================================================
 * KSDS: BINARY SEARCH BY KEY
 * ========================================================================= */

static int ksds_find_key(ZOS_DATASET *ds, const unsigned char *key,
                          int key_len, bool *exact) {
    int lo = 0, hi = ds->vsam_count - 1, mid = 0;
    *exact = false;

    while (lo <= hi) {
        mid = (lo + hi) / 2;
        /* Skip logically deleted records */
        if (ds->vsam_records[mid].deleted) { lo = mid + 1; continue; }

        int cmp = vsam_key_cmp(ds->vsam_records[mid].key, key, key_len);
        if (cmp == 0) { *exact = true; return mid; }
        else if (cmp < 0) lo = mid + 1;
        else              hi = mid - 1;
    }
    return lo;  /* Insertion point */
}

/* =========================================================================
 * VSAM GET
 *
 * RPL options:
 *   KEY + LOC_KEY  → direct access by key (KSDS/RRDS)
 *   KEY + LOC_FWD  → sequential forward from current position
 *   ADR + LOC_KEY  → direct access by RBA (ESDS)
 *   RRN + LOC_KEY  → direct access by slot number (RRDS)
 *
 * Return code in RPL.rtncd:
 *   0  = OK
 *   8  = not found / EOF (fdbk distinguishes)
 *   12 = physical error
 *
 * IBM VSAM GET macro equivalent:
 *   GET RPL=MYRPL
 *   LTR R15,R15
 *   BNZ NOTFOUND
 * ========================================================================= */

int vsam_get(ZOS_DCB *dcb, ZOS_RPL *rpl) {
    if (!dcb || !dcb->is_open || !rpl) return VSAM_NOTOPEN;

    ZOS_DATASET *ds = dcb->dataset;
    rpl->rtncd = 0;
    rpl->fdbk  = 0;

    /* ---- KSDS ---- */
    if (ds->vsam_type == VSAM_KSDS) {
        if (rpl->locate == RPL_LOC_KEY || rpl->locate == RPL_LOC_FIRST) {
            /* Direct by key */
            if (rpl->locate == RPL_LOC_FIRST) {
                dcb->vsam_pos = 0;
                dcb->vsam_positioned = true;
            } else {
                if (!rpl->arg || rpl->arglen <= 0) {
                    rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOTFOUND; return 8;
                }
                bool exact;
                int pos = ksds_find_key(ds, rpl->arg, rpl->arglen, &exact);
                if (!exact) {
                    rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOTFOUND; return 8;
                }
                dcb->vsam_pos = pos;
                dcb->vsam_positioned = true;
            }
        } else {
            /* Sequential forward */
            if (!dcb->vsam_positioned) dcb->vsam_pos = 0;
        }

        /* Skip deleted */
        while (dcb->vsam_pos < ds->vsam_count &&
               ds->vsam_records[dcb->vsam_pos].deleted) {
            dcb->vsam_pos++;
        }
        if (dcb->vsam_pos >= ds->vsam_count) {
            rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_EOF; return 8;
        }

        ZOS_VSAM_RECORD *rec = &ds->vsam_records[dcb->vsam_pos];
        if (rpl->area && rpl->arealen >= rec->len) {
            memcpy(rpl->area, rec->data, (size_t)rec->len);
            rpl->reclen = rec->len;
        }
        dcb->vsam_positioned = true;
        dcb->vsam_pos++;   /* always advance; ERASE uses vsam_pos-1 */

        return 0;
    }

    /* ---- ESDS ---- */
    if (ds->vsam_type == VSAM_ESDS) {
        if (rpl->option == RPL_OPT_ADR && rpl->arg) {
            /* Direct by RBA */
            long rba = 0;
            memcpy(&rba, rpl->arg, sizeof(long) < (size_t)rpl->arglen ?
                                   sizeof(long) : (size_t)rpl->arglen);
            for (int i = 0; i < ds->vsam_count; i++) {
                if (ds->vsam_records[i].rba == rba) {
                    if (rpl->area && rpl->arealen >= ds->vsam_records[i].len) {
                        memcpy(rpl->area, ds->vsam_records[i].data,
                               (size_t)ds->vsam_records[i].len);
                        rpl->reclen = ds->vsam_records[i].len;
                    }
                    dcb->vsam_pos = i + 1;
                    return 0;
                }
            }
            rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOTFOUND; return 8;
        } else {
            /* Sequential */
            if (!dcb->vsam_positioned) dcb->vsam_pos = 0;
            if (dcb->vsam_pos >= ds->vsam_count) {
                rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_EOF; return 8;
            }
            ZOS_VSAM_RECORD *rec = &ds->vsam_records[dcb->vsam_pos++];
            if (rpl->area && rpl->arealen >= rec->len) {
                memcpy(rpl->area, rec->data, (size_t)rec->len);
                rpl->reclen = rec->len;
            }
            dcb->vsam_positioned = true;
            return 0;
        }
    }

    /* ---- RRDS ---- */
    if (ds->vsam_type == VSAM_RRDS) {
        int rrn = 0;
        if (rpl->option == RPL_OPT_RRN && rpl->arg) {
            memcpy(&rrn, rpl->arg, sizeof(int));
        } else {
            rrn = dcb->vsam_pos + 1;
        }
        if (rrn < 1 || rrn > ds->vsam_count) {
            rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOSLOT; return 8;
        }
        ZOS_VSAM_RECORD *rec = &ds->vsam_records[rrn - 1];
        if (rec->deleted) {
            rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOTFOUND; return 8;
        }
        if (rpl->area && rpl->arealen >= rec->len) {
            memcpy(rpl->area, rec->data, (size_t)rec->len);
            rpl->reclen = rec->len;
        }
        dcb->vsam_pos = rrn;  /* Point to next slot */
        return 0;
    }

    rpl->rtncd = 12; return 12;
}

/* =========================================================================
 * VSAM PUT (INSERT or UPDATE)
 *
 * For KSDS:
 *   INSERT (new key): inserts in sorted position. Duplicate → RTNCD=8/FDBK=8
 *   UPDATE (held record): replaces current position record.
 *
 * For ESDS:
 *   Always appends. No key required.
 *
 * For RRDS:
 *   PUT at specified RRN (slot number).
 *
 * IBM VSAM PUT macro:
 *   PUT RPL=MYRPL
 * ========================================================================= */

int vsam_put(ZOS_DCB *dcb, ZOS_RPL *rpl) {
    if (!dcb || !dcb->is_open || !rpl) return VSAM_NOTOPEN;
    if (dcb->mode != OPEN_OUTPUT && dcb->mode != OPEN_INOUT) return VSAM_NOTOPEN;

    ZOS_DATASET *ds = dcb->dataset;
    rpl->rtncd = 0;
    rpl->fdbk  = 0;

    if (!rpl->area || rpl->arealen <= 0) { rpl->rtncd = 12; return 12; }

    /* ---- KSDS ---- */
    if (ds->vsam_type == VSAM_KSDS) {
        if (ds->key_len <= 0 || ds->key_offset + ds->key_len > rpl->arealen) {
            rpl->rtncd = 12; return 12;
        }

        /* Extract key from record */
        unsigned char key[DS_MAX_KEY_LEN];
        memcpy(key, (unsigned char *)rpl->area + ds->key_offset,
               (size_t)ds->key_len);

        bool exact;
        int pos = ksds_find_key(ds, key, ds->key_len, &exact);

        if (exact) {
            /* Duplicate key */
            rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_DUPKEY; return 8;
        }

        /* Need space for one more record */
        if (ds->vsam_count >= ds->vsam_capacity) {
            if (vsam_grow(ds) != 0) { rpl->rtncd = 12; return 12; }
        }

        /* Shift records right to make room at pos */
        if (pos < ds->vsam_count) {
            memmove(&ds->vsam_records[pos + 1], &ds->vsam_records[pos],
                    (size_t)(ds->vsam_count - pos) * sizeof(ZOS_VSAM_RECORD));
        }

        ZOS_VSAM_RECORD *rec = &ds->vsam_records[pos];
        memset(rec, 0, sizeof(ZOS_VSAM_RECORD));
        rec->data = (unsigned char *)malloc((size_t)rpl->arealen);
        if (!rec->data) { rpl->rtncd = 12; return 12; }
        memcpy(rec->data, rpl->area, (size_t)rpl->arealen);
        rec->len = rpl->arealen;
        memcpy(rec->key, key, (size_t)ds->key_len);
        rec->key_len = ds->key_len;
        rec->deleted = false;

        ds->vsam_count++;
        dcb->vsam_pos = pos + 1;
        return 0;
    }

    /* ---- ESDS ---- */
    if (ds->vsam_type == VSAM_ESDS) {
        if (ds->vsam_count >= ds->vsam_capacity) {
            if (vsam_grow(ds) != 0) { rpl->rtncd = 12; return 12; }
        }

        ZOS_VSAM_RECORD *rec = &ds->vsam_records[ds->vsam_count];
        memset(rec, 0, sizeof(ZOS_VSAM_RECORD));
        rec->data = (unsigned char *)malloc((size_t)rpl->arealen);
        if (!rec->data) { rpl->rtncd = 12; return 12; }
        memcpy(rec->data, rpl->area, (size_t)rpl->arealen);
        rec->len     = rpl->arealen;
        rec->rba     = ds->next_rba;
        rec->deleted = false;

        ds->next_rba += rpl->arealen;
        ds->vsam_count++;
        return 0;
    }

    /* ---- RRDS ---- */
    if (ds->vsam_type == VSAM_RRDS) {
        int rrn = 0;
        if (rpl->option == RPL_OPT_RRN && rpl->arg) {
            memcpy(&rrn, rpl->arg, sizeof(int));
        } else {
            /* Find first empty slot */
            for (int i = 0; i < ds->vsam_count; i++) {
                if (ds->vsam_records[i].deleted) { rrn = i + 1; break; }
            }
            if (!rrn) { rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOSLOT; return 8; }
        }
        if (rrn < 1 || rrn > ds->vsam_count) {
            rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOSLOT; return 8;
        }

        ZOS_VSAM_RECORD *rec = &ds->vsam_records[rrn - 1];
        free(rec->data);
        rec->data = (unsigned char *)malloc((size_t)rpl->arealen);
        if (!rec->data) { rpl->rtncd = 12; return 12; }
        memcpy(rec->data, rpl->area, (size_t)rpl->arealen);
        rec->len     = rpl->arealen;
        rec->rrn     = rrn;
        rec->deleted = false;
        return 0;
    }

    rpl->rtncd = 12; return 12;
}

/* =========================================================================
 * VSAM ERASE (logical delete)
 *
 * Marks a record as deleted. KSDS uses logical delete (space reused
 * by REORG). RRDS marks the slot as empty.
 *
 * Rule: Must have previously done a GET for UPDATE (held record).
 * In our simulator: ERASE at current position.
 *
 * IBM VSAM ERASE macro:
 *   ERASE RPL=MYRPL
 * ========================================================================= */

int vsam_erase(ZOS_DCB *dcb, ZOS_RPL *rpl) {
    if (!dcb || !dcb->is_open || !rpl) return VSAM_NOTOPEN;

    ZOS_DATASET *ds = dcb->dataset;
    rpl->rtncd = 0; rpl->fdbk = 0;

    int pos = dcb->vsam_pos - 1;  /* Current = one before vsam_pos */
    if (pos < 0 || pos >= ds->vsam_count) {
        rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOTFOUND; return 8;
    }

    ds->vsam_records[pos].deleted = true;
    return 0;
}

/* =========================================================================
 * VSAM POINT
 *
 * Positions the dataset for subsequent sequential GET.
 *   POINT by KEY: position before the record with that key.
 *   POINT FIRST:  position before the first record.
 *   POINT LAST:   position before the last record.
 *
 * IBM VSAM POINT macro:
 *   POINT RPL=MYRPL
 * ========================================================================= */

int vsam_point(ZOS_DCB *dcb, ZOS_RPL *rpl) {
    if (!dcb || !dcb->is_open || !rpl) return VSAM_NOTOPEN;

    ZOS_DATASET *ds = dcb->dataset;
    rpl->rtncd = 0; rpl->fdbk = 0;

    if (rpl->locate == RPL_LOC_FIRST) {
        dcb->vsam_pos = 0;
        dcb->vsam_positioned = true;
        return 0;
    }

    if (rpl->locate == RPL_LOC_LAST) {
        dcb->vsam_pos = ds->vsam_count > 0 ? ds->vsam_count - 1 : 0;
        dcb->vsam_positioned = true;
        return 0;
    }

    if (ds->vsam_type == VSAM_KSDS && rpl->arg && rpl->arglen > 0) {
        bool exact;
        int pos = ksds_find_key(ds, rpl->arg, rpl->arglen, &exact);
        dcb->vsam_pos = pos;
        dcb->vsam_positioned = true;
        if (!exact) {
            /* Position to next record >= key (generic equal) */
        }
        return 0;
    }

    rpl->rtncd = 8; rpl->fdbk = VSAM_FDBK_NOTFOUND; return 8;
}
