/*
 * Dataset Simulator Test Suite
 *
 * Tests all dataset types: PS (FB/VB), PDS, VSAM (KSDS/ESDS/RRDS), GDG.
 * Also tests IDCAMS command processing.
 *
 * Test framework: simple assert macros, each test returns 0=pass 1=fail.
 * Run: ./test_datasets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/datasets/datasets.h"

/* =========================================================================
 * TEST FRAMEWORK
 * ========================================================================= */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL at line %d: expected %d, got %d\n", __LINE__, (int)(b), (int)(a)); \
        return 1; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL at line %d: expected '%s', got '%s'\n", __LINE__, (b), (a)); \
        return 1; \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) do { \
    if (!(p)) { \
        printf("  FAIL at line %d: unexpected NULL\n", __LINE__); \
        return 1; \
    } \
} while(0)

#define ASSERT_NULL(p) do { \
    if ((p)) { \
        printf("  FAIL at line %d: expected NULL\n", __LINE__); \
        return 1; \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, n) do { \
    if (memcmp((a), (b), (n)) != 0) { \
        printf("  FAIL at line %d: memory mismatch\n", __LINE__); \
        return 1; \
    } \
} while(0)

static void run_test(const char *name, int (*fn)(void)) {
    tests_run++;
    printf(" %-55s ", name);
    fflush(stdout);
    int rc = fn();
    if (rc == 0) { tests_passed++; printf("PASS\n"); }
    else          { tests_failed++; printf("FAIL\n"); }
}

/* Reset catalog between tests */
static void reset_catalog(void) {
    ds_catalog_init("ICFCAT.TESTCAT");
}

/* =========================================================================
 * PS TESTS
 * ========================================================================= */

/* Write 10 FB records, read them back, verify content */
static int test_ps_fb_write_read(void) {
    reset_catalog();

    ZOS_DCB *out = ps_open("USER.TEST.FB", "OUTDD", OPEN_OUTPUT,
                           RECFM_FB, 80, 800);
    ASSERT_NOT_NULL(out);

    char rec[80];
    for (int i = 0; i < 10; i++) {
        memset(rec, ' ', 80);
        snprintf(rec, 80, "RECORD-%04d", i);
        rec[strlen(rec)] = ' ';  /* Don't null-terminate in the record */
        ASSERT_EQ(ps_write(out, rec, 80), DS_OK);
    }
    ps_close(out);

    /* Verify record count */
    ZOS_DATASET *ds = ds_catalog_find("USER.TEST.FB");
    ASSERT_NOT_NULL(ds);
    ASSERT_EQ(ds->records.count, 10);

    /* Read back */
    ZOS_DCB *in = ps_open("USER.TEST.FB", "INDD", OPEN_INPUT,
                          RECFM_FB, 80, 800);
    ASSERT_NOT_NULL(in);

    char buf[80];
    int  len;
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(ps_read(in, buf, &len), DS_OK);
        ASSERT_EQ(len, 80);
        char expected[12];
        snprintf(expected, sizeof(expected), "RECORD-%04d", i);
        ASSERT_EQ(memcmp(buf, expected, strlen(expected)), 0);
    }
    /* 11th read = EOF */
    ASSERT_EQ(ps_read(in, buf, &len), DS_EOF);
    ps_close(in);
    return 0;
}

/* Variable-length records: verify RDW encoding/decoding */
static int test_ps_vb_rdw(void) {
    ZOS_RDW rdw;

    /* Encode 76 bytes of data → total RDW = 80 */
    ds_rdw_encode(&rdw, 76);
    int decoded = ds_rdw_decode(&rdw);
    ASSERT_EQ(decoded, 76);

    /* Encode 0 → total 4 */
    ds_rdw_encode(&rdw, 0);
    ASSERT_EQ(ds_rdw_decode(&rdw), 0);

    /* Encode 32756 */
    ds_rdw_encode(&rdw, 32756);
    ASSERT_EQ(ds_rdw_decode(&rdw), 32756);

    return 0;
}

