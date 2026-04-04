/*
 * GDG (Generation Data Group) Implementation
 *
 * A GDG is a collection of sequentially related datasets called generations.
 * Widely used in batch processing for backup/archive cycles.
 *
 * Example:
 *   MY.PAYROLL.BACKUP.G0001V00  ← oldest generation
 *   MY.PAYROLL.BACKUP.G0002V00
 *   MY.PAYROLL.BACKUP.G0003V00  ← current generation (G0003V00 = (0))
 *
 * Relative generation notation in JCL:
 *   MY.PAYROLL.BACKUP(0)   → current (G0003V00)
 *   MY.PAYROLL.BACKUP(-1)  → previous (G0002V00)
 *   MY.PAYROLL.BACKUP(-2)  → two back (G0001V00)
 *   MY.PAYROLL.BACKUP(+1)  → new generation (G0004V00, created by job)
 *
 * IDCAMS DEFINE GDG:
 *   DEFINE GDG (NAME(MY.PAYROLL.BACKUP) -
 *               LIMIT(7)                -
 *               NOEMPTY                 -
 *               SCRATCH)
 *
 * When LIMIT is reached:
 *   EMPTY:   all old generations uncataloged when limit exceeded
 *   NOEMPTY: only the oldest is rolled off (most common)
 *   SCRATCH: rolled-off generations deleted from VTOC
 *
 * IBM Reference: DFSMS Using Data Sets, Chapter 17 (GDG)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datasets.h"

/* =========================================================================
 * HELPERS
 * ========================================================================= */

/* Format absolute generation number into GDG name suffix.
 * gen=1 → "G0001V00", gen=99 → "G0099V00" */
static void gdg_gen_suffix(int gen, char *buf, int buflen) {
    snprintf(buf, (size_t)buflen, "G%04dV00", gen);
}

/* Build full generation DSN from base + gen number */
static void gdg_gen_dsn(const char *base, int gen, char *dsn, int dsn_len) {
    char suffix[12];
    gdg_gen_suffix(gen, suffix, sizeof(suffix));
    snprintf(dsn, (size_t)dsn_len, "%s.%s", base, suffix);
}

/* =========================================================================
 * DEFINE GDG BASE
 *
 * Creates the GDG base entry in the catalog.
 * The base is not a dataset itself — it's a catalog entry that acts
 * as the "parent" of all generations.
 * ========================================================================= */

ZOS_DATASET *gdg_define_base(const char *base_dsn, int limit,
                              bool empty, bool scratch) {
    if (ds_catalog_find(base_dsn)) return NULL;  /* Already exists */

    ZOS_DATASET *base = ds_catalog_alloc(base_dsn, DSORG_GDG,
                                          RECFM_UNKNOWN, 0, 0);
    if (!base) return NULL;

    base->gdg_limit    = (limit > 0 && limit <= DS_MAX_GDG_GENS) ? limit : 7;
    base->gdg_empty    = empty;
    base->gdg_scratch  = scratch;
    base->gdg_gen_count = 0;

    return base;
}

/* =========================================================================
 * NEW GENERATION (+1)
 *
 * Allocates the next generation number and registers it.
 * Called when JCL contains DISP=(NEW,CATLG) for a GDG relative reference.
 *
 * Returns the new absolute generation number, or -1 on error.
 * ========================================================================= */

int gdg_new_gen(const char *base_dsn) {
    ZOS_DATASET *base = ds_catalog_find(base_dsn);
    if (!base || base->dsorg != DSORG_GDG) return -1;

    /* Determine next generation number */
    int next_gen = 1;
    for (int i = 0; i < base->gdg_gen_count; i++) {
        if (base->gdg_gens[i].gen_number >= next_gen) {
            next_gen = base->gdg_gens[i].gen_number + 1;
        }
    }

    /* Check limit */
    int active_count = 0;
    for (int i = 0; i < base->gdg_gen_count; i++) {
        if (base->gdg_gens[i].is_active && base->gdg_gens[i].is_cataloged)
            active_count++;
    }

    if (active_count >= base->gdg_limit) {
        if (gdg_rolloff(base) != DS_OK) return -1;
    }

    /* Register new generation */
    if (base->gdg_gen_count >= DS_MAX_GDG_GENS) return -1;

    char gen_dsn[DS_DSN_LEN + 1];
    gdg_gen_dsn(base_dsn, next_gen, gen_dsn, sizeof(gen_dsn));

    ZOS_GDG_GEN *g = &base->gdg_gens[base->gdg_gen_count++];
    g->gen_number   = next_gen;
    g->is_active    = true;
    g->is_cataloged = true;
    strncpy(g->dsn, gen_dsn, DS_DSN_LEN);
    g->dsn[DS_DSN_LEN] = '\0';

    /* Allocate the actual generation dataset as PS FB */
    ZOS_DATASET *gen_ds = ds_catalog_alloc(gen_dsn, DSORG_PS,
                                            RECFM_FB, 80, 800);
    if (!gen_ds) {
        base->gdg_gen_count--;
        return -1;
    }
    g->dataset_idx = zos_catalog.count - 1;

    return next_gen;
}

