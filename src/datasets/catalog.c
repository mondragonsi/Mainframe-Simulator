/*
 * z/OS ICF Catalog Implementation
 *
 * The Integrated Catalog Facility (ICF) is the z/OS dataset registry.
 * Every allocated dataset — PS, PDS, VSAM, GDG — has a catalog entry.
 * The catalog is queried by OPEN, JCL allocation, and the LISTCAT command.
 *
 * Educational note:
 *   Real z/OS has two catalog types:
 *     - Master Catalog (MCAT): system datasets, points to user catalogs
 *     - User Catalogs (UCAT): owned by applications/projects
 *   Each VSAM cluster has: ICF catalog entry + VVDS on the volume
 *   Non-VSAM datasets: cataloged via DISP=CATLG in JCL
 *
 * This simulator uses a single flat in-memory catalog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "datasets.h"

/* Global catalog */
ZOS_CATALOG zos_catalog;

/* DCB pool */
static ZOS_DCB dcb_pool[DS_MAX_OPEN];
static bool    dcb_pool_used[DS_MAX_OPEN];

/* =========================================================================
 * UTILITIES
 * ========================================================================= */

const char *ds_dsorg_name(ZOS_DSORG dsorg) {
    switch (dsorg) {
        case DSORG_PS:  return "PS ";
        case DSORG_PO:  return "PO ";
        case DSORG_VS:  return "VS ";
        case DSORG_GDG: return "GDG";
        default:        return "???";
    }
}

const char *ds_recfm_name(ZOS_RECFM recfm) {
    switch (recfm) {
        case RECFM_F:  return "F  ";
        case RECFM_FB: return "FB ";
        case RECFM_V:  return "V  ";
        case RECFM_VB: return "VB ";
        case RECFM_U:  return "U  ";
        default:       return "???";
    }
}

ZOS_RECFM ds_recfm_parse(const char *s) {
    if (!s) return RECFM_UNKNOWN;
    if (strcmp(s, "FB") == 0) return RECFM_FB;
    if (strcmp(s, "F")  == 0) return RECFM_F;
    if (strcmp(s, "VB") == 0) return RECFM_VB;
    if (strcmp(s, "V")  == 0) return RECFM_V;
    if (strcmp(s, "U")  == 0) return RECFM_U;
    return RECFM_UNKNOWN;
}

/* Uppercase DSN in place */
static void dsn_upper(char *dsn) {
    for (int i = 0; dsn[i]; i++)
        dsn[i] = (char)toupper((unsigned char)dsn[i]);
}

/* =========================================================================
 * RECORD ARRAY HELPERS
 * ========================================================================= */

void ds_record_array_init(DS_RECORD_ARRAY *ra) {
    ra->records  = NULL;
    ra->count    = 0;
    ra->capacity = 0;
}

void ds_record_array_free(DS_RECORD_ARRAY *ra) {
    for (int i = 0; i < ra->count; i++) {
        free(ra->records[i].data);
        ra->records[i].data = NULL;
    }
    free(ra->records);
    ra->records  = NULL;
    ra->count    = 0;
    ra->capacity = 0;
}

int ds_record_array_add(DS_RECORD_ARRAY *ra, const void *data, int len) {
    if (ra->count >= ra->capacity) {
        int newcap = ra->capacity == 0 ? 64 : ra->capacity * 2;
        DS_RECORD *newrecs = (DS_RECORD *)realloc(ra->records,
                                                   newcap * sizeof(DS_RECORD));
        if (!newrecs) return -1;
        ra->records  = newrecs;
        ra->capacity = newcap;
    }
    ra->records[ra->count].data = (unsigned char *)malloc((size_t)len);
    if (!ra->records[ra->count].data) return -1;
    memcpy(ra->records[ra->count].data, data, (size_t)len);
    ra->records[ra->count].len = len;
    ra->count++;
    return 0;
}

/* =========================================================================
 * RDW ENCODE / DECODE
 *
 * Variable-length records have a 4-byte Record Descriptor Word (RDW):
 *   Bytes 0-1: record length INCLUDING the 4 RDW bytes (big-endian)
 *   Bytes 2-3: segment flags (00 00 for complete records)
 * ========================================================================= */