/* VB write/read with different record lengths */
static int test_ps_vb_write_read(void) {
    reset_catalog();

    ZOS_DCB *out = ps_open("USER.TEST.VB", "OUTVB", OPEN_OUTPUT,
                           RECFM_VB, 255, 0);
    ASSERT_NOT_NULL(out);

    int lengths[] = {10, 50, 100, 3, 200};
    char buf[256];
    for (int i = 0; i < 5; i++) {
        memset(buf, 'A' + i, lengths[i]);
        ASSERT_EQ(ps_write(out, buf, lengths[i]), DS_OK);
    }
    ps_close(out);

    ZOS_DCB *in = ps_open("USER.TEST.VB", "INVB", OPEN_INPUT,
                          RECFM_VB, 255, 0);
    ASSERT_NOT_NULL(in);

    char rbuf[256];
    int  rlen;
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(ps_read(in, rbuf, &rlen), DS_OK);
        ASSERT_EQ(rlen, lengths[i]);
        /* Verify content */
        char expected[256];
        memset(expected, 'A' + i, lengths[i]);
        ASSERT_MEM_EQ(rbuf, expected, lengths[i]);
    }
    ASSERT_EQ(ps_read(in, rbuf, &rlen), DS_EOF);
    ps_close(in);
    return 0;
}

/* PS POINT: position to specific record */
static int test_ps_point(void) {
    reset_catalog();

    ZOS_DCB *out = ps_open("USER.POINT.TEST", "OUTDD", OPEN_OUTPUT,
                           RECFM_FB, 10, 100);
    ASSERT_NOT_NULL(out);
    char rec[10];
    for (int i = 0; i < 5; i++) {
        memset(rec, '0' + i, 10);
        ps_write(out, rec, 10);
    }
    ps_close(out);

    ZOS_DCB *in = ps_open("USER.POINT.TEST", "INDD", OPEN_INPUT,
                          RECFM_FB, 10, 100);
    ASSERT_NOT_NULL(in);

    /* Point to record 3 (0-based) */
    ASSERT_EQ(ps_point(in, 3), DS_OK);
    char buf[10];
    int len;
    ASSERT_EQ(ps_read(in, buf, &len), DS_OK);
    /* Should be the 4th record: filled with '3' */
    ASSERT_EQ(buf[0], '3');
    ps_close(in);
    return 0;
}

/* PS EXTEND: append to existing dataset */
static int test_ps_extend(void) {
    reset_catalog();

    /* Write 3 records */
    ZOS_DCB *out = ps_open("USER.EXT.TEST", "OUT1", OPEN_OUTPUT,
                           RECFM_FB, 10, 100);
    ASSERT_NOT_NULL(out);
    char r[10];
    memset(r, 'A', 10); ps_write(out, r, 10);
    memset(r, 'B', 10); ps_write(out, r, 10);
    memset(r, 'C', 10); ps_write(out, r, 10);
    ps_close(out);

    /* Extend: append 2 more */
    ZOS_DCB *ext = ps_open("USER.EXT.TEST", "OUT2", OPEN_EXTEND,
                           RECFM_FB, 10, 100);
    ASSERT_NOT_NULL(ext);
    memset(r, 'D', 10); ps_write(ext, r, 10);
    memset(r, 'E', 10); ps_write(ext, r, 10);
    ps_close(ext);

    ZOS_DATASET *ds = ds_catalog_find("USER.EXT.TEST");
    ASSERT_NOT_NULL(ds);
    ASSERT_EQ(ds->records.count, 5);
    return 0;
}

/* =========================================================================
 * PDS TESTS
 * ========================================================================= */

