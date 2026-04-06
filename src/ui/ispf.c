/*
 * ispf.c — ISPF/PDF Utilities Panel Simulator
 *
 * Simulates ISPF 3.x (Utilities) panels: Library (3.1), Data Set (3.2),
 * Move/Copy (3.3), DSList (3.4), Browse, Edit, Member List, Allocate.
 *
 * Terminal: 80x24 ANSI. All I/O via stdio (fgets/printf).
 * C99. No external dependencies beyond standard C library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "datasets/datasets.h"
#include "ispf.h"

/* -------------------------------------------------------------------------
 * ANSI positioning / attribute macros
 * ---------------------------------------------------------------------- */
#define ISPF_GOTO(r,c)   printf("\033[%d;%dH",(r),(c))
#define ISPF_CLR_EOL     printf("\033[K")
#define ISPF_CLR_SCR     printf("\033[2J\033[H")
#define ISPF_REV_ON      printf("\033[7m")
#define ISPF_REV_OFF     printf("\033[27m")
#define ISPF_BOLD        printf("\033[1m")
#define ISPF_NORM        printf("\033[0m")
#define ISPF_HIDE_CUR    printf("\033[?25l")
#define ISPF_SHOW_CUR    printf("\033[?25h")
#define ISPF_CYAN        printf("\033[36m")
#define ISPF_YELLOW      printf("\033[33m")
#define ISPF_GREEN       printf("\033[32m")
#define ISPF_WHITE       printf("\033[37m")

/* -------------------------------------------------------------------------
 * Panel dimensions
 * ---------------------------------------------------------------------- */
#define PANEL_COLS  80
#define PANEL_ROWS  24
#define CMD_ROW     3
#define CMD_COL     14
#define BODY_TOP    4
#define BODY_BOT    22

/* -------------------------------------------------------------------------
 * Short / long message state (persists one panel cycle)
 * ---------------------------------------------------------------------- */
static char s_short_msg[9]  = "";
static char s_long_msg[80]  = "";

static void set_msg(const char *sh, const char *lg)
{
    strncpy(s_short_msg, sh ? sh : "", sizeof(s_short_msg) - 1);
    s_short_msg[sizeof(s_short_msg) - 1] = '\0';
    strncpy(s_long_msg,  lg ? lg : "", sizeof(s_long_msg)  - 1);
    s_long_msg[sizeof(s_long_msg) - 1] = '\0';
}

static void clear_msg(void)
{
    s_short_msg[0] = '\0';
    s_long_msg[0]  = '\0';
}

/* -------------------------------------------------------------------------
 * Utility: uppercase a string in-place
 * ---------------------------------------------------------------------- */
static void str_upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* -------------------------------------------------------------------------
 * Utility: trim trailing whitespace / CR / LF in-place
 * ---------------------------------------------------------------------- */
static void str_rtrim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' '))
        s[--n] = '\0';
}

/* -------------------------------------------------------------------------
 * draw_header — row 1: reversed bar with panel_id, centered title, right_info
 *               row 2: yellow short message (right-aligned) or long message
 * ---------------------------------------------------------------------- */
static void draw_header(const char *panel_id, const char *title,
                        const char *right_info)
{
    char bar[PANEL_COLS + 1];
    int  id_len    = (int)strlen(panel_id);
    int  title_len = (int)strlen(title);
    int  ri_len    = right_info ? (int)strlen(right_info) : 0;
    int  pad_left, i;

    /* Build the 80-char bar string */
    memset(bar, ' ', PANEL_COLS);
    bar[PANEL_COLS] = '\0';

    /* panel_id left */
    if (id_len > 0) {
        int copy = id_len < PANEL_COLS ? id_len : PANEL_COLS;
        memcpy(bar, panel_id, (size_t)copy);
    }

    /* title centered */
    pad_left = (PANEL_COLS - title_len) / 2;
    if (pad_left < 0) pad_left = 0;
    if (pad_left + title_len <= PANEL_COLS)
        memcpy(bar + pad_left, title, (size_t)title_len);

    /* right_info right-justified */
    if (ri_len > 0 && ri_len < PANEL_COLS) {
        int start = PANEL_COLS - ri_len;
        memcpy(bar + start, right_info, (size_t)ri_len);
    }

    ISPF_GOTO(1, 1);
    ISPF_REV_ON;
    ISPF_BOLD;
    printf("%s", bar);
    ISPF_NORM;

    /* Row 2: message area */
    ISPF_GOTO(2, 1);
    ISPF_CLR_EOL;
    if (s_short_msg[0] != '\0') {
        int sm_len = (int)strlen(s_short_msg);
        ISPF_YELLOW;
        ISPF_GOTO(2, PANEL_COLS - sm_len);
        printf("%s", s_short_msg);
        ISPF_NORM;
    }
    if (s_long_msg[0] != '\0') {
        ISPF_GOTO(2, 1);
        ISPF_YELLOW;
        printf("%s", s_long_msg);
        ISPF_NORM;
    }
    for (i = 3; i <= BODY_BOT; i++) {
        ISPF_GOTO(i, 1);
        ISPF_CLR_EOL;
    }
}

/* -------------------------------------------------------------------------
 * draw_pf_bar — rows 23-24 PF key legend (reversed)
 * ---------------------------------------------------------------------- */
static void draw_pf_bar(void)
{
    ISPF_GOTO(23, 1);
    ISPF_REV_ON;
    printf("%-80s", " F1=Help    F2=Split   F3=End     F5=RFind   F6=RChange F7=Up");
    ISPF_NORM;
    ISPF_GOTO(24, 1);
    ISPF_REV_ON;
    printf("%-80s", " F8=Down    F9=Swap    F10=Left   F11=Right  F12=Cancel");
    ISPF_NORM;
}

/* -------------------------------------------------------------------------
 * read_cmd — print "Command ===>" prompt on row 3, read input, uppercase
 * ---------------------------------------------------------------------- */
static void read_cmd(char *buf, int maxlen)
{
    char tmp[256];
    ISPF_GOTO(CMD_ROW, 1);
    ISPF_CLR_EOL;
    ISPF_WHITE;
    printf(" Command ===>");
    ISPF_CYAN;
    printf(" ");
    ISPF_NORM;
    ISPF_SHOW_CUR;
    fflush(stdout);

    if (fgets(tmp, (int)sizeof(tmp), stdin) == NULL)
        tmp[0] = '\0';
    str_rtrim(tmp);
    str_upper(tmp);

    strncpy(buf, tmp, (size_t)(maxlen - 1));
    buf[maxlen - 1] = '\0';
    ISPF_HIDE_CUR;
}

/* -------------------------------------------------------------------------
 * show_field — print a field value in cyan at row,col padded to 'width'
 *              with underscores for empty space
 * ---------------------------------------------------------------------- */
static void show_field(int row, int col, int width, const char *val)
{
    int vlen = val ? (int)strlen(val) : 0;
    int i;
    ISPF_GOTO(row, col);
    ISPF_CYAN;
    if (vlen > 0) printf("%.*s", width, val);
    for (i = vlen; i < width; i++) putchar('_');
    ISPF_NORM;
}

/* -------------------------------------------------------------------------
 * read_field — show field, read new value; if blank keeps existing value
 *              uppercases result
 * ---------------------------------------------------------------------- */