void ds_rdw_encode(ZOS_RDW *rdw, int data_len) {
    int total = data_len + 4;   /* data + RDW itself */
    rdw->length   = (uint16_t)((total >> 8) | ((total & 0xFF) << 8)); /* big-endian */
    rdw->flags    = 0x00;
    rdw->reserved = 0x00;
}

int ds_rdw_decode(const ZOS_RDW *rdw) {
    /* Big-endian to host */
    int total = ((rdw->length >> 8) & 0xFF) | ((rdw->length & 0xFF) << 8);
    return total - 4;  /* data length without RDW */
}

/* =========================================================================
 * DCB POOL
 * ========================================================================= */

void ds_dcb_pool_init(void) {
    memset(dcb_pool,      0, sizeof(dcb_pool));
    memset(dcb_pool_used, 0, sizeof(dcb_pool_used));
}

ZOS_DCB *ds_dcb_alloc(void) {
    for (int i = 0; i < DS_MAX_OPEN; i++) {
        if (!dcb_pool_used[i]) {
            dcb_pool_used[i] = true;
            memset(&dcb_pool[i], 0, sizeof(ZOS_DCB));
            return &dcb_pool[i];
        }
    }
    return NULL;  /* IEC143I OPEN ERROR - TOO MANY OPEN DATASETS */
}

void ds_dcb_free(ZOS_DCB *dcb) {
    if (!dcb) return;
    for (int i = 0; i < DS_MAX_OPEN; i++) {
        if (&dcb_pool[i] == dcb) {
            dcb_pool_used[i] = false;
            memset(dcb, 0, sizeof(ZOS_DCB));
            return;
        }
    }
}

/* =========================================================================
 * CATALOG INIT
 * ========================================================================= */

void ds_catalog_init(const char *catalog_name) {
    memset(&zos_catalog, 0, sizeof(ZOS_CATALOG));
    ds_dcb_pool_init();

    if (catalog_name) {
        strncpy(zos_catalog.catalog_name, catalog_name, DS_DSN_LEN);
    } else {
        strncpy(zos_catalog.catalog_name, "ICFCAT.MASTERCAT", DS_DSN_LEN);
    }
    zos_catalog.catalog_name[DS_DSN_LEN] = '\0';
}

/* =========================================================================
 * CATALOG FIND
 * ========================================================================= */