/* Create PDS, write members, STOW, FIND, read back */
static int test_pds_member_stow_find(void) {
    reset_catalog();

    /* Create PDS */
    ds_catalog_alloc("USER.SRCLIB", DSORG_PO, RECFM_FB, 80, 800);

    ZOS_DCB *dcb = pds_open("USER.SRCLIB", "SRCLIB", OPEN_OUTPUT);
    ASSERT_NOT_NULL(dcb);

    /* Write member MYPROG */
    const char *lines[] = {
        "       IDENTIFICATION DIVISION.                                      ",
        "       PROGRAM-ID. MYPROG.                                           ",
        "       PROCEDURE DIVISION.                                           ",
        "           STOP RUN.                                                 "
    };
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(pds_write(dcb, lines[i], 80), DS_OK);
    }
    ASSERT_EQ(pds_stow(dcb, "MYPROG"), DS_OK);

    /* Write member COPY01 */
    const char *copy_lines[] = {
        "       01  WS-HOSP-REC.                                              ",
        "           05 WS-HOSPCODE  PIC X(4).                                 "
    };
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(pds_write(dcb, copy_lines[i], 80), DS_OK);
    }
    ASSERT_EQ(pds_stow(dcb, "COPY01"), DS_OK);
    pds_close(dcb);

    /* Verify directory */
    ZOS_DATASET *ds = ds_catalog_find("USER.SRCLIB");
    ASSERT_NOT_NULL(ds);
    ASSERT_EQ(ds->member_count, 2);

    /* FIND MYPROG and read */
    ZOS_DCB *rdcb = pds_open("USER.SRCLIB", "SRCLIB", OPEN_INPUT);
    ASSERT_NOT_NULL(rdcb);
    ASSERT_EQ(pds_find(rdcb, "MYPROG"), DS_OK);

    char buf[80];
    int len;
    int read_count = 0;
    while (pds_read(rdcb, buf, &len) == DS_OK) read_count++;
    ASSERT_EQ(read_count, 4);

    /* FIND non-existent member */
    ASSERT_EQ(pds_find(rdcb, "NOEXIST"), DS_MEMBER_NF);
    pds_close(rdcb);

    return 0;
}

/* PDS: list members */
static int test_pds_list_members(void) {
    reset_catalog();

    ds_catalog_alloc("USER.PROCLIB", DSORG_PO, RECFM_FB, 80, 800);
    ZOS_DCB *dcb = pds_open("USER.PROCLIB", "PRC", OPEN_OUTPUT);
    ASSERT_NOT_NULL(dcb);

    char line[80];
    memset(line, ' ', 80);
    pds_write(dcb, line, 80);
    pds_stow(dcb, "PROC01");
    pds_write(dcb, line, 80);
    pds_stow(dcb, "PROC02");
    pds_write(dcb, line, 80);
    pds_stow(dcb, "PROC03");
    /* list */
    ASSERT_EQ(pds_list_members(dcb), DS_OK);
    pds_close(dcb);

    ZOS_DATASET *ds = ds_catalog_find("USER.PROCLIB");
    ASSERT_EQ(ds->member_count, 3);
    return 0;
}

/* =========================================================================
 * VSAM KSDS TESTS
 * ========================================================================= */

/* Insert by key, GET by key, verify data */
static int test_vsam_ksds_insert_get(void) {
    reset_catalog();

    ZOS_DATASET *ds = vsam_define_ksds("USER.HOSP.KSDS", 0, 4, 80, 80);
    ASSERT_NOT_NULL(ds);

    ZOS_DCB *dcb = vsam_open("USER.HOSP.KSDS", "HOSPDD", OPEN_OUTPUT);
    ASSERT_NOT_NULL(dcb);

    /* Insert 3 records with 4-byte keys */
    struct { char key[4]; char data[76]; } recs[] = {
        {"H002", "HOSPITAL GERAL                                              "},
        {"H001", "HOSPITAL SANTA CASA                                         "},
        {"H003", "HOSPITAL SAO LUCAS                                          "},
    };

    ZOS_RPL rpl;
    char buf[80];
    for (int i = 0; i < 3; i++) {
        memset(&rpl, 0, sizeof(rpl));
        memset(buf, ' ', 80);
        memcpy(buf, recs[i].key, 4);
        memcpy(buf + 4, recs[i].data, 76);
        rpl.acb     = dcb;
        rpl.area    = (unsigned char *)buf;
        rpl.arealen = 80;
        rpl.option  = RPL_OPT_KEY;
        ASSERT_EQ(vsam_put(dcb, &rpl), VSAM_OK);
    }
    vsam_close(dcb);

    /* KSDS should store in key order: H001, H002, H003 */
    ASSERT_EQ(ds->vsam_count, 3);
    ASSERT_EQ(memcmp(ds->vsam_records[0].key, "H001", 4), 0);
    ASSERT_EQ(memcmp(ds->vsam_records[1].key, "H002", 4), 0);
    ASSERT_EQ(memcmp(ds->vsam_records[2].key, "H003", 4), 0);

    /* GET by key H002 */
    ZOS_DCB *in = vsam_open("USER.HOSP.KSDS", "HOSPDD", OPEN_INPUT);
    ASSERT_NOT_NULL(in);

    char getbuf[80];
    memset(&rpl, 0, sizeof(rpl));
    rpl.acb     = in;
    rpl.area    = (unsigned char *)getbuf;
    rpl.arealen = 80;
    rpl.option  = RPL_OPT_KEY;
    rpl.locate  = RPL_LOC_KEY;
    rpl.arg     = (unsigned char *)"H002";
    rpl.arglen  = 4;

    ASSERT_EQ(vsam_get(in, &rpl), VSAM_OK);
    ASSERT_EQ(memcmp(getbuf, "H002", 4), 0);
    ASSERT_EQ(rpl.reclen, 80);

    vsam_close(in);
    return 0;
}