/* =========================================================================
 * RESOLVE RELATIVE GENERATION
 *
 * Converts a relative reference (0, -1, -2, +1) to the absolute DSN.
 *   (0)  = most recent cataloged generation
 *   (-1) = previous generation
 *   (+1) = next generation (must be NEW, calls gdg_new_gen)
 *
 * Returns a pointer to the resolved ZOS_DATASET, or NULL if not found.
 * ========================================================================= */

ZOS_DATASET *gdg_resolve(const char *base_dsn, int relative) {
    ZOS_DATASET *base = ds_catalog_find(base_dsn);
    if (!base || base->dsorg != DSORG_GDG) return NULL;

    if (relative > 0) {
        /* New generation */
        int gen = gdg_new_gen(base_dsn);
        if (gen < 0) return NULL;
        /* Re-find base (catalog may have shifted) */
        base = ds_catalog_find(base_dsn);
        if (!base) return NULL;
        /* Return the last gen entry's dataset */
        ZOS_GDG_GEN *g = &base->gdg_gens[base->gdg_gen_count - 1];
        return ds_catalog_find(g->dsn);
    }

    /* Build list of active generation numbers in ascending order */
    int gen_nums[DS_MAX_GDG_GENS];
    int gen_count = 0;
    for (int i = 0; i < base->gdg_gen_count; i++) {
        if (base->gdg_gens[i].is_active && base->gdg_gens[i].is_cataloged) {
            gen_nums[gen_count++] = base->gdg_gens[i].gen_number;
        }
    }

    if (gen_count == 0) return NULL;

    /* Sort ascending (simple insertion sort — count is small) */
    for (int i = 1; i < gen_count; i++) {
        int key = gen_nums[i], j = i - 1;
        while (j >= 0 && gen_nums[j] > key) { gen_nums[j+1] = gen_nums[j]; j--; }
        gen_nums[j+1] = key;
    }

    /* (0) = last element, (-1) = second-to-last, etc. */
    int idx = gen_count - 1 + relative;  /* relative is 0 or negative */
    if (idx < 0 || idx >= gen_count) return NULL;

    int target_gen = gen_nums[idx];

    /* Find the generation entry */
    for (int i = 0; i < base->gdg_gen_count; i++) {
        if (base->gdg_gens[i].gen_number == target_gen) {
            return ds_catalog_find(base->gdg_gens[i].dsn);
        }
    }
    return NULL;
}

/* =========================================================================
 * ROLLOFF
 *
 * When LIMIT is exceeded, rolls off the oldest generation.
 * NOEMPTY: uncatalog oldest one generation.
 * EMPTY:   uncatalog ALL generations.
 * SCRATCH: also delete the dataset from the catalog.
 * ========================================================================= */

int gdg_rolloff(ZOS_DATASET *base) {
    if (!base || base->dsorg != DSORG_GDG) return DS_NOT_FOUND;

    if (base->gdg_empty) {
        /* EMPTY: uncatalog all */
        for (int i = 0; i < base->gdg_gen_count; i++) {
            base->gdg_gens[i].is_cataloged = false;
            if (base->gdg_scratch) {
                ds_catalog_delete(base->gdg_gens[i].dsn);
                base->gdg_gens[i].is_active = false;
            }
        }
    } else {
        /* NOEMPTY: find and remove the oldest cataloged generation */
        int oldest_gen = -1, oldest_idx = -1;
        for (int i = 0; i < base->gdg_gen_count; i++) {
            if (base->gdg_gens[i].is_active && base->gdg_gens[i].is_cataloged) {
                if (oldest_gen < 0 ||
                    base->gdg_gens[i].gen_number < oldest_gen) {
                    oldest_gen = base->gdg_gens[i].gen_number;
                    oldest_idx = i;
                }
            }
        }
        if (oldest_idx >= 0) {
            base->gdg_gens[oldest_idx].is_cataloged = false;
            if (base->gdg_scratch) {
                ds_catalog_delete(base->gdg_gens[oldest_idx].dsn);
                base->gdg_gens[oldest_idx].is_active = false;
            }
        }
    }
    return DS_OK;
}

/* =========================================================================
 * GDG LISTCAT
 * ========================================================================= */

void gdg_listcat_base(const char *base_dsn) {
    ZOS_DATASET *base = ds_catalog_find(base_dsn);
    if (!base || base->dsorg != DSORG_GDG) {
        printf(" IDC3012I ENTRY %s NOT FOUND\n", base_dsn);
        return;
    }

    printf("\n GDG ------- %s\n", base->dsn);
    printf("    LIMIT---%d   EMPTY---%s   SCRATCH---%s\n",
           base->gdg_limit,
           base->gdg_empty   ? "YES" : "NO",
           base->gdg_scratch ? "YES" : "NO");
    printf("    GENERATIONS:\n");
    printf("    %-44s  GEN  ACTIVE  CATALOGED\n", "DSN");
    printf("    %s  ---  ------  ---------\n",
           "--------------------------------------------");

    int shown = 0;
    for (int i = 0; i < base->gdg_gen_count; i++) {
        ZOS_GDG_GEN *g = &base->gdg_gens[i];
        printf("    %-44s  %04d  %-6s  %-9s\n",
               g->dsn, g->gen_number,
               g->is_active    ? "YES" : "NO",
               g->is_cataloged ? "YES" : "NO");
        shown++;
    }
    if (shown == 0) printf("    (NO GENERATIONS)\n");
    printf("\n");
}