ZOS_DATASET *ds_catalog_find(const char *dsn) {
    if (!dsn) return NULL;
    char upper[DS_DSN_LEN + 1];
    strncpy(upper, dsn, DS_DSN_LEN);
    upper[DS_DSN_LEN] = '\0';
    dsn_upper(upper);

    for (int i = 0; i < zos_catalog.count; i++) {
        if (strcmp(zos_catalog.datasets[i].dsn, upper) == 0 &&
            zos_catalog.datasets[i].exists) {
            return &zos_catalog.datasets[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * CATALOG ALLOC
 * ========================================================================= */

ZOS_DATASET *ds_catalog_alloc(const char *dsn, ZOS_DSORG dsorg,
                               ZOS_RECFM recfm, int lrecl, int blksize) {
    if (!dsn || zos_catalog.count >= DS_MAX_DATASETS) return NULL;

    /* Don't duplicate */
    if (ds_catalog_find(dsn)) return NULL;

    ZOS_DATASET *ds = &zos_catalog.datasets[zos_catalog.count++];
    memset(ds, 0, sizeof(ZOS_DATASET));

    strncpy(ds->dsn, dsn, DS_DSN_LEN);
    ds->dsn[DS_DSN_LEN] = '\0';
    dsn_upper(ds->dsn);

    strncpy(ds->volser, "SIMVOL", DS_VOLSER_LEN);
    ds->volser[DS_VOLSER_LEN] = '\0';

    ds->dsorg       = dsorg;
    ds->recfm       = recfm;
    ds->lrecl       = lrecl;
    ds->blksize     = blksize > 0 ? blksize : lrecl * 10;
    ds->exists      = true;
    ds->is_cataloged= true;

    ds_record_array_init(&ds->records);

    return ds;
}

/* =========================================================================
 * CATALOG DELETE
 * ========================================================================= */

int ds_catalog_delete(const char *dsn) {
    ZOS_DATASET *ds = ds_catalog_find(dsn);
    if (!ds) return DS_NOT_FOUND;
    if (ds->open_count > 0) return DS_NOT_OPEN; /* Can't delete while open */

    ds_record_array_free(&ds->records);

    /* Free VSAM records */
    if (ds->vsam_records) {
        for (int i = 0; i < ds->vsam_count; i++) {
            free(ds->vsam_records[i].data);
        }
        free(ds->vsam_records);
        ds->vsam_records = NULL;
    }

    ds->exists = false;
    ds->is_cataloged = false;
    return DS_OK;
}

/* =========================================================================
 * LISTCAT
 *
 * Simulates the IDCAMS LISTCAT command output.
 * IBM LISTCAT output format (simplified):
 *
 *  LISTING FROM CATALOG -- ICFCAT.MASTERCAT
 *
 *  NONVSAM ------- SYS1.PROCLIB
 *    IN-CAT --- ICFCAT.MASTERCAT
 *    HISTORY
 *      DATASET-OWNER -------- (NONE)
 *    DATA SET ATTRIBUTES
 *      DSORG---------PO     RECFM--------FB
 *      LRECL---------080    BLKSIZE------800
 * ========================================================================= */

void ds_catalog_listcat(const char *filter) {
    printf("\n");
    printf(" LISTING FROM CATALOG -- %s\n", zos_catalog.catalog_name);
    printf("\n");

    int shown = 0;
    for (int i = 0; i < zos_catalog.count; i++) {
        ZOS_DATASET *ds = &zos_catalog.datasets[i];
        if (!ds->exists) continue;

        /* Apply filter (simple prefix match) */
        if (filter && filter[0] && filter[0] != '*') {
            if (strncmp(ds->dsn, filter, strlen(filter)) != 0) continue;
        }

        const char *type = (ds->dsorg == DSORG_VS) ? "CLUSTER" : "NONVSAM";

        printf(" %s ------- %s\n", type, ds->dsn);
        printf("    IN-CAT --- %s\n", zos_catalog.catalog_name);
        printf("    DATA SET ATTRIBUTES\n");
        printf("      DSORG------%-3s     RECFM------%-3s\n",
               ds_dsorg_name(ds->dsorg), ds_recfm_name(ds->recfm));

        if (ds->dsorg != DSORG_GDG) {
            printf("      LRECL------%-5d   BLKSIZE----%-5d\n",
                   ds->lrecl, ds->blksize);
        }

        if (ds->dsorg == DSORG_VS) {
            const char *vtype = ds->vsam_type == VSAM_KSDS ? "KSDS" :
                                ds->vsam_type == VSAM_ESDS ? "ESDS" :
                                ds->vsam_type == VSAM_RRDS ? "RRDS" : "LDS ";
            printf("      VSAM-TYPE--%-4s    KEYLEN-----%-3d\n",
                   vtype, ds->key_len);
            printf("      KEYOFF-----%-3d     RECORDS----%-5d\n",
                   ds->key_offset, ds->vsam_count);
        }

        if (ds->dsorg == DSORG_PO) {
            printf("      MEMBERS----%-3d\n", ds->member_count);
        }

        if (ds->dsorg == DSORG_GDG) {
            printf("      LIMIT------%-3d     GENERATIONS %d\n",
                   ds->gdg_limit, ds->gdg_gen_count);
        }

        printf("\n");
        shown++;
    }

    if (shown == 0) {
        printf(" IDC3012I ENTRY %s NOT FOUND\n",
               filter ? filter : "*");
    }

    printf(" IDC0002I IDCAMS PROCESSING COMPLETE. MAXIMUM CONDITION CODE WAS 0\n\n");
}