/* Duplicate key insert → RTNCD=8, FDBK=8 */
static int test_vsam_ksds_duplicate_key(void) {
    reset_catalog();

    vsam_define_ksds("USER.DUP.KSDS", 0, 4, 20, 20);
    ZOS_DCB *dcb = vsam_open("USER.DUP.KSDS", "DUPDD", OPEN_OUTPUT);
    ASSERT_NOT_NULL(dcb);

    char buf[20];
    ZOS_RPL rpl;
    memset(&rpl, 0, sizeof(rpl));
    memset(buf, ' ', 20);
    memcpy(buf, "KEY1", 4);
    rpl.acb = dcb; rpl.area = (unsigned char *)buf;
    rpl.arealen = 20; rpl.option = RPL_OPT_KEY;

    ASSERT_EQ(vsam_put(dcb, &rpl), VSAM_OK);   /* First insert OK */
    ASSERT_EQ(vsam_put(dcb, &rpl), 8);          /* Duplicate → RTNCD=8 */
    ASSERT_EQ(rpl.fdbk, VSAM_FDBK_DUPKEY);

    vsam_close(dcb);
    return 0;
}

/* Sequential GET through all KSDS records */
static int test_vsam_ksds_sequential(void) {
    reset_catalog();

    vsam_define_ksds("USER.SEQ.KSDS", 0, 4, 10, 10);
    ZOS_DCB *out = vsam_open("USER.SEQ.KSDS", "SEQDD", OPEN_OUTPUT);
    ASSERT_NOT_NULL(out);

    char buf[10];
    ZOS_RPL rpl;
    const char *keys[] = {"KEY5", "KEY2", "KEY8", "KEY1", "KEY3"};
    for (int i = 0; i < 5; i++) {
        memset(&rpl, 0, sizeof(rpl));
        memset(buf, ' ', 10);
        memcpy(buf, keys[i], 4);
        rpl.acb = out; rpl.area = (unsigned char *)buf;
        rpl.arealen = 10; rpl.option = RPL_OPT_KEY;
        ASSERT_EQ(vsam_put(out, &rpl), VSAM_OK);
    }
    vsam_close(out);

    /* Sequential read → should come back in key order */
    ZOS_DCB *in = vsam_open("USER.SEQ.KSDS", "SEQDD", OPEN_INPUT);
    ASSERT_NOT_NULL(in);

    const char *expected_order[] = {"KEY1", "KEY2", "KEY3", "KEY5", "KEY8"};
    char rbuf[10];
    for (int i = 0; i < 5; i++) {
        memset(&rpl, 0, sizeof(rpl));
        rpl.acb = in; rpl.area = (unsigned char *)rbuf;
        rpl.arealen = 10; rpl.option = RPL_OPT_KEY;
        rpl.locate = (i == 0) ? RPL_LOC_FIRST : RPL_LOC_FWD;
        ASSERT_EQ(vsam_get(in, &rpl), VSAM_OK);
        ASSERT_EQ(memcmp(rbuf, expected_order[i], 4), 0);
    }
    /* 6th GET → EOF */
    memset(&rpl, 0, sizeof(rpl));
    rpl.acb = in; rpl.area = (unsigned char *)rbuf;
    rpl.arealen = 10; rpl.option = RPL_OPT_KEY; rpl.locate = RPL_LOC_FWD;
    ASSERT_EQ(vsam_get(in, &rpl), 8);
    ASSERT_EQ(rpl.fdbk, VSAM_FDBK_EOF);

    vsam_close(in);
    return 0;
}