static void read_field(int row, int col, int width, char *buf, int maxlen)
{
    char tmp[256];
    show_field(row, col, width, buf);
    ISPF_GOTO(row, col);
    ISPF_SHOW_CUR;
    fflush(stdout);

    if (fgets(tmp, (int)sizeof(tmp), stdin) == NULL)
        tmp[0] = '\0';
    str_rtrim(tmp);

    if (tmp[0] != '\0') {
        str_upper(tmp);
        strncpy(buf, tmp, (size_t)(maxlen - 1));
        buf[maxlen - 1] = '\0';
    }
    /* else: keep existing buf */
    ISPF_HIDE_CUR;
}

/* -------------------------------------------------------------------------
 * wait_enter — show message at row 22, wait for Enter key
 * ---------------------------------------------------------------------- */
static void wait_enter(const char *msg)
{
    char tmp[16];
    ISPF_GOTO(22, 1);
    ISPF_CLR_EOL;
    ISPF_YELLOW;
    if (msg) printf(" %s", msg);
    ISPF_NORM;
    ISPF_SHOW_CUR;
    fflush(stdout);
    if (fgets(tmp, (int)sizeof(tmp), stdin) == NULL) { /* ignore */ }
    ISPF_HIDE_CUR;
}

/* -------------------------------------------------------------------------
 * validate_member_name — returns 1 if valid IMS/z/OS member name
 * Rules: 1-8 chars, first=alpha|#|$|@, rest=alnum|#|$|@|-
 * ---------------------------------------------------------------------- */