/* ESDS: append and sequential read by insertion order */
static int test_vsam_esds_append_read(void) {
    reset_catalog();

    vsam_define_esds("USER.LOG.ESDS", 50, 50);
    ZOS_DCB *out = vsam_open("USER.LOG.ESDS", "LOGDD", OPEN_OUTPUT);
    ASSERT_NOT_NULL(out);

    char buf[50];
    ZOS_RPL rpl;
    for (int i = 0; i < 5; i++) {
        memset(&rpl, 0, sizeof(rpl));
        snprintf(buf, 50, "LOG-ENTRY-%04d                                    ", i);
        rpl.acb = out; rpl.area = (unsigned char *)buf;
        rpl.arealen = 50; rpl.option = RPL_OPT_ADR;
        ASSERT_EQ(vsam_put(out, &rpl), VSAM_OK);
    }
    vsam_close(out);

    ZOS_DATASET *ds = ds_catalog_find("USER.LOG.ESDS");
    ASSERT_EQ(ds->vsam_count, 5);

    /* RBA check: each record is 50 bytes, so RBAs are 0, 50, 100, 150, 200 */
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(ds->vsam_records[i].rba, i * 50);
    }

    /* Sequential read */
    ZOS_DCB *in = vsam_open("USER.LOG.ESDS", "LOGDD", OPEN_INPUT);
    char rbuf[50];
    for (int i = 0; i < 5; i++) {
        memset(&rpl, 0, sizeof(rpl));
        rpl.acb = in; rpl.area = (unsigned char *)rbuf;
        rpl.arealen = 50; rpl.option = RPL_OPT_ADR;
        ASSERT_EQ(vsam_get(in, &rpl), VSAM_OK);
        char expected[14];
        snprintf(expected, sizeof(expected), "LOG-ENTRY-%04d", i);
        ASSERT_EQ(memcmp(rbuf, expected, strlen(expected)), 0);
    }
    vsam_close(in);
    return 0;
}

/* RRDS: write to specific slot, read by RRN */
static int test_vsam_rrds_slots(void) {
    reset_catalog();

    vsam_define_rrds("USER.SLOT.RRDS", 20, 10);
    ZOS_DCB *dcb = vsam_open("USER.SLOT.RRDS", "SLOTDD", OPEN_OUTPUT);
    ASSERT_NOT_NULL(dcb);

    char buf[20];
    ZOS_RPL rpl;
    int rrns[] = {3, 7, 1};

    for (int i = 0; i < 3; i++) {
        memset(&rpl, 0, sizeof(rpl));
        snprintf(buf, 20, "SLOT-%04d          ", rrns[i]);
        int rrn = rrns[i];
        rpl.acb = dcb; rpl.area = (unsigned char *)buf;
        rpl.arealen = 20; rpl.option = RPL_OPT_RRN;
        rpl.arg = (unsigned char *)&rrn; rpl.arglen = sizeof(int);
        ASSERT_EQ(vsam_put(dcb, &rpl), VSAM_OK);
    }
    vsam_close(dcb);

    /* Read slot 7 */
    ZOS_DCB *in = vsam_open("USER.SLOT.RRDS", "SLOTDD", OPEN_INPUT);
    char rbuf[20];
    memset(&rpl, 0, sizeof(rpl));
    int rrn = 7;
    rpl.acb = in; rpl.area = (unsigned char *)rbuf;
    rpl.arealen = 20; rpl.option = RPL_OPT_RRN;
    rpl.arg = (unsigned char *)&rrn; rpl.arglen = sizeof(int);
    ASSERT_EQ(vsam_get(in, &rpl), VSAM_OK);
    ASSERT_EQ(memcmp(rbuf, "SLOT-0007", 9), 0);

    /* Empty slot */
    rrn = 5;
    ASSERT_EQ(vsam_get(in, &rpl), 8);

    vsam_close(in);
    return 0;
}

/* VSAM ERASE */
static int test_vsam_ksds_erase(void) {
    reset_catalog();

    vsam_define_ksds("USER.ERASE.KSDS", 0, 4, 10, 10);
    ZOS_DCB *dcb = vsam_open("USER.ERASE.KSDS", "ERDD", OPEN_INOUT);
    ASSERT_NOT_NULL(dcb);

    char buf[10]; ZOS_RPL rpl;
    const char *keys[] = {"AA01", "AA02", "AA03"};
    for (int i = 0; i < 3; i++) {
        memset(&rpl, 0, sizeof(rpl));
        memset(buf, ' ', 10); memcpy(buf, keys[i], 4);
        rpl.acb = dcb; rpl.area = (unsigned char *)buf;
        rpl.arealen = 10; rpl.option = RPL_OPT_KEY;
        vsam_put(dcb, &rpl);
    }

    /* GET AA02 then ERASE it */
    char rbuf[10];
    memset(&rpl, 0, sizeof(rpl));
    rpl.acb = dcb; rpl.area = (unsigned char *)rbuf;
    rpl.arealen = 10; rpl.option = RPL_OPT_KEY;
    rpl.locate = RPL_LOC_KEY;
    rpl.arg = (unsigned char *)"AA02"; rpl.arglen = 4;
    ASSERT_EQ(vsam_get(dcb, &rpl), VSAM_OK);
    ASSERT_EQ(vsam_erase(dcb, &rpl), VSAM_OK);

    /* Verify AA02 is logically deleted */
    ZOS_DATASET *ds = ds_catalog_find("USER.ERASE.KSDS");
    bool found_deleted = false;
    for (int i = 0; i < ds->vsam_count; i++) {
        if (memcmp(ds->vsam_records[i].key, "AA02", 4) == 0 &&
            ds->vsam_records[i].deleted) {
            found_deleted = true; break;
        }
    }
    ASSERT_EQ(found_deleted, true);
    vsam_close(dcb);
    return 0;
}

/* =========================================================================
 * GDG TESTS
 * ========================================================================= */

/* Define base, create 3 generations, resolve (0) and (-1) */
static int test_gdg_relative_gen(void) {
    reset_catalog();

    ZOS_DATASET *base = gdg_define_base("MY.BACKUP", 5, false, true);
    ASSERT_NOT_NULL(base);

    /* Create 3 generations */
    int g1 = gdg_new_gen("MY.BACKUP");
    int g2 = gdg_new_gen("MY.BACKUP");
    int g3 = gdg_new_gen("MY.BACKUP");
    ASSERT_EQ(g1, 1);
    ASSERT_EQ(g2, 2);
    ASSERT_EQ(g3, 3);

    /* Re-fetch base (catalog may realloc) */
    base = ds_catalog_find("MY.BACKUP");
    ASSERT_NOT_NULL(base);
    ASSERT_EQ(base->gdg_gen_count, 3);

    /* Resolve (0) = current = G0003V00 */
    ZOS_DATASET *cur = gdg_resolve("MY.BACKUP", 0);
    ASSERT_NOT_NULL(cur);
    ASSERT_EQ(strstr(cur->dsn, "G0003V00") != NULL, 1);

    /* Resolve (-1) = G0002V00 */
    ZOS_DATASET *prev = gdg_resolve("MY.BACKUP", -1);
    ASSERT_NOT_NULL(prev);
    ASSERT_EQ(strstr(prev->dsn, "G0002V00") != NULL, 1);

    /* Resolve (-2) = G0001V00 */
    ZOS_DATASET *prev2 = gdg_resolve("MY.BACKUP", -2);
    ASSERT_NOT_NULL(prev2);
    ASSERT_EQ(strstr(prev2->dsn, "G0001V00") != NULL, 1);

    /* Resolve (-3) = doesn't exist */
    ASSERT_NULL(gdg_resolve("MY.BACKUP", -3));

    return 0;
}