static int validate_member_name(const char *name)
{
    int i, n;
    if (!name || name[0] == '\0') return 0;
    n = (int)strlen(name);
    if (n > 8) return 0;
    if (!isalpha((unsigned char)name[0]) &
        name[0] != '#' && name[0] != '$' && name[0] != '@')
        return 0;
    for (i = 1; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!isalnum(c) && c != '#' && c != '$' && c != '@' && c != '-')
            return 0;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * dsorg_str / recfm_str — format enums for display
 * ---------------------------------------------------------------------- */
static const char *dsorg_str(ZOS_DSORG dsorg)
{
    switch (dsorg) {
        case DSORG_PS: return "PS";
        case DSORG_PO: return "PO";
        default:       return "??";
    }
}

static const char *recfm_str(ZOS_RECFM recfm)
{
    switch (recfm) {
        case RECFM_FB: return "FB";
        default:       return "??";
    }
}

/* -------------------------------------------------------------------------
 * Forward declarations for panel functions
 * ---------------------------------------------------------------------- */
static void ispf_library_panel(void);
static void ispf_dataset_utility(void);
static void ispf_allocate_panel(const char *initial_dsn);
static void ispf_dslist_panel(void);
static void ispf_dslist_results(const char *filter);
static void ispf_member_list(const char *dsn);
static void ispf_browse(const char *dsn, const char *member);
static void ispf_edit(const char *dsn, const char *member);

/* =========================================================================
 * PUBLIC: ispf_utilities_menu — ISRUTIL — top-level utilities dispatcher
 * ====================================================================== */
void ispf_utilities_menu(void)
{
    char cmd[80];

    for (;;) {
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRUTIL", "ISPF/PDF  UTILITIES", "");
        clear_msg();

        /* Body */
        ISPF_GOTO(5,  1); printf("  Option ===>");
        ISPF_GOTO(7,  1); printf("   1  Library      - Compress or print data set. Print index listing.");
        ISPF_GOTO(8,  1); printf("                     Print, rename, delete, browse, or edit members.");
        ISPF_GOTO(9,  1); printf("   2  Data Set     - Allocate, rename, delete, catalog, uncatalog,");
        ISPF_GOTO(10, 1); printf("                     or display information of an entire data set.");
        ISPF_GOTO(11, 1); printf("   3  Move/Copy    - Move, or copy, members or data sets.");
        ISPF_GOTO(12, 1); printf("   4  Dslist       - Print or display (to process) list of data set");
        ISPF_GOTO(13, 1); printf("                     names and/or members. Print or display VTOC information.");
        ISPF_GOTO(15, 1); printf("  Enter END command to terminate ISPF.");

        draw_pf_bar();

        /* Position cursor at option field */
        ISPF_GOTO(5, 14);
        ISPF_SHOW_CUR;
        fflush(stdout);

        read_cmd(cmd, (int)sizeof(cmd));

        if (strcmp(cmd, "END") == 0 || strcmp(cmd, "=X") == 0) {
            ISPF_CLR_SCR;
            ISPF_NORM;
            return;
        } else if (strcmp(cmd, "1") == 0 || strcmp(cmd, "=3.1") == 0) {
            ispf_library_panel();
        } else if (strcmp(cmd, "2") == 0 || strcmp(cmd, "=3.2") == 0) {
            ispf_dataset_utility();
        } else if (strcmp(cmd, "3") == 0) {
            set_msg("NOT IMPL", "Move/Copy not implemented in this release");
        } else if (strcmp(cmd, "4") == 0 || strcmp(cmd, "=3.4") == 0) {
            ispf_dslist_panel();
        } else if (cmd[0] != '\0') {
            set_msg("INVALID", "Invalid option");
        }
    }
}

/* =========================================================================
 * STATIC: ispf_library_panel — ISRLIBD — Option 3.1
 * ====================================================================== */
static void ispf_library_panel(void)
{
    char opt[4]    = "";
    char dsn[DS_DSN_LEN + 1]     = "";
    char member[DS_MEMBER_LEN + 1] = "";

    for (;;) {
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRLIBD", "LIBRARY UTILITY", "");

        ISPF_GOTO(4,  1); printf("  Option ===>");
        ISPF_GOTO(5,  1); ISPF_BOLD; printf("  Blank"); ISPF_NORM;
        printf(" - Display member list");
        ISPF_GOTO(6,  1); printf("    C - Compress data set          ");
        ISPF_BOLD; printf("E"); ISPF_NORM; printf(" - Edit member");
        ISPF_GOTO(7,  1); printf("    P - Print member               ");
        ISPF_BOLD; printf("D"); ISPF_NORM; printf(" - Delete member");
        ISPF_GOTO(8,  1); printf("    R - Rename member              ");
        ISPF_BOLD; printf("B"); ISPF_NORM; printf(" - Browse member");
        ISPF_GOTO(9,  1); printf("    X - Print index listing");
        ISPF_GOTO(11, 1); printf("  ISPF Library or Other Partitioned Data Set:");
        ISPF_GOTO(13, 1); printf("     Data Set Name . . .");
        ISPF_GOTO(15, 1); printf("  Member  . . . . . . .");
        ISPF_GOTO(16, 1); printf("                                (Blank for member list)");
        ISPF_GOTO(17, 1); printf("  Password  . . . . . .          (If password protected)");

        show_field(4,  14, 2, opt);
        show_field(13, 26, DS_DSN_LEN, dsn);
        show_field(15, 26, DS_MEMBER_LEN, member);

        draw_pf_bar();

        /* Read option */
        ISPF_GOTO(4, 14);
        ISPF_SHOW_CUR;
        fflush(stdout);
        {
            char line[256];
            ISPF_GOTO(4, 14); ISPF_CLR_EOL;
            ISPF_CYAN; printf("%.2s", opt); ISPF_NORM;
            ISPF_GOTO(4, 14);
            fflush(stdout);
            if (fgets(line, (int)sizeof(line), stdin)) {
                str_rtrim(line);
                str_upper(line);
                if (line[0] != '\0') {
                    strncpy(opt, line, sizeof(opt) - 1);
                    opt[sizeof(opt) - 1] = '\0';
                }
            }
        }

        /* Check for END */
        if (strcmp(opt, "END") == 0 || strcmp(opt, "=X") == 0) return;

        /* Read DSN */
        ISPF_GOTO(13, 26);
        {
            char line[256];
            ISPF_CLR_EOL;
            ISPF_CYAN;
            printf("%-*s", DS_DSN_LEN, dsn);
            ISPF_NORM;
            ISPF_GOTO(13, 26);
            fflush(stdout);
            if (fgets(line, (int)sizeof(line), stdin)) {
                str_rtrim(line);
                str_upper(line);
                if (line[0] != '\0') {
                    strncpy(dsn, line, DS_DSN_LEN);
                    dsn[DS_DSN_LEN] = '\0';
                }
            }
        }

        if (strcmp(dsn, "END") == 0) return;

        /* Read member */
        ISPF_GOTO(15, 26);
        {
            char line[256];
            ISPF_CLR_EOL;
            ISPF_CYAN;
            printf("%-*s", DS_MEMBER_LEN, member);
            ISPF_NORM;
            ISPF_GOTO(15, 26);
            fflush(stdout);
            if (fgets(line, (int)sizeof(line), stdin)) {
                str_rtrim(line);
                str_upper(line);
                if (line[0] != '\0') {
                    strncpy(member, line, DS_MEMBER_LEN);
                    member[DS_MEMBER_LEN] = '\0';
                }
            }
        }

        if (dsn[0] == '\0') {
            set_msg("MISSING", "Data set name required");
            continue;
        }

        /* Validate DSN is in catalog */
        {
            ZOS_DATASET *ds = ds_catalog_find(dsn);
            if (ds == NULL) {
                set_msg("NOT CATG", "Data set not in catalog");
                continue;
            }
            if (ds->dsorg != DSORG_PO) {
                /* For non-PDS operations, warn if PDS-specific op */
                if (strcmp(opt, "C") == 0 ||
                    strcmp(opt, "R") == 0 ||
                    (strcmp(opt, "D") == 0 && member[0] != '\0') ||
                    (strcmp(opt, "E") == 0 && member[0] == '\0') ) {
                    set_msg("NOT PDS", "Data set is not a partitioned data set");
                    continue;
                }
            }

            clear_msg();

            if (strcmp(opt, "C") == 0) {
                ISPF_GOTO(20, 1);
                ISPF_YELLOW;
                printf("  COMPRESS: not implemented (use IDCAMS REPRO)");
                ISPF_NORM;
                wait_enter("Press Enter to continue...");
            } else if (strcmp(opt, "D") == 0) {
                if (member[0] == '\0') {
                    set_msg("NO MBR", "Member name required for delete");
                    continue;
                }
                if (!validate_member_name(member)) {
                    set_msg("INV MBR", "Invalid member name");
                    continue;
                }
                /* Find and "delete" member by zeroing record_count */
                {
                    int found = 0;
                    int i;
                    for (i = 0; i < ds->member_count; i++) {
                        if (strcmp(ds->members[i].name, member) == 0) {
                            ds->members[i].record_count = 0;
                            found = 1;
                            break;
                        }
                    }
                    if (found) {
                        ISPF_GOTO(20, 1);
                        ISPF_GREEN;
                        printf("  IEC130I %s(%s) MEMBER DELETED", dsn, member);
                        ISPF_NORM;
                        set_msg("DELETED", "Member deleted");
                    } else {
                        set_msg("NOT FND", "Member not found in PDS");
                    }
                }
                wait_enter("Press Enter to continue...");
            } else if (strcmp(opt, "R") == 0) {
                if (member[0] == '\0') {
                    set_msg("NO MBR", "Member name required for rename");
                    continue;
                }
                {
                    char newname[DS_MEMBER_LEN + 1] = "";
                    ISPF_GOTO(19, 1);
                    printf("  New member name: ");
                    ISPF_SHOW_CUR;
                    fflush(stdout);
                    {
                        char line[256];
                        if (fgets(line, (int)sizeof(line), stdin)) {
                            str_rtrim(line);
                            str_upper(line);
                            strncpy(newname, line, DS_MEMBER_LEN);
                            newname[DS_MEMBER_LEN] = '\0';
                        }
                    }
                    ISPF_HIDE_CUR;
                    if (!validate_member_name(newname)) {
                        set_msg("INV MBR", "Invalid new member name");
                        continue;
                    }
                    {
                        int found = 0;
                        int i;
                        for (i = 0; i < ds->member_count; i++) {
                            if (strcmp(ds->members[i].name, member) == 0) {
                                strncpy(ds->members[i].name, newname,
                                        DS_MEMBER_LEN);
                                ds->members[i].name[DS_MEMBER_LEN] = '\0';
                                found = 1;
                                break;
                            }
                        }
                        if (found) {
                            strncpy(member, newname, DS_MEMBER_LEN);
                            set_msg("RENAMED", "Member renamed");
                        } else {
                            set_msg("NOT FND", "Member not found");
                        }
                    }
                }
                wait_enter("Press Enter to continue...");
            } else if (strcmp(opt, "E") == 0) {
                if (member[0] == '\0') {
                    set_msg("NO MBR", "Member name required for edit");
                    continue;
                }
                ispf_edit(dsn, member);
            } else if (strcmp(opt, "B") == 0) {
                ispf_browse(dsn, member);
            } else {
                /* blank or unrecognized → member list or browse */
                if (member[0] != '\0') {
                    ispf_browse(dsn, member);
                } else {
                    ispf_member_list(dsn);
                }
            }
        }
        clear_msg();
    }
}

/* =========================================================================
 * STATIC: ispf_dataset_utility — ISRDUTIL — Option 3.2
 * ====================================================================== */
static void ispf_dataset_utility(void)
{
    char opt[4]    = "";
    char dsn[DS_DSN_LEN + 1] = "";
    char volser[7] = "";

    for (;;) {
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRDUTIL", "DATA SET UTILITY", "");

        ISPF_GOTO(4,  1); printf("  Option ===>");
        ISPF_GOTO(6,  1); printf("    A  - Allocate new data set     R  - Rename entire data set");
        ISPF_GOTO(7,  1); printf("    D  - Delete entire data set    S  - Short data set information");
        ISPF_GOTO(8,  1); printf("    U  - Uncatalog a data set      I  - Data set information");
        ISPF_GOTO(10, 1); printf("  ISPF Library or Other Partitioned or Sequential Data Set:");
        ISPF_GOTO(12, 1); printf("     Data Set Name . . .");
        ISPF_GOTO(14, 1); printf("  Volume Serial  . . .  ______  (If not cataloged)");

        show_field(4,  14, 2, opt);
        show_field(12, 26, DS_DSN_LEN, dsn);
        show_field(14, 24, 6, volser);

        draw_pf_bar();

        ISPF_GOTO(4, 14);
        ISPF_SHOW_CUR;
        fflush(stdout);

        {
            char line[256];
            ISPF_GOTO(4, 14); ISPF_CLR_EOL;
            ISPF_CYAN; printf("%.2s", opt); ISPF_NORM;
            ISPF_GOTO(4, 14);
            fflush(stdout);
            if (fgets(line, (int)sizeof(line), stdin)) {
                str_rtrim(line);
                str_upper(line);
                if (line[0] != '\0') {
                    strncpy(opt, line, sizeof(opt) - 1);
                    opt[sizeof(opt) - 1] = '\0';
                }
            }
        }

        if (strcmp(opt, "END") == 0 || strcmp(opt, "=X") == 0) return;

        /* Read DSN */
        {
            char line[256];
            ISPF_GOTO(12, 26); ISPF_CLR_EOL;
            ISPF_CYAN; printf("%-*s", DS_DSN_LEN, dsn); ISPF_NORM;
            ISPF_GOTO(12, 26);
            fflush(stdout);
            if (fgets(line, (int)sizeof(line), stdin)) {
                str_rtrim(line);
                str_upper(line);
                if (line[0] != '\0') {
                    strncpy(dsn, line, DS_DSN_LEN);
                    dsn[DS_DSN_LEN] = '\0';
                }
            }
        }

        if (strcmp(dsn, "END") == 0) return;

        if (strcmp(opt, "A") == 0) {
            ispf_allocate_panel(dsn);
            dsn[0] = '\0';
            continue;
        }

        if (dsn[0] == '\0') {
            set_msg("MISSING", "Data set name required");
            continue;
        }

        if (strcmp(opt, "D") == 0) {
            ISPF_GOTO(19, 1);
            ISPF_YELLOW;
            printf("  DELETE: use IDCAMS DELETE for now (catalog removal not directly supported)");
            ISPF_NORM;
            ISPF_GOTO(20, 1);
            printf("  IGD101I %s CATALOG ENTRY NOT REMOVED", dsn);
            wait_enter("Press Enter to continue...");
        } else if (strcmp(opt, "I") == 0 || strcmp(opt, "S") == 0) {
            ZOS_DATASET *ds = ds_catalog_find(dsn);
            if (ds == NULL) {
                set_msg("NOT CATG", "Data set not in catalog");
                continue;
            }
            ISPF_GOTO(19, 1);
            ISPF_CYAN;
            printf("  Data Set Name  : %-44s", ds->dsn);
            ISPF_GOTO(20, 1);
            printf("  Dsorg: %-4s  Recfm: %-4s  Lrecl: %-6d  Blksize: %-6d",
                   dsorg_str(ds->dsorg), recfm_str(ds->recfm),
                   ds->lrecl, ds->blksize);
            if (ds->dsorg == DSORG_PO) {
                ISPF_GOTO(21, 1);
                printf("  Members: %d", ds->member_count);
            }
            ISPF_NORM;
            wait_enter("Press Enter to continue...");
        } else if (strcmp(opt, "R") == 0) {
            ISPF_GOTO(19, 1);
            ISPF_YELLOW;
            printf("  RENAME: not implemented in this release");
            ISPF_NORM;
            wait_enter("Press Enter to continue...");
        } else if (strcmp(opt, "U") == 0) {
            ISPF_GOTO(19, 1);
            ISPF_YELLOW;
            printf("  UNCATALOG: use IDCAMS DELETE NOSCRATCH for now");
            ISPF_NORM;
            wait_enter("Press Enter to continue...");
        } else if (opt[0] != '\0') {
            set_msg("INVALID", "Invalid option");
        }
        clear_msg();
    }
}

/* =========================================================================
 * STATIC: ispf_allocate_panel — ISRACDA — called from 3.2 A
 * ====================================================================== */
static void ispf_allocate_panel(const char *initial_dsn)
{
    char dsn[DS_DSN_LEN + 1]  = "";
    char mgmt_class[9]        = "";
    char stor_class[9]        = "";
    char volser[7]            = "";
    char data_class[9]        = "";
    char space_units[8]       = "TRACKS";
    char primary[8]           = "1";
    char secondary[8]         = "1";
    char dir_blocks[8]        = "0";
    char recfm_buf[4]         = "FB";
    char lrecl_buf[8]         = "80";
    char blksize_buf[8]       = "800";
    char dstype[8]            = "PDS";
    char profile[9]           = "";
    char mixed[4]             = "NO";

    if (initial_dsn && initial_dsn[0] != '\0') {
        strncpy(dsn, initial_dsn, DS_DSN_LEN);
        dsn[DS_DSN_LEN] = '\0';
    }

    for (;;) {
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRACDA", "ALLOCATE NEW DATA SET", "Data set type: NEW");

        ISPF_GOTO(4,  1); printf("  Command ===>");
        ISPF_GOTO(6,  1); printf("  Data Set Name  . . .");
        ISPF_GOTO(8,  1); printf("  Management Class . .");
        ISPF_GOTO(9,  1); printf("  Storage Class  . . .");
        ISPF_GOTO(10, 1); printf("    Volume Serial  . .");
        ISPF_GOTO(11, 1); printf("    Data Class . . . .");
        ISPF_GOTO(12, 1); printf("    Space Units  . . .");
        ISPF_GOTO(13, 1); printf("    Primary quantity .");
        ISPF_GOTO(14, 1); printf("    Secondary quantity");
        ISPF_GOTO(15, 1); printf("    Directory blocks .");
        ISPF_GOTO(16, 1); printf("    Record Format  . .");
        ISPF_GOTO(17, 1); printf("    Record Length  . .");
        ISPF_GOTO(18, 1); printf("    Block Size . . . .");
        ISPF_GOTO(19, 1); printf("    Data set name type");
        ISPF_GOTO(20, 1); printf("    Profile name . . .");
        ISPF_GOTO(21, 1); printf("    Mixed mode . . . .");

        /* Annotations */
        ISPF_GOTO(8,  56); ISPF_WHITE; printf("(Blank for default management class)");
        ISPF_GOTO(9,  56); printf("(Blank for default storage class)");
        ISPF_GOTO(10, 56); printf("(Blank for system default volume)");
        ISPF_GOTO(11, 56); printf("(Blank for default data class)");
        ISPF_GOTO(12, 56); printf("(BLKS, TRACKS, or CYLS)");
        ISPF_GOTO(13, 56); printf("(In above units)");
        ISPF_GOTO(14, 56); printf("(In above units)");
        ISPF_GOTO(15, 56); printf("(Zero for sequential data set)");
        ISPF_GOTO(19, 56); printf("(PDS, PS, or blank for PS)");
        ISPF_GOTO(21, 56); printf("(YES or NO)");
        ISPF_NORM;

        /* Show field values */
        show_field(6,  23, DS_DSN_LEN, dsn);
        show_field(8,  23, 8,  mgmt_class);
        show_field(9,  23, 8,  stor_class);
        show_field(10, 23, 6,  volser);
        show_field(11, 23, 8,  data_class);
        show_field(12, 23, 8,  space_units);
        show_field(13, 23, 6,  primary);
        show_field(14, 23, 6,  secondary);
        show_field(15, 23, 6,  dir_blocks);
        show_field(16, 23, 4,  recfm_buf);
        show_field(17, 23, 6,  lrecl_buf);
        show_field(18, 23, 6,  blksize_buf);
        show_field(19, 23, 8,  dstype);
        show_field(20, 23, 8,  profile);
        show_field(21, 23, 4,  mixed);

        draw_pf_bar();

        ISPF_GOTO(6, 23);
        ISPF_SHOW_CUR;
        fflush(stdout);

        /* Read DSN field */
        {
            char line[256];
            ISPF_CLR_EOL;
            ISPF_CYAN; printf("%-*s", DS_DSN_LEN, dsn); ISPF_NORM;
            ISPF_GOTO(6, 23);
            fflush(stdout);
            if (fgets(line, (int)sizeof(line), stdin)) {
                str_rtrim(line);
                str_upper(line);
                if (strcmp(line, "END") == 0) return;
                if (line[0] != '\0') {
                    strncpy(dsn, line, DS_DSN_LEN);
                    dsn[DS_DSN_LEN] = '\0';
                }
            }
        }

        /* Read critical fields */
        read_field(12, 23, 8, space_units, (int)sizeof(space_units));
        read_field(15, 23, 6, dir_blocks,  (int)sizeof(dir_blocks));
        read_field(16, 23, 4, recfm_buf,   (int)sizeof(recfm_buf));
        read_field(17, 23, 6, lrecl_buf,   (int)sizeof(lrecl_buf));
        read_field(18, 23, 6, blksize_buf, (int)sizeof(blksize_buf));
        read_field(19, 23, 8, dstype,      (int)sizeof(dstype));

        ISPF_HIDE_CUR;

        if (dsn[0] == '\0') {
            set_msg("MISSING", "Data set name required");
            continue;
        }

        /* Validate and allocate */
        {
            int lrecl    = atoi(lrecl_buf);
            int blksize  = atoi(blksize_buf);
            int dir_blk  = atoi(dir_blocks);
            ZOS_DSORG   dsorg;
            ZOS_RECFM   recfm;
            ZOS_DATASET *newds;

            /* Map dstype to DSORG */
            if (strcmp(dstype, "PDS") == 0 || dir_blk > 0) {
                dsorg = DSORG_PO;
            } else {
                dsorg = DSORG_PS;
            }

            /* Map recfm */
            if (strcmp(recfm_buf, "FB") == 0) {
                recfm = RECFM_FB;
            } else {
                recfm = RECFM_FB; /* default */
            }

            if (lrecl <= 0 || lrecl > 32760) {
                set_msg("INV LEN", "Invalid record length");
                continue;
            }
            if (blksize <= 0 || blksize > 32760) {
                set_msg("INV BLK", "Invalid block size");
                continue;
            }

            /* Check not already in catalog */
            if (ds_catalog_find(dsn) != NULL) {
                set_msg("ALREADY", "Data set already in catalog");
                continue;
            }

            newds = ds_catalog_alloc(dsn, dsorg, recfm, lrecl, blksize);
            if (newds == NULL) {
                set_msg("FAILED", "Allocation failed - catalog full");
                continue;
            }

            ISPF_GOTO(22, 1);
            ISPF_GREEN;
            printf("  IEF285I %s CATALOGED", dsn);
            ISPF_NORM;
            set_msg("ALLOC OK", "Data set allocated and cataloged");
            wait_enter("Press Enter to continue...");
            return;
        }
    }
}

/* =========================================================================
 * STATIC: ispf_dslist_panel — ISRDSAB — Option 3.4
 * ====================================================================== */
static void ispf_dslist_panel(void)
{
    char filter[DS_DSN_LEN + 1] = "";
    char volser[7]              = "";
    char init_view[4]           = "1";
    char confirm[4]             = "Y";

    for (;;) {
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRDSAB", "DATA SET LIST UTILITY", "");

        ISPF_GOTO(4,  1); printf("  Command ===>");
        ISPF_GOTO(6,  1); printf("  Enter one or more of the following:");
        ISPF_GOTO(7,  1); printf("  Dsname Level  . . .");
        ISPF_GOTO(7,  50); printf("(Blank for all data sets)");
        ISPF_GOTO(8,  1); printf("  Volume serial . . .");
        ISPF_GOTO(10, 1); printf("  Data set list options:");
        ISPF_GOTO(11, 1); printf("    Initial View  . .");
        ISPF_GOTO(11, 30); printf("1. Volume  2. Space  3. Attrib  4. Total");
        ISPF_GOTO(12, 1); printf("    Confirm Delete  .");
        ISPF_GOTO(12, 30); printf("Y. Yes     N. No");
        ISPF_GOTO(14, 1); printf("  When the data set list is displayed, enter either:");
        ISPF_GOTO(15, 1); printf("     \"/\" to obtain a selection menu");
        ISPF_GOTO(16, 1); printf("     A line command to perform one of the following:");
        ISPF_GOTO(17, 1); printf("       B  - Browse data set               I  - Data set information");
        ISPF_GOTO(18, 1); printf("       E  - Edit data set                 D  - Delete data set");
        ISPF_GOTO(19, 1); printf("       M  - Display member list");

        show_field(7,  22, DS_DSN_LEN, filter);
        show_field(8,  22, 6,          volser);
        show_field(11, 22, 1,          init_view);
        show_field(12, 22, 1,          confirm);

        draw_pf_bar();

        read_cmd(filter, DS_DSN_LEN + 1);

        if (strcmp(filter, "END") == 0 || strcmp(filter, "=X") == 0) return;

        /* Accept blank (show all) or a filter */
        str_upper(filter);
        clear_msg();
        ispf_dslist_results(filter);
    }
}

/* =========================================================================
 * STATIC: ispf_dslist_results — filtered dataset list with line commands
 * ====================================================================== */
static void ispf_dslist_results(const char *filter)
{
    /* Build a local index of matching datasets */
#define MAX_LISTED 64
    ZOS_DATASET *listed[MAX_LISTED];
    int          count = 0;
    int          i;
    char         right_info[32];
    char         title[80];
    int          filter_len = filter ? (int)strlen(filter) : 0;

    /* Scan catalog */
    for (i = 0; i < zos_catalog.count && count < MAX_LISTED; i++) {
        ZOS_DATASET *ds = &zos_catalog.datasets[i];
        if (ds == NULL) continue;
        if (filter_len == 0) {
            listed[count++] = ds;
        } else {
            /* Strip trailing '*' for prefix match */
            char pat[DS_DSN_LEN + 1];
            int  pat_len;
            strncpy(pat, filter, DS_DSN_LEN);
            pat[DS_DSN_LEN] = '\0';
            pat_len = (int)strlen(pat);
            if (pat_len > 0 && pat[pat_len - 1] == '*') {
                pat[--pat_len] = '\0';
            }
            if (strncmp(ds->dsn, pat, (size_t)pat_len) == 0)
                listed[count++] = ds;
        }
    }

    snprintf(right_info, (int)sizeof(right_info), "Row 1 of %d", count);
    snprintf(title, (int)sizeof(title), "DSLIST - %s",
             (filter && filter[0] != '\0') ? filter : "ALL");

    for (;;) {
        int row;
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRDSLIST", title, right_info);

        /* Column headers */
        ISPF_GOTO(BODY_TOP, 1);
        ISPF_BOLD;
        printf(" Cmd  %-44s Dsorg Recfm Lrecl Blksize", "Dataset Name");
        ISPF_NORM;

        if (count == 0) {
            ISPF_GOTO(BODY_TOP + 1, 1);
            ISPF_YELLOW;
            printf("  (No data sets in catalog)");
            ISPF_NORM;
        }

        /* List rows */
        for (i = 0; i < count && i < (BODY_BOT - BODY_TOP - 1); i++) {
            row = BODY_TOP + 1 + i;
            ISPF_GOTO(row, 1);
            printf("      %-44s %-5s %-5s %5d %7d",
                   listed[i]->dsn,
                   dsorg_str(listed[i]->dsorg),
                   recfm_str(listed[i]->recfm),
                   listed[i]->lrecl,
                   listed[i]->blksize);
        }
        ISPF_GOTO(BODY_TOP + 1 + (count < (BODY_BOT - BODY_TOP - 1) ? count : (BODY_BOT - BODY_TOP - 1)), 1);
        printf(" **End**");

        draw_pf_bar();

        ISPF_GOTO(22, 1);
        ISPF_CLR_EOL;
        ISPF_YELLOW;
        printf("  Enter line cmd in Cmd column (B/E/M/D/I) then dataset number (1-%d), or END:", count);
        ISPF_NORM;

        {
            char line[80];
            char lc[4]  = "";
            int  ds_idx = 0;

            ISPF_GOTO(CMD_ROW, 1); ISPF_CLR_EOL;
            ISPF_WHITE; printf(" Command ===>"); ISPF_CYAN; printf(" "); ISPF_NORM;
            ISPF_SHOW_CUR; fflush(stdout);

            if (fgets(line, (int)sizeof(line), stdin) == NULL) line[0] = '\0';
            str_rtrim(line);
            str_upper(line);

            if (strcmp(line, "END") == 0 || strcmp(line, "=X") == 0) return;
            if (line[0] == '\0') return;

            /* Parse: "B 3" or "B3" */
            if (isalpha((unsigned char)line[0])) {
                lc[0] = line[0]; lc[1] = '\0';
                ds_idx = atoi(line + 1);
                if (line[1] == ' ') ds_idx = atoi(line + 2);
            } else {
                ds_idx = atoi(line);
            }

            if (ds_idx < 1 || ds_idx > count) {
                set_msg("INV IDX", "Invalid data set number");
                continue;
            }

            {
                ZOS_DATASET *ds = listed[ds_idx - 1];
                if (strcmp(lc, "B") == 0) {
                    ispf_browse(ds->dsn, "");
                } else if (strcmp(lc, "E") == 0) {
                    ispf_edit(ds->dsn, "");
                } else if (strcmp(lc, "M") == 0) {
                    if (ds->dsorg == DSORG_PO) ispf_member_list(ds->dsn);
                    else { set_msg("NOT PDS", "Not a partitioned data set"); }
                } else if (strcmp(lc, "D") == 0) {
                    ISPF_GOTO(22, 1); ISPF_CLR_EOL;
                    ISPF_YELLOW;
                    printf("  DELETE: use IDCAMS DELETE for now (not directly supported)");
                    ISPF_NORM;
                    wait_enter("Press Enter to continue...");
                } else if (strcmp(lc, "I") == 0) {
                    ISPF_GOTO(22, 1); ISPF_CLR_EOL;
                    ISPF_CYAN;
                    printf("  %s  Dsorg:%-4s Recfm:%-4s Lrecl:%-6d Blksize:%-6d",
                           ds->dsn, dsorg_str(ds->dsorg), recfm_str(ds->recfm),
                           ds->lrecl, ds->blksize);
                    ISPF_NORM;
                    wait_enter("Press Enter to continue...");
                } else {
                    set_msg("INV CMD", "Unknown line command");
                }
            }
        }
        clear_msg();
    }
#undef MAX_LISTED
}

/* =========================================================================
 * STATIC: ispf_member_list — ISRMLM — PDS member list
 * ====================================================================== */
static void ispf_member_list(const char *dsn)
{
    ZOS_DATASET *ds;
    char         title[80];
    char         right_info[32];
    int          i;

    ds = ds_catalog_find(dsn);
    if (ds == NULL) {
        set_msg("NOT CATG", "Data set not in catalog");
        return;
    }
    if (ds->dsorg != DSORG_PO) {
        set_msg("NOT PDS", "Data set is not a PDS");
        return;
    }

    snprintf(title, (int)sizeof(title), "MEMBER LIST - %s", dsn);
    snprintf(right_info, (int)sizeof(right_info), "Row 1 of %d", ds->member_count);

    for (;;) {
        int row;
        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISRMLM", title, right_info);

        ISPF_GOTO(BODY_TOP, 1);
        ISPF_BOLD;
        printf(" Cmd  %-8s   Size   Init    Mod   Id", "Name");
        ISPF_NORM;

        if (ds->member_count == 0) {
            ISPF_GOTO(BODY_TOP + 1, 1);
            ISPF_YELLOW;
            printf("  (No members)");
            ISPF_NORM;
        }

        for (i = 0; i < ds->member_count && i < (BODY_BOT - BODY_TOP - 1); i++) {
            row = BODY_TOP + 1 + i;
            ISPF_GOTO(row, 1);
            printf("      %-8s   %4d   %4d      0",
                   ds->members[i].name,
                   ds->members[i].record_count,
                   ds->members[i].record_count);
        }
        ISPF_GOTO(BODY_TOP + 1 + (ds->member_count < (BODY_BOT - BODY_TOP - 1)
                                   ? ds->member_count : (BODY_BOT - BODY_TOP - 1)), 1);
        printf(" **End**");

        draw_pf_bar();

        ISPF_GOTO(22, 1); ISPF_CLR_EOL;
        ISPF_YELLOW;
        printf("  Enter cmd (B/E/D/R) and member number (1-%d), or END:", ds->member_count);
        ISPF_NORM;

        {
            char line[80];
            char lc[4]  = "";
            int  m_idx  = 0;

            ISPF_GOTO(CMD_ROW, 1); ISPF_CLR_EOL;
            ISPF_WHITE; printf(" Command ===>"); ISPF_CYAN; printf(" "); ISPF_NORM;
            ISPF_SHOW_CUR; fflush(stdout);

            if (fgets(line, (int)sizeof(line), stdin) == NULL) line[0] = '\0';
            str_rtrim(line);
            str_upper(line);

            if (strcmp(line, "END") == 0 || strcmp(line, "=X") == 0) return;
            if (line[0] == '\0') return;

            if (isalpha((unsigned char)line[0])) {
                lc[0] = line[0]; lc[1] = '\0';
                m_idx = atoi(line + 1);
                if (line[1] == ' ') m_idx = atoi(line + 2);
            } else {
                m_idx = atoi(line);
            }

            if (m_idx < 1 || m_idx > ds->member_count) {
                set_msg("INV IDX", "Invalid member number");
                continue;
            }

            {
                ZOS_PDS_MEMBER *mem = &ds->members[m_idx - 1];

                if (strcmp(lc, "B") == 0) {
                    ispf_browse(dsn, mem->name);
                } else if (strcmp(lc, "E") == 0) {
                    ispf_edit(dsn, mem->name);
                } else if (strcmp(lc, "D") == 0) {
                    mem->record_count = 0;
                    ISPF_GOTO(22, 1); ISPF_CLR_EOL;
                    ISPF_GREEN;
                    printf("  IEC130I %s(%s) MEMBER DELETED", dsn, mem->name);
                    ISPF_NORM;
                    set_msg("DELETED", "Member deleted");
                    wait_enter("Press Enter to continue...");
                    /* Refresh right info */
                    snprintf(right_info, (int)sizeof(right_info),
                             "Row 1 of %d", ds->member_count);
                } else if (strcmp(lc, "R") == 0) {
                    char newname[DS_MEMBER_LEN + 1] = "";
                    ISPF_GOTO(22, 1); ISPF_CLR_EOL;
                    ISPF_WHITE; printf("  New member name: "); ISPF_NORM;
                    ISPF_SHOW_CUR; fflush(stdout);
                    {
                        char nl[256];
                        if (fgets(nl, (int)sizeof(nl), stdin)) {
                            str_rtrim(nl); str_upper(nl);
                            strncpy(newname, nl, DS_MEMBER_LEN);
                            newname[DS_MEMBER_LEN] = '\0';
                        }
                    }
                    ISPF_HIDE_CUR;
                    if (!validate_member_name(newname)) {
                        set_msg("INV MBR", "Invalid member name");
                    } else {
                        strncpy(mem->name, newname, DS_MEMBER_LEN);
                        mem->name[DS_MEMBER_LEN] = '\0';
                        set_msg("RENAMED", "Member renamed");
                    }
                } else {
                    set_msg("INV CMD", "Unknown line command");
                }
            }
        }
        clear_msg();
    }
}

/* =========================================================================
 * STATIC: ispf_browse — ISRBRO — browse a PS or PDS member
 * ====================================================================== */
static void ispf_browse(const char *dsn, const char *member)
{
    char title[80];
    char right_info[40] = "Line 00000 Col 001 080";

    /* Build title */
    if (member && member[0] != '\0')
        snprintf(title, (int)sizeof(title), "BROWSE %s(%s)", dsn, member);
    else
        snprintf(title, (int)sizeof(title), "BROWSE %s", dsn);

    /* Retrieve records from catalog */
    {
        ZOS_DATASET *ds = ds_catalog_find(dsn);
        int          total_recs  = 0;
        int          top_line    = 0;
        int          page_size   = BODY_BOT - BODY_TOP;  /* ~18 lines */

#define BROWSE_MAX_RECS 256
#define BROWSE_MAX_LEN  133

        static char browse_buf[BROWSE_MAX_RECS][BROWSE_MAX_LEN];
        int         browse_count = 0;

        if (ds == NULL) {
            set_msg("NOT CATG", "Data set not in catalog");
            return;
        }

        /* Load records */
        if (ds->dsorg == DSORG_PO) {
            /* PDS: find member */
            int found = 0;
            int mi;
            if (member == NULL || member[0] == '\0') {
                set_msg("NO MBR", "Member name required to browse PDS");
                return;
            }
            for (mi = 0; mi < ds->member_count; mi++) {
                if (strcmp(ds->members[mi].name, member) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                set_msg("MBR NF", "Member not found");
                return;
            }
            {
                /* Read records via pds API */
                ZOS_DCB *pf = pds_open(dsn, NULL, OPEN_INPUT);
                if (pf != NULL) {
                    if (pds_find(pf, member) == DS_OK) {
                        char rec[BROWSE_MAX_LEN];
                        int  rlen = ds->lrecl < (BROWSE_MAX_LEN - 1)
                                    ? ds->lrecl : (BROWSE_MAX_LEN - 1);
                        while (browse_count < BROWSE_MAX_RECS &
                               pds_read(pf, rec, &rlen) == DS_OK) {
                            rec[rlen] = '\0';
                            str_rtrim(rec);
                            strncpy(browse_buf[browse_count], rec,
                                    BROWSE_MAX_LEN - 1);
                            browse_buf[browse_count][BROWSE_MAX_LEN - 1] = '\0';
                            browse_count++;
                        }
                    }
                    pds_close(pf);
                }
            }
        } else {
            /* PS */
            ZOS_DCB *pf = ps_open(dsn, NULL, OPEN_INPUT, RECFM_FB, 0, 0);
            if (pf != NULL) {
                char rec[BROWSE_MAX_LEN];
                int  rlen = ds->lrecl < (BROWSE_MAX_LEN - 1)
                            ? ds->lrecl : (BROWSE_MAX_LEN - 1);
                while (browse_count < BROWSE_MAX_RECS &
                       ps_read(pf, rec, &rlen) == DS_OK) {
                    rec[rlen] = '\0';
                    str_rtrim(rec);
                    strncpy(browse_buf[browse_count], rec, BROWSE_MAX_LEN - 1);
                    browse_buf[browse_count][BROWSE_MAX_LEN - 1] = '\0';
                    browse_count++;
                }
                ps_close(pf);
            }
        }

        total_recs = browse_count;

        /* Display loop */
        for (;;) {
            int row, shown;
            snprintf(right_info, (int)sizeof(right_info),
                     "Line %05d Col 001 080", top_line + 1);

            ISPF_CLR_SCR;
            ISPF_HIDE_CUR;
            draw_header("ISRBRO", title, right_info);

            row   = BODY_TOP;
            shown = 0;

            if (total_recs == 0) {
                ISPF_GOTO(BODY_TOP, 1);
                ISPF_YELLOW;
                printf(" ****** ******* Bottom of Data *******");
                ISPF_NORM;
            } else {
                int li;
                for (li = top_line; li < total_recs && shown < page_size; li++) {
                    ISPF_GOTO(row, 1);
                    ISPF_WHITE; printf(" %06d ", li + 1); ISPF_NORM;
                    printf("%.72s", browse_buf[li]);
                    row++;
                    shown++;
                }
                if (top_line + shown >= total_recs) {
                    ISPF_GOTO(row, 1);
                    ISPF_YELLOW;
                    printf(" **End of Data**");
                    ISPF_NORM;
                }
            }

            draw_pf_bar();

            ISPF_GOTO(22, 1); ISPF_CLR_EOL;
            ISPF_YELLOW;
            printf("  PF3=End  PF7=Up  PF8=Down");
            ISPF_NORM;

            ISPF_GOTO(CMD_ROW, 1); ISPF_CLR_EOL;
            ISPF_WHITE; printf(" Command ===>"); ISPF_CYAN; printf(" "); ISPF_NORM;
            ISPF_SHOW_CUR; fflush(stdout);

            {
                char cmd[80];
                if (fgets(cmd, (int)sizeof(cmd), stdin) == NULL) break;
                str_rtrim(cmd);
                str_upper(cmd);

                if (strcmp(cmd, "END") == 0 || strcmp(cmd, "=X") == 0 ||
                    strcmp(cmd, "F3")  == 0) {
                    break;
                } else if (strcmp(cmd, "F7") == 0 || strcmp(cmd, "UP") == 0) {
                    top_line -= page_size;
                    if (top_line < 0) {
                        top_line = 0;
                        set_msg("TOP", "Already at top of data");
                    }
                } else if (strcmp(cmd, "F8") == 0 || strcmp(cmd, "DOWN") == 0) {
                    if (top_line + page_size >= total_recs) {
                        set_msg("BOTTOM", "Already at bottom of data");
                    } else {
                        top_line += page_size;
                    }
                }
                /* blank → scroll down one page */
                else if (cmd[0] == '\0') {
                    if (top_line + page_size < total_recs) {
                        top_line += page_size;
                    } else {
                        break;
                    }
                }
            }
            clear_msg();
        }

#undef BROWSE_MAX_RECS
#undef BROWSE_MAX_LEN
    }
}

/* =========================================================================
 * STATIC: ispf_edit — ISREDIT — edit a PS or PDS member
 * ====================================================================== */
static void ispf_edit(const char *dsn, const char *member)
{
    char title[80];
    ZOS_DATASET *ds;

#define EDIT_MAX_RECS 256
#define EDIT_MAX_LEN  133

    static char edit_buf[EDIT_MAX_RECS][EDIT_MAX_LEN];
    int         edit_count = 0;
    int         i;

    if (member && member[0] != '\0')
        snprintf(title, (int)sizeof(title), "EDIT %s(%s)", dsn, member);
    else
        snprintf(title, (int)sizeof(title), "EDIT %s", dsn);

    ds = ds_catalog_find(dsn);
    if (ds == NULL) {
        set_msg("NOT CATG", "Data set not in catalog");
        return;
    }

    /* Load existing content */
    if (ds->dsorg == DSORG_PO) {
        if (member == NULL || member[0] == '\0') {
            set_msg("NO MBR", "Member name required to edit PDS");
            return;
        }
        {
            ZOS_DCB *pf = pds_open(dsn, NULL, OPEN_INPUT);
            if (pf != NULL) {
                if (pds_find(pf, member) == DS_OK) {
                    char rec[EDIT_MAX_LEN];
                    int  rlen = ds->lrecl < (EDIT_MAX_LEN - 1)
                                ? ds->lrecl : (EDIT_MAX_LEN - 1);
                    while (edit_count < EDIT_MAX_RECS &
                           pds_read(pf, rec, &rlen) == DS_OK) {
                        rec[rlen] = '\0';
                        str_rtrim(rec);
                        strncpy(edit_buf[edit_count], rec, EDIT_MAX_LEN - 1);
                        edit_buf[edit_count][EDIT_MAX_LEN - 1] = '\0';
                        edit_count++;
                    }
                }
                pds_close(pf);
            }
        }
    } else {
        ZOS_DCB *pf = ps_open(dsn, NULL, OPEN_INPUT, RECFM_FB, 0, 0);
        if (pf != NULL) {
            char rec[EDIT_MAX_LEN];
            int  rlen = ds->lrecl < (EDIT_MAX_LEN - 1)
                        ? ds->lrecl : (EDIT_MAX_LEN - 1);
            while (edit_count < EDIT_MAX_RECS &
                   ps_read(pf, rec, &rlen) == DS_OK) {
                rec[rlen] = '\0';
                str_rtrim(rec);
                strncpy(edit_buf[edit_count], rec, EDIT_MAX_LEN - 1);
                edit_buf[edit_count][EDIT_MAX_LEN - 1] = '\0';
                edit_count++;
            }
            ps_close(pf);
        }
    }

    /* Edit loop */
    for (;;) {
        int row, shown, top_line = 0, page_size;

        page_size = BODY_BOT - BODY_TOP - 2;

        ISPF_CLR_SCR;
        ISPF_HIDE_CUR;
        draw_header("ISREDIT", title, "Columns 00001 00072");

        ISPF_GOTO(BODY_TOP, 1);
        ISPF_BOLD;
        printf(" ****** ************************** Top of Data **************************");
        ISPF_NORM;

        row   = BODY_TOP + 1;
        shown = 0;

        for (i = top_line; i < edit_count && shown < page_size; i++) {
            ISPF_GOTO(row, 1);
            ISPF_WHITE; printf(" %06d ", i + 1); ISPF_NORM;
            printf("%.72s", edit_buf[i]);
            row++;
            shown++;
        }

        /* Input line for new record */
        if (edit_count < EDIT_MAX_RECS && row <= BODY_BOT) {
            ISPF_GOTO(row, 1);
            ISPF_WHITE; printf(" %06d ", edit_count + 1); ISPF_NORM;
            ISPF_CYAN;  printf("_"); ISPF_NORM;
            row++;
        }

        if (row <= BODY_BOT) {
            ISPF_GOTO(row, 1);
            ISPF_BOLD;
            printf(" ****** ************************** Bottom of Data **********************");
            ISPF_NORM;
        }

        draw_pf_bar();

        ISPF_GOTO(22, 1); ISPF_CLR_EOL;
        ISPF_YELLOW;
        printf("  Type line and Enter to add, blank line to stop. PF3=Save and End");
        ISPF_NORM;

        ISPF_GOTO(CMD_ROW, 1); ISPF_CLR_EOL;
        ISPF_WHITE; printf(" Command ===>"); ISPF_CYAN; printf(" "); ISPF_NORM;
        ISPF_SHOW_CUR; fflush(stdout);

        {
            char cmd[80];
            if (fgets(cmd, (int)sizeof(cmd), stdin) == NULL) break;
            str_rtrim(cmd);

            if (strcmp(cmd, "END")      == 0 ||
                strcmp(cmd, "end")      == 0 ||
                strcmp(cmd, "F3")       == 0 ||
                strcmp(cmd, "=X")       == 0) {
                /* Save */
                goto save_and_exit;
            }

            str_upper(cmd);
            if (cmd[0] == '\0') goto save_and_exit;

            /* Treat as new record text */
            if (edit_count < EDIT_MAX_RECS) {
                strncpy(edit_buf[edit_count], cmd, EDIT_MAX_LEN - 1);
                edit_buf[edit_count][EDIT_MAX_LEN - 1] = '\0';
                edit_count++;
            } else {
                set_msg("BUF FUL", "Edit buffer full");
            }
        }
        clear_msg();
        continue;

save_and_exit:
        /* Write records back */
        {
            int save_ok = 0;

            if (ds->dsorg == DSORG_PO) {
                if (member && member[0] != '\0') {
                    ZOS_DCB *pf = pds_open(dsn, NULL, OPEN_OUTPUT);
                    if (pf != NULL) {
                        int rlen = ds->lrecl < (EDIT_MAX_LEN - 1)
                                   ? ds->lrecl : (EDIT_MAX_LEN - 1);
                        int ok = 1;
                        for (i = 0; i < edit_count && ok; i++) {
                            char padded[EDIT_MAX_LEN];
                            memset(padded, ' ', (size_t)rlen);
                            padded[rlen] = '\0';
                            {
                                int sl = (int)strlen(edit_buf[i]);
                                int cp = sl < rlen ? sl : rlen;
                                memcpy(padded, edit_buf[i], (size_t)cp);
                            }
                            if (pds_write(pf, padded, rlen) != DS_OK)
                                ok = 0;
                        }
                        if (ok) pds_stow(pf, member);
                        pds_close(pf);
                        save_ok = ok;
                    }
                }
            } else {
                ZOS_DCB *pf = ps_open(dsn, NULL, OPEN_OUTPUT, RECFM_FB, 0, 0);
                if (pf != NULL) {
                    int rlen = ds->lrecl < (EDIT_MAX_LEN - 1)
                               ? ds->lrecl : (EDIT_MAX_LEN - 1);
                    int ok = 1;
                    for (i = 0; i < edit_count && ok; i++) {
                        char padded[EDIT_MAX_LEN];
                        memset(padded, ' ', (size_t)rlen);
                        padded[rlen] = '\0';
                        {
                            int sl = (int)strlen(edit_buf[i]);
                            int cp = sl < rlen ? sl : rlen;
                            memcpy(padded, edit_buf[i], (size_t)cp);
                        }
                        if (ps_write(pf, padded, rlen) != DS_OK)
                            ok = 0;
                    }
                    ps_close(pf);
                    save_ok = ok;
                }
            }

            ISPF_GOTO(22, 1); ISPF_CLR_EOL;
            if (save_ok) {
                ISPF_GREEN;
                printf("  MEMBER SAVED - %d record(s) written", edit_count);
                ISPF_NORM;
                set_msg("SAVED", "Member saved");
            } else {
                ISPF_YELLOW;
                printf("  SAVE FAILED - check data set and member");
                ISPF_NORM;
                set_msg("SAVFAIL", "Save failed");
            }
            wait_enter("Press Enter to continue...");
        }
        break;
    }

#undef EDIT_MAX_RECS
#undef EDIT_MAX_LEN
}