/* GDG rolloff: limit=3, add 4th → oldest removed */
static int test_gdg_rolloff(void) {
    reset_catalog();

    gdg_define_base("MY.ROLLING", 3, false, false);

    gdg_new_gen("MY.ROLLING");  /* G0001 */
    gdg_new_gen("MY.ROLLING");  /* G0002 */
    gdg_new_gen("MY.ROLLING");  /* G0003 */
    gdg_new_gen("MY.ROLLING");  /* G0004 — should trigger rolloff of G0001 */

    ZOS_DATASET *base = ds_catalog_find("MY.ROLLING");
    ASSERT_NOT_NULL(base);

    /* G0001 should be uncataloged */
    bool g1_uncataloged = false;
    for (int i = 0; i < base->gdg_gen_count; i++) {
        if (base->gdg_gens[i].gen_number == 1 &&
            !base->gdg_gens[i].is_cataloged) {
            g1_uncataloged = true; break;
        }
    }
    ASSERT_EQ(g1_uncataloged, true);

    /* (0) should resolve to G0004 */
    ZOS_DATASET *cur = gdg_resolve("MY.ROLLING", 0);
    ASSERT_NOT_NULL(cur);
    ASSERT_EQ(strstr(cur->dsn, "G0004V00") != NULL, 1);

    return 0;
}

/* =========================================================================
 * IDCAMS TESTS
 * ========================================================================= */

static int test_idcams_define_listcat_delete(void) {
    reset_catalog();

    /* DEFINE CLUSTER */
    int rc = idcams_define_cluster(
        "(NAME(IDCAMS.TEST.KSDS) INDEXED KEYS(8 0) RECORDSIZE(100 200))");
    ASSERT_EQ(rc, 0);

    ZOS_DATASET *ds = ds_catalog_find("IDCAMS.TEST.KSDS");
    ASSERT_NOT_NULL(ds);
    ASSERT_EQ(ds->vsam_type, VSAM_KSDS);
    ASSERT_EQ(ds->key_len, 8);

    /* LISTCAT */
    rc = idcams_listcat("IDCAMS.TEST");
    ASSERT_EQ(rc, 0);

    /* DELETE */
    rc = idcams_delete("IDCAMS.TEST.KSDS", false);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(ds_catalog_find("IDCAMS.TEST.KSDS"));

    return 0;
}

static int test_idcams_repro_ps_to_ps(void) {
    reset_catalog();

    /* Create source */
    ZOS_DCB *src = ps_open("REPRO.SOURCE", "SRC", OPEN_OUTPUT,
                           RECFM_FB, 20, 200);
    ASSERT_NOT_NULL(src);
    char rec[20];
    for (int i = 0; i < 5; i++) {
        snprintf(rec, 20, "ITEM-%04d           ", i);
        ps_write(src, rec, 20);
    }
    ps_close(src);

    /* REPRO to target */
    ds_catalog_alloc("REPRO.TARGET", DSORG_PS, RECFM_FB, 20, 200);
    int rc = idcams_repro("REPRO.SOURCE", "REPRO.TARGET");
    ASSERT_EQ(rc, 0);

    ZOS_DATASET *tgt = ds_catalog_find("REPRO.TARGET");
    ASSERT_NOT_NULL(tgt);
    ASSERT_EQ(tgt->records.count, 5);

    return 0;
}

static int test_idcams_run_stream(void) {
    reset_catalog();

    const char *stream =
        "DEFINE CLUSTER (NAME(STREAM.KSDS) INDEXED KEYS(4 0) RECORDSIZE(40 40))\n"
        "DEFINE GDG (NAME(STREAM.GDG) LIMIT(5) NOEMPTY SCRATCH)\n"
        "LISTCAT ENTRIES(STREAM)\n";

    int rc = idcams_run(stream);
    ASSERT_EQ(rc, 0);

    ASSERT_NOT_NULL(ds_catalog_find("STREAM.KSDS"));
    ASSERT_NOT_NULL(ds_catalog_find("STREAM.GDG"));
    return 0;
}

/* =========================================================================
 * CATALOG TESTS
 * ========================================================================= */

static int test_catalog_alloc_find_delete(void) {
    reset_catalog();

    ZOS_DATASET *ds = ds_catalog_alloc("CAT.TEST.DS", DSORG_PS,
                                        RECFM_FB, 80, 800);
    ASSERT_NOT_NULL(ds);
    ASSERT_EQ(ds->dsorg, DSORG_PS);
    ASSERT_EQ(ds->lrecl, 80);

    /* Find */
    ZOS_DATASET *found = ds_catalog_find("CAT.TEST.DS");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(found, ds);

    /* Lowercase DSN should also work */
    ASSERT_NOT_NULL(ds_catalog_find("cat.test.ds"));

    /* Duplicate alloc → NULL */
    ASSERT_NULL(ds_catalog_alloc("CAT.TEST.DS", DSORG_PS, RECFM_FB, 80, 800));

    /* Delete */
    ASSERT_EQ(ds_catalog_delete("CAT.TEST.DS"), DS_OK);
    ASSERT_NULL(ds_catalog_find("CAT.TEST.DS"));

    /* Delete non-existent */
    ASSERT_EQ(ds_catalog_delete("NOT.THERE"), DS_NOT_FOUND);

    return 0;
}

/* =========================================================================
 * MAIN
 * ========================================================================= */

int main(void) {
    printf("\n");
    printf(" ============================================================\n");
    printf("  z/OS DATASET SIMULATOR — TEST SUITE\n");
    printf(" ============================================================\n\n");

    /* Catalog */
    run_test("catalog: alloc / find / delete",        test_catalog_alloc_find_delete);

    /* PS */
    run_test("ps: FB write 10 records and read back",  test_ps_fb_write_read);
    run_test("ps: VB RDW encode/decode",               test_ps_vb_rdw);
    run_test("ps: VB variable-length write/read",      test_ps_vb_write_read);
    run_test("ps: POINT to specific record",           test_ps_point);
    run_test("ps: EXTEND appends to existing dataset", test_ps_extend);

    /* PDS */
    run_test("pds: STOW member / FIND / read",         test_pds_member_stow_find);
    run_test("pds: list members",                      test_pds_list_members);

    /* VSAM KSDS */
    run_test("vsam ksds: insert / GET by key",         test_vsam_ksds_insert_get);
    run_test("vsam ksds: duplicate key → RTNCD=8",    test_vsam_ksds_duplicate_key);
    run_test("vsam ksds: sequential GET in key order", test_vsam_ksds_sequential);
    run_test("vsam ksds: ERASE (logical delete)",      test_vsam_ksds_erase);

    /* VSAM ESDS */
    run_test("vsam esds: append and sequential read",  test_vsam_esds_append_read);

    /* VSAM RRDS */
    run_test("vsam rrds: write / read by slot (RRN)",  test_vsam_rrds_slots);

    /* GDG */
    run_test("gdg: define / new gens / resolve (0)(-1)",test_gdg_relative_gen);
    run_test("gdg: rolloff when LIMIT exceeded",        test_gdg_rolloff);

    /* IDCAMS */
    run_test("idcams: DEFINE CLUSTER / LISTCAT / DELETE", test_idcams_define_listcat_delete);
    run_test("idcams: REPRO PS to PS",                  test_idcams_repro_ps_to_ps);
    run_test("idcams: multi-command stream",             test_idcams_run_stream);

    printf("\n");
    printf(" ============================================================\n");
    printf("  RESULTS: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("  (%d FAILED)", tests_failed);
    printf("\n");
    printf(" ============================================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
