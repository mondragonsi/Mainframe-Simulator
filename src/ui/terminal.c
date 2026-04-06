/*
 * IMS Terminal Interface Implementation
 * 
 * Mainframe-like terminal for IMS DBA operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "terminal.h"
#include "../zos/as_monitor.h"
#include "../zos/address_space.h"
#include "../datasets/datasets.h"
#include "ispf.h"

/* =========================================================================
 * PF KEY HANDLING  (ISPF standard: PF1=HELP PF3=END PF7=UP PF8=DOWN PF12=CANCEL)
 * ========================================================================= */
typedef enum {
    PF_NONE=0,PF1,PF2,PF3,PF4,PF5,PF6,PF7,PF8,PF9,PF10,PF11,PF12,
    PF13,PF14,PF15,PF16,PF17,PF18,PF19,PF20,PF21,PF22,PF23,PF24
} PF_KEY;

static PF_KEY term_read_input(char *buf, int maxlen) {
    if (!fgets(buf,maxlen,stdin)) { buf[0]=0; return PF_NONE; }
    int n=(int)strlen(buf);
    while(n>0&&(buf[n-1]==10||buf[n-1]==13)) buf[--n]=0;
    if (n>=3 && buf[0]==27) {
        static const struct { const char *seq; PF_KEY k; } map[]={
            {"[11~",PF1},{"[12~",PF2},{"[13~",PF3},{"[14~",PF4},
            {"[15~",PF5},{"[17~",PF6},{"[18~",PF7},{"[19~",PF8},
            {"[20~",PF9},{"[21~",PF10},{"[23~",PF11},{"[24~",PF12},
            {"OP",PF1},{"OQ",PF2},{"OR",PF3},{"OS",PF4},
            {NULL,PF_NONE}
        };
        for(int i=0;map[i].seq;i++)
            if(strcmp(buf,map[i].seq)==0){buf[0]=0;return map[i].k;}
        buf[0]=0; return PF_NONE;
    }
    return PF_NONE;
}

static void term_pf_bar(void) {
    printf("%s F1=Help  F3=End   F7=Up    F8=Down  F12=Cancel                              %s\n",
           TERM_REVERSE, TERM_RESET);
}


/* Global terminal context */
static IMS_TERMINAL terminal;

int terminal_init(void) {
    memset(&terminal, 0, sizeof(IMS_TERMINAL));
    terminal.mode = TERM_MODE_MAIN;
    return 0;
}

void terminal_shutdown(void) {
    terminal_clear();
}

void terminal_clear(void) {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[2J\033[H");
#endif
}

void terminal_header(const char *title) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date_str[12], time_str[10];
    strftime(date_str, 12, "%Y/%m/%d", tm_info);
    strftime(time_str, 10, "%H:%M:%S", tm_info);
    
    printf("%s", TERM_REVERSE);
    printf(" IMS Simulator %s", IMS_VERSION);
    
    /* Pad to fill the line */
    int title_len = title ? strlen(title) : 0;
    int pad = TERM_COLS - 25 - title_len - 20;
    for (int i = 0; i < pad; i++) printf(" ");
    
    if (title) {
        printf("%s%s%s", TERM_BOLD, title, TERM_RESET TERM_REVERSE);
    }
    
    printf("  %s %s ", date_str, time_str);
    printf("%s\n", TERM_RESET);
    
    /* System ID and status line */
    printf("%s", TERM_CYAN);
    printf(" System: %-4s  ", ims_system.imsid);
    printf("DB: %-8s  ", terminal.current_db[0] ? terminal.current_db : "--------");
    printf("PSB: %-8s  ", terminal.current_psb[0] ? terminal.current_psb : "--------");
    printf("Mode: %-8s", 
           terminal.mode == TERM_MODE_MAIN ? "MAIN" :
           terminal.mode == TERM_MODE_DB ? "DATABASE" :
           terminal.mode == TERM_MODE_TM ? "TM" :
           terminal.mode == TERM_MODE_DLI ? "DL/I" :
           terminal.mode == TERM_MODE_DISPLAY ? "DISPLAY" : "HELP");
    printf("%s\n", TERM_RESET);
    
    printf("--------------------------------------------------------------------------------\n");
}

void terminal_footer(void) {
    printf("--------------------------------------------------------------------------------\n");
    
    if (terminal.show_message) {
        printf("%s%s%s\n", TERM_YELLOW, terminal.message, TERM_RESET);
        terminal.show_message = false;
    } else {
        printf("\n");
    }
    
    printf(" Enter command (or HELP for assistance): ");
}

void terminal_message(const char *msg) {
    strncpy(terminal.message, msg, 255);
    terminal.show_message = true;
}

void terminal_error(const char *msg) {
    printf("%s*** ERROR: %s%s\n", TERM_RED, msg, TERM_RESET);
}

void terminal_main_menu(void) {
    terminal_clear();
    terminal_header("MAIN MENU");
    
    printf("\n");
    printf("  %sIMS Simulator - Main Menu%s\n", TERM_BOLD, TERM_RESET);
    printf("\n");
    printf("  Commands:\n");
    printf("\n");
    printf("   %s/DB%s      - Database Manager (IMS DB)\n", TERM_GREEN, TERM_RESET);
    printf("            - View and manage database definitions\n");
    printf("\n");
    printf("   %s/TM%s      - Transaction Manager (IMS TM)\n", TERM_GREEN, TERM_RESET);
    printf("            - Manage regions, queues, and transactions\n");
    printf("\n");
    printf("   %s/DLI%s     - DL/I Call Interface\n", TERM_GREEN, TERM_RESET);
    printf("            - Execute DL/I calls interactively\n");
    printf("\n");
    printf("   %s/ZOS%s     - z/OS Address Space Monitor\n", TERM_GREEN, TERM_RESET);
    printf("            - View address spaces, architecture, JES2 spool\n");
    printf("\n");
    printf("   %s/DS%s      - DFSMS Dataset Manager\n", TERM_GREEN, TERM_RESET);
    printf("            - ICF catalog, IDCAMS, LISTCAT, BROWSE datasets\n");
    printf("\n");
    printf("   %s/DISPLAY%s - Display System Status\n", TERM_GREEN, TERM_RESET);
    printf("            - View current system state\n");
    printf("\n");
    printf("   %s/LOAD%s    - Load Sample Database\n", TERM_GREEN, TERM_RESET);
    printf("            - Load HOSPITAL sample database\n");
    printf("\n");
    printf("   %s/HELP%s    - Help and Documentation\n", TERM_GREEN, TERM_RESET);
    printf("\n");
    printf("   %s/END%s     - Exit IMS Simulator\n", TERM_GREEN, TERM_RESET);
    printf("\n");
    
    terminal_footer();
}

void terminal_display_databases(void) {
    terminal_clear();
    terminal_header("DATABASE MANAGER");
    
    printf("\n  %sRegistered Databases%s\n\n", TERM_BOLD, TERM_RESET);
    
    if (ims_system.dbd_count == 0) {
        printf("  (No databases registered. Use /LOAD to load sample database)\n");
    } else {
        printf("  %-10s %-8s %-8s %-30s\n", "DBD NAME", "ACCESS", "SEGS", "ROOT SEGMENT");
        printf("  ---------- -------- -------- ------------------------------\n");
        
        for (int i = 0; i < ims_system.dbd_count; i++) {
            IMS_DBD *dbd = ims_system.dbds[i];
            printf("  %-10s %-8s %-8d %-30s\n",
                   dbd->name,
                   dbd->access_method,
                   dbd->segment_count,
                   dbd->root ? dbd->root->name : "");
        }
    }
    
    printf("\n");
    printf("  Commands:\n");
    printf("    /SELECT dbname  - Select database for operations\n");
    printf("    /SHOW dbname    - Show database structure\n");
    printf("    /BACK           - Return to main menu\n");
    printf("\n");
    
    terminal_footer();
}

void terminal_display_transactions(void) {
    printf("\n  %sRegistered Transactions%s\n\n", TERM_BOLD, TERM_RESET);
    
    if (ims_system.transaction_count == 0) {
        printf("  (No transactions defined)\n");
    } else {
        printf("  %-10s %-10s %-8s %-6s %-10s\n", 
               "TXN CODE", "PSB", "REGION", "CONV", "PRIORITY");
        printf("  ---------- ---------- -------- ------ ----------\n");
        
        for (int i = 0; i < ims_system.transaction_count; i++) {
            IMS_TRANSACTION_DEF *txn = &ims_system.transactions[i];
            printf("  %-10s %-10s %-8s %-6s %-10d\n",
                   txn->code,
                   txn->psb_name,
                   txn->region_type == REGION_MPP ? "MPP" : "BMP",
                   txn->is_conversational ? "YES" : "NO",
                   txn->priority);
        }
    }
}

void terminal_display_queues(void) {
    printf("\n  %sMessage Queues%s\n\n", TERM_BOLD, TERM_RESET);
    
    printf("  Queue        Count\n");
    printf("  ------------ -----\n");
    printf("  INPUT        %d\n", ims_system.input_queue.count);
    printf("  OUTPUT       %d\n", ims_system.output_queue.count);
    
    if (ims_system.input_queue.count > 0) {
        printf("\n  Input Queue Messages:\n");
        printf("  %-6s %-10s %-10s %-6s\n", "ID", "TXN", "LTERM", "SEGS");
        printf("  ------ ---------- ---------- ------\n");
        
        IMS_MESSAGE *msg = ims_system.input_queue.head;
        int count = 0;
        while (msg && count < 10) {
            printf("  %-6d %-10s %-10s %-6d\n",
                   msg->id, msg->transaction_code, msg->lterm, msg->segment_count);
            msg = msg->next;
            count++;
        }
        if (ims_system.input_queue.count > 10) {
            printf("  ... and %d more\n", ims_system.input_queue.count - 10);
        }
    }
}

void terminal_display_regions(void) {
    printf("\n  %sActive Regions%s\n\n", TERM_BOLD, TERM_RESET);
    
    printf("  %-10s %-8s %-12s %-10s\n", "NAME", "TYPE", "STATE", "PSB");
    printf("  ---------- -------- ------------ ----------\n");
    
    for (int i = 0; i < ims_system.region_count; i++) {
        IMS_REGION *region = &ims_system.regions[i];
        printf("  %-10s %-8s %-12s %-10s\n",
               region->name,
               region->type == REGION_MPP ? "MPP" : "BMP",
               region->is_active ? "ACTIVE" : "INACTIVE",
               region->current_psb ? region->current_psb->name : "--------");
    }
    
    if (ims_system.region_count == 0) {
        printf("  (No active regions)\n");
    }
}

void terminal_display_psbs(void) {
    printf("\n  %sRegistered PSBs%s\n\n", TERM_BOLD, TERM_RESET);
    
    if (ims_system.psb_count == 0) {
        printf("  (No PSBs registered)\n");
    } else {
        printf("  %-10s %-6s %-8s %-10s\n", "PSB NAME", "LANG", "PCBs", "CONV");
        printf("  ---------- ------ -------- ----------\n");
        
        for (int i = 0; i < ims_system.psb_count; i++) {
            IMS_PSB *psb = ims_system.psbs[i];
            printf("  %-10s %-6c %-8d %-10s\n",
                   psb->name,
                   psb->language,
                   psb->pcb_count,
                   psb->is_conversational ? "YES" : "NO");
        }
    }
}

void terminal_dli_panel(void) {
    terminal_clear();
    terminal_header("DL/I CALL INTERFACE");
    
    printf("\n");
    printf("  %sDL/I Call Syntax:%s\n", TERM_BOLD, TERM_RESET);
    printf("\n");
    printf("  Get Calls:\n");
    printf("    GU  segname [(field=value)]  - Get Unique\n");
    printf("    GN  [segname]                - Get Next\n");
    printf("    GNP [segname]                - Get Next in Parent\n");
    printf("\n");
    printf("  Update Calls:\n");
    printf("    ISRT segname                 - Insert segment\n");
    printf("    DLET                         - Delete current segment\n");
    printf("    REPL                         - Replace current segment\n");
    printf("\n");
    printf("  System Calls:\n");
    printf("    PCB psbname                  - Schedule PSB\n");
    printf("    TERM                         - Terminate PSB\n");
    printf("\n");
    printf("  %sExamples:%s\n", TERM_BOLD, TERM_RESET);
    printf("    GU HOSPITAL (HOSPNAME=SANTA CASA)\n");
    printf("    GN WARD\n");
    printf("    GNP PATIENT\n");
    printf("\n");
    printf("  Type /BACK to return to main menu\n");
    printf("\n");
    
    if (terminal.current_pcb) {
        printf("  %sCurrent PCB Status:%s\n", TERM_BOLD, TERM_RESET);
        printf("    DB:      %s\n", terminal.current_pcb->db_name);
        printf("    Status:  %s\n", IMS_STATUS_STR(terminal.current_pcb->status_code));
        printf("    Segment: %s\n", terminal.current_pcb->segment_name);
        printf("\n");
    }
    
    terminal_footer();
}

void terminal_help(void) {
    terminal_clear();
    terminal_header("HELP");
    
    printf("\n");
    printf("  %sIMS Simulator Help%s\n", TERM_BOLD, TERM_RESET);
    printf("\n");
    printf("  This simulator provides a learning environment for IBM IMS.\n");
    printf("  It simulates both IMS DB (Database Manager) and IMS TM\n");
    printf("  (Transaction Manager) functionality.\n");
    printf("\n");
    printf("  %sNavigation:%s\n", TERM_BOLD, TERM_RESET);
    printf("    /MAIN, /BACK  - Return to main menu\n");
    printf("    /END, EXIT    - Exit simulator\n");
    printf("\n");
    printf("  %sDatabase Commands:%s\n", TERM_BOLD, TERM_RESET);
    printf("    /DB           - Database manager panel\n");
    printf("    /LOAD         - Load sample HOSPITAL database\n");
    printf("    /SHOW db      - Show database structure\n");
    printf("\n");
    printf("  %sTransaction Commands:%s\n", TERM_BOLD, TERM_RESET);
    printf("    /TM           - Transaction manager panel\n");
    printf("    /QUEUE        - Show message queues\n");
    printf("    /REGION       - Show active regions\n");
    printf("\n");
    printf("  %sDL/I Commands:%s\n", TERM_BOLD, TERM_RESET);
    printf("    /DLI          - DL/I call interface\n");
    printf("    GU, GN, GNP   - Get calls\n");
    printf("    ISRT, DLET, REPL - Update calls\n");
    printf("\n");
    printf("  Press ENTER to return...\n");
    
    terminal_footer();
}

int terminal_get_input(char *buffer, int max_length) {
    if (!fgets(buffer, max_length, stdin)) {
        return -1;
    }
    
    /* Remove newline */
    int len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
        len--;
    }
    
    /* Convert to uppercase */
    for (int i = 0; i < len; i++) {
        buffer[i] = toupper((unsigned char)buffer[i]);
    }
    
    strncpy(terminal.last_command, buffer, 255);
    return len;
}

int terminal_process_command(const char *command) {
    if (!command || !command[0]) {
        return 0;
    }
    
    /* Navigation commands */
    if (strcmp(command, "/END") == 0 || strcmp(command, "EXIT") == 0 ||
        strcmp(command, "/EXIT") == 0 || strcmp(command, "QUIT") == 0) {
        return -1;  /* Exit */
    }
    
    if (strcmp(command, "/MAIN") == 0 || strcmp(command, "/BACK") == 0) {
        terminal.mode = TERM_MODE_MAIN;
        return 0;
    }
    
    if (strcmp(command, "/DB") == 0) {
        terminal.mode = TERM_MODE_DB;
        return 0;
    }
    
    if (strcmp(command, "/TM") == 0) {
        terminal.mode = TERM_MODE_TM;
        return 0;
    }
    
    if (strcmp(command, "/DLI") == 0) {
        terminal.mode = TERM_MODE_DLI;
        return 0;
    }
    
    if (strcmp(command, "/DISPLAY") == 0 || strcmp(command, "/STATUS") == 0) {
        terminal.mode = TERM_MODE_DISPLAY;
        return 0;
    }
    
    if (strcmp(command, "/HELP") == 0 || strcmp(command, "HELP") == 0) {
        terminal.mode = TERM_MODE_HELP;
        return 0;
    }

    if (strcmp(command, "/ZOS") == 0 || strcmp(command, "/AS") == 0) {
        as_monitor_run();
        return 0;
    }

    if (strcmp(command, "/DS") == 0 || strcmp(command, "/DATASETS") == 0) {
        ispf_utilities_menu();
        return 0;
    }
    
    if (strcmp(command, "/LOAD") == 0) {
        terminal_message("Loading HOSPITAL sample database...");
        /* This will be implemented in database.c */
        extern int load_hospital_database(void);
        if (load_hospital_database() == 0) {
            terminal_message("HOSPITAL database loaded successfully");
        } else {
            terminal_message("Failed to load HOSPITAL database");
        }
        return 0;
    }
    
    if (strcmp(command, "/QUEUE") == 0) {
        terminal_display_queues();
        return 0;
    }
    
    if (strcmp(command, "/REGION") == 0) {
        terminal_display_regions();
        return 0;
    }
    
    /* DL/I commands when in DL/I mode */
    if (terminal.mode == TERM_MODE_DLI) {
        return terminal_execute_dli(command);
    }
    
    terminal_message("Unknown command. Type /HELP for assistance.");
    return 0;
}

int terminal_execute_dli(const char *command) {
    /* Parse and execute DL/I command */
    char func[10] = {0}, arg1[32] = {0}, arg2[128] = {0};
    int parsed = sscanf(command, "%9s %31s %127[^\n]", func, arg1, arg2);
    
    if (parsed < 1) {
        return 0;
    }
    
    /* Static IO area and PSB/PCB pointers */
    static char io_area[1024];
    static IMS_PSB *current_psb = NULL;
    static IMS_PCB *current_pcb = NULL;
    
    /* PCB - Schedule PSB */
    if (strcmp(func, "PCB") == 0 && parsed >= 2) {
        if (dli_pcb(arg1, &current_psb) == 0) {
            strncpy(terminal.current_psb, arg1, 8);
            /* Get the DB PCB from the PSB */
            if (current_psb && current_psb->db_pcbs[0]) {
                current_pcb = current_psb->db_pcbs[0];
                terminal.current_pcb = current_pcb;
                strncpy(terminal.current_db, current_pcb->db_name, 8);
            }
            terminal_message("PSB scheduled successfully");
        } else {
            terminal_message("ERROR: PSB not found");
        }
        return 0;
    }
    
    /* Check if PSB is scheduled */
    if (!current_psb || !current_pcb) {
        /* Auto-schedule HOSPPGM if not done */
        if (ims_system.psb_count > 0) {
            dli_pcb("HOSPPGM", &current_psb);
            if (current_psb && current_psb->db_pcbs[0]) {
                current_pcb = current_psb->db_pcbs[0];
                terminal.current_pcb = current_pcb;
                strncpy(terminal.current_db, current_pcb->db_name, 8);
                strncpy(terminal.current_psb, current_psb->name, 8);
            }
        }
        
        if (!current_pcb) {
            terminal_message("ERROR: No PSB scheduled. Use /LOAD first, then PCB HOSPPGM");
            return 0;
        }
    }
    
    /* GU - Get Unique */
    if (strcmp(func, "GU") == 0) {
        memset(io_area, 0, sizeof(io_area));
        
        /* Parse SSA from arg1 if provided */
        IMS_SSA ssa = {0};
        IMS_SSA *ssa_list[1] = {NULL};
        int ssa_count = 0;
        
        if (parsed >= 2 && arg1[0]) {
            /* Build SSA string from arg1 and arg2 */
            char ssa_string[200];
            if (parsed >= 3 && arg2[0]) {
                snprintf(ssa_string, sizeof(ssa_string), "%s %s", arg1, arg2);
            } else {
                strncpy(ssa_string, arg1, sizeof(ssa_string));
            }
            ssa_parse(ssa_string, &ssa);
            ssa_list[0] = &ssa;
            ssa_count = 1;
        }
        
        int rc = dli_gu(current_pcb, io_area, ssa_list, ssa_count);
        
        printf("\n  %s=== GU Result ===%s\n", TERM_GREEN, TERM_RESET);
        printf("  Status: %s (%s)\n", 
               IMS_STATUS_STR(current_pcb->status_code),
               ims_status_desc(current_pcb->status_code));
        printf("  Segment: %s (Level %d)\n", 
               current_pcb->segment_name, current_pcb->level);
        
        if (rc == 0) {
            /* Display segment data based on segment name */
            printf("\n  %sData:%s\n", TERM_BOLD, TERM_RESET);
            display_segment_data(current_pcb->segment_name, io_area);
        }
        printf("\n  Press ENTER to continue...\n");
        getchar();
        return 0;
    }
    
    /* GN - Get Next */
    if (strcmp(func, "GN") == 0) {
        memset(io_area, 0, sizeof(io_area));
        
        IMS_SSA ssa = {0};
        IMS_SSA *ssa_list[1] = {NULL};
        int ssa_count = 0;
        
        if (parsed >= 2 && arg1[0]) {
            ssa_parse(arg1, &ssa);
            ssa_list[0] = &ssa;
            ssa_count = 1;
        }
        
        int rc = dli_gn(current_pcb, io_area, ssa_list, ssa_count);
        
        printf("\n  %s=== GN Result ===%s\n", TERM_GREEN, TERM_RESET);
        printf("  Status: %s (%s)\n",
               IMS_STATUS_STR(current_pcb->status_code),
               ims_status_desc(current_pcb->status_code));
        printf("  Segment: %s (Level %d)\n",
               current_pcb->segment_name, current_pcb->level);
        
        if (rc == 0) {
            printf("\n  %sData:%s\n", TERM_BOLD, TERM_RESET);
            display_segment_data(current_pcb->segment_name, io_area);
        }
        printf("\n  Press ENTER to continue...\n");
        getchar();
        return 0;
    }
    
    /* GNP - Get Next in Parent */
    if (strcmp(func, "GNP") == 0) {
        memset(io_area, 0, sizeof(io_area));
        
        IMS_SSA ssa = {0};
        IMS_SSA *ssa_list[1] = {NULL};
        int ssa_count = 0;
        
        if (parsed >= 2 && arg1[0]) {
            ssa_parse(arg1, &ssa);
            ssa_list[0] = &ssa;
            ssa_count = 1;
        }
        
        int rc = dli_gnp(current_pcb, io_area, ssa_list, ssa_count);
        
        printf("\n  %s=== GNP Result ===%s\n", TERM_GREEN, TERM_RESET);
        printf("  Status: %s (%s)\n",
               IMS_STATUS_STR(current_pcb->status_code),
               ims_status_desc(current_pcb->status_code));
        printf("  Segment: %s (Level %d)\n",
               current_pcb->segment_name, current_pcb->level);
        
        if (rc == 0) {
            printf("\n  %sData:%s\n", TERM_BOLD, TERM_RESET);
            display_segment_data(current_pcb->segment_name, io_area);
        }
        printf("\n  Press ENTER to continue...\n");
        getchar();
        return 0;
    }
    
    if (strcmp(func, "ISRT") == 0) {
        terminal_message("ISRT: Insert not yet implemented interactively");
        return 0;
    }
    
    if (strcmp(func, "DLET") == 0) {
        terminal_message("DLET: Delete not yet implemented interactively");
        return 0;
    }
    
    if (strcmp(func, "REPL") == 0) {
        terminal_message("REPL: Replace not yet implemented interactively");
        return 0;
    }
    
    terminal_message("Unknown DL/I function. Valid: GU, GN, GNP, PCB");
    return 0;
}

/* Helper to display segment data in a formatted way */
void display_segment_data(const char *segment_name, const char *data) {
    if (strcmp(segment_name, "HOSPITAL") == 0) {
        char hospcode[5] = {0}, hospname[31] = {0}, hospaddr[51] = {0}, hospphon[16] = {0};
        memcpy(hospcode, data + 0, 4);
        memcpy(hospname, data + 4, 30);
        memcpy(hospaddr, data + 34, 50);
        memcpy(hospphon, data + 84, 15);
        
        printf("    %-12s: '%s'\n", "HOSPCODE", hospcode);
        printf("    %-12s: '%s'\n", "HOSPNAME", hospname);
        printf("    %-12s: '%s'\n", "HOSPADDR", hospaddr);
        printf("    %-12s: '%s'\n", "HOSPPHON", hospphon);
    }
    else if (strcmp(segment_name, "WARD") == 0) {
        char wardno[5] = {0}, wardname[21] = {0}, wardtype[11] = {0}, numbeds[5] = {0};
        memcpy(wardno, data + 0, 4);
        memcpy(wardname, data + 4, 20);
        memcpy(wardtype, data + 24, 10);
        memcpy(numbeds, data + 34, 4);
        
        printf("    %-12s: '%s'\n", "WARDNO", wardno);
        printf("    %-12s: '%s'\n", "WARDNAME", wardname);
        printf("    %-12s: '%s'\n", "WARDTYPE", wardtype);
        printf("    %-12s: '%s'\n", "NUMBEDS", numbeds);
    }
    else if (strcmp(segment_name, "PATIENT") == 0) {
        char patno[7] = {0}, patname[31] = {0}, pataddr[51] = {0};
        char datadmit[11] = {0}, patdoc[21] = {0};
        memcpy(patno, data + 0, 6);
        memcpy(patname, data + 6, 30);
        memcpy(pataddr, data + 36, 50);
        memcpy(datadmit, data + 86, 10);
        memcpy(patdoc, data + 96, 20);
        
        printf("    %-12s: '%s'\n", "PATNO", patno);
        printf("    %-12s: '%s'\n", "PATNAME", patname);
        printf("    %-12s: '%s'\n", "PATADDR", pataddr);
        printf("    %-12s: '%s'\n", "DATADMIT", datadmit);
        printf("    %-12s: '%s'\n", "PATDOC", patdoc);
    }
    else if (strcmp(segment_name, "TREATMNT") == 0) {
        char treatno[5] = {0}, treatdat[11] = {0}, treattyp[21] = {0}, treatmed[31] = {0};
        memcpy(treatno, data + 0, 4);
        memcpy(treatdat, data + 4, 10);
        memcpy(treattyp, data + 14, 20);
        memcpy(treatmed, data + 34, 30);
        
        printf("    %-12s: '%s'\n", "TREATNO", treatno);
        printf("    %-12s: '%s'\n", "TREATDAT", treatdat);
        printf("    %-12s: '%s'\n", "TREATTYP", treattyp);
        printf("    %-12s: '%s'\n", "TREATMED", treatmed);
    }
    else if (strcmp(segment_name, "DOCTOR") == 0) {
        char docno[5] = {0}, docname[31] = {0}, docspec[21] = {0};
        memcpy(docno, data + 0, 4);
        memcpy(docname, data + 4, 30);
        memcpy(docspec, data + 34, 20);
        
        printf("    %-12s: '%s'\n", "DOCNO", docno);
        printf("    %-12s: '%s'\n", "DOCNAME", docname);
        printf("    %-12s: '%s'\n", "DOCSPEC", docspec);
    }
    else if (strcmp(segment_name, "FACILITY") == 0) {
        char faccode[5] = {0}, facname[31] = {0}, factype[21] = {0};
        memcpy(faccode, data + 0, 4);
        memcpy(facname, data + 4, 30);
        memcpy(factype, data + 34, 20);
        
        printf("    %-12s: '%s'\n", "FACCODE", faccode);
        printf("    %-12s: '%s'\n", "FACNAME", facname);
        printf("    %-12s: '%s'\n", "FACTYPE", factype);
    }
    else {
        /* Generic hex dump for unknown segments */
        printf("    (Raw data: first 40 bytes)\n    ");
        for (int i = 0; i < 40; i++) {
            printf("%02X ", (unsigned char)data[i]);
            if ((i + 1) % 20 == 0) printf("\n    ");
        }
        printf("\n");
    }
}

/* =========================================================================
 * /DS PANEL â€” Interactive DFSMS Dataset Manager
 *
 * Presents an ISPF-like interface for dataset operations:
 *   LISTCAT â€” list catalog entries
 *   IDCAMS  â€” run IDCAMS commands
 *   BROWSE  â€” view PS/PDS dataset contents
 * ========================================================================= */

/* =========================================================================
 * DS PANEL HELPERS — parse DSN(MEMBER), validate member names
 * ========================================================================= */

/* Parse 'MY.PDS(MEMBER1)' into dsn + member.
 * member[] is empty string if no member specified. */
static int ds_parse_dsn_member(const char *token,
                               char *dsn, char *member) {
    const char *lp = strchr(token, '(');
    if (lp) {
        int dlen = (int)(lp - token);
        if (dlen > DS_DSN_LEN) dlen = DS_DSN_LEN;
        strncpy(dsn, token, (size_t)dlen);
        dsn[dlen] = '\0';
        const char *rp = strchr(lp + 1, ')');
        int mlen = rp ? (int)(rp - lp - 1) : (int)strlen(lp + 1);
        if (mlen > DS_MEMBER_LEN) mlen = DS_MEMBER_LEN;
        strncpy(member, lp + 1, (size_t)mlen);
        member[mlen] = '\0';
        for (int i = 0; member[i]; i++)
            member[i] = (char)toupper((unsigned char)member[i]);
    } else {
        strncpy(dsn, token, DS_DSN_LEN);
        dsn[DS_DSN_LEN] = '\0';
        member[0] = '\0';
    }
    for (int i = 0; dsn[i]; i++)
        dsn[i] = (char)toupper((unsigned char)dsn[i]);
    return 0;
}

/* IBM member name rules: 1-8 chars, first = alpha|#,
 * rest = alnum|#|-  (z/OS DFSMS Using Data Sets, ch.5) */
static int ds_valid_member(const char *name) {
    int n = (int)strlen(name);
    if (n < 1 || n > 8) return 0;
    char c = name[0];
    if (!isalpha((unsigned char)c) && c != '#' && c != '$' && c != '@')
        return 0;
    for (int i = 1; i < n; i++) {
        c = name[i];
        if (!isalnum((unsigned char)c) &&
            c != '#' && c != '$' && c != '@' && c != '-')
            return 0;
    }
    return 1;
}

        /* ---- WRITE  (PS and PDS) ---- */

void terminal_ds_panel(void) {
    char input[256];

    while (1) {
        terminal_clear();
        terminal_header("DFSMS DATASET MANAGER");

        printf("\n");
        printf(" %sDFSMS DATASET SERVICES%s         Catalog entries: %d\n",
               TERM_BOLD, TERM_RESET, zos_catalog.count);
        printf("\n");
        printf(" %sQuick Start:%s\n", TERM_GREEN, TERM_RESET);
        printf("   1. ALLOC MY.DATA PS RECFM(FB) LRECL(80)   -- allocate a dataset\n");
        printf("   2. WRITE MY.DATA                           -- add records interactively\n");
        printf("   3. BROWSE MY.DATA                         -- view records\n");
        printf("   4. DELETE MY.DATA                         -- delete dataset\n");
        printf("\n");
        printf(" %sAll Commands:%s\n", TERM_CYAN, TERM_RESET);
        printf("   %-30s List catalog (all or specific DSN)\n", "LISTCAT [dsn]");
        printf("   %-30s Allocate PS or PDS dataset\n",         "ALLOC dsn PS|PDS [RECFM(x)] [LRECL(n)]");
        printf("   %-30s Write records to PS dataset\n",        "WRITE dsn");
        printf("   %-30s Browse dataset contents\n",            "BROWSE dsn");
        printf("   %-30s Delete dataset from catalog\n",        "DELETE dsn");
        printf("   %-30s Run IDCAMS command\n",                 "IDCAMS [command]");
        printf("   %-30s Define VSAM cluster\n",                "DEFINE CLUSTER (...)");
        printf("\n");
        term_pf_bar();
        printf(" COMMAND ===> ");
        fflush(stdout);

        PF_KEY pf = term_read_input(input, (int)sizeof(input));
        if (pf == PF3 || pf == PF12) break;
        if (!input[0]) continue;

        char cmd[32] = {0};
        sscanf(input, "%31s", cmd);
        for (int i = 0; cmd[i]; i++) cmd[i] = (char)toupper((unsigned char)cmd[i]);

        if (strcmp(cmd, "END") == 0 || strcmp(cmd, "Q") == 0 ||
            strcmp(cmd, "EXIT") == 0) {
            break;
        }

        if (strcmp(cmd, "LISTCAT") == 0) {
            char dsn[DS_DSN_LEN + 1] = {0};
            if (sscanf(input, "%*s %44s", dsn) == 1) {
                ZOS_DATASET *ds = ds_catalog_find(dsn);
                if (ds && ds->dsorg == DSORG_GDG)
                    gdg_listcat_base(dsn);
                else
                    ds_catalog_listcat(dsn);
            } else {
                ds_catalog_listcat(NULL);
            }
            printf("\n Press ENTER to continue...");
            fflush(stdout);
            getchar();
            continue;
        }

        if (strcmp(cmd, "ALLOC") == 0) {
            char dsn[DS_DSN_LEN + 1] = {0};
            char dsorg_str[8] = "PS";
            char rest_opts[128] = {0};
            char tmp1[64]={0}, tmp2[64]={0};
            int parsed = sscanf(input, "%*s %44s %7s %127[^\n]", dsn, tmp1, tmp2);
            if (parsed >= 1) {
                if (parsed >= 2) {
                    char up[8]={0};
                    for (int i=0; tmp1[i] && i<7; i++)
                        up[i]=(char)toupper((unsigned char)tmp1[i]);
                    if (strcmp(up,"PS")==0||strcmp(up,"PO")==0||strcmp(up,"PDS")==0) {
                        strncpy(dsorg_str, up, sizeof(dsorg_str)-1);
                        if (parsed >= 3) strncpy(rest_opts, tmp2, sizeof(rest_opts)-1);
                    } else {
                        snprintf(rest_opts, sizeof(rest_opts), "%s %s", tmp1,
                                 parsed>=3 ? tmp2 : "");
                    }
                }
                char alloc_cmd[512];
                snprintf(alloc_cmd, sizeof(alloc_cmd),
                         "ALLOCATE NAME(%s) %s %s", dsn, dsorg_str, rest_opts);
                printf("\n --- IDCAMS ALLOCATE ---\n");
                int rc = idcams_run(alloc_cmd);
                printf(" --- RC=%d ---\n", rc);
            } else {
                printf(" Syntax: ALLOC <dsn> [PS|PDS] [RECFM(x)] [LRECL(n)]\n");
            }
            printf("\n Press ENTER to continue...");
            fflush(stdout);
            getchar();
            continue;
        }
        if (strcmp(cmd, "WRITE") == 0) {
            char token[DS_DSN_LEN + DS_MEMBER_LEN + 4] = {0};
            if (sscanf(input, "%*s %79s", token) != 1) {
                printf(" Syntax: WRITE <dsn>         -- sequential dataset\n");
                printf("         WRITE <dsn(member)> -- PDS member\n");
                printf("\n Press ENTER to continue...");
                fflush(stdout); getchar(); continue;
            }
            char dsn[DS_DSN_LEN + 1] = {0};
            char mbr[DS_MEMBER_LEN + 1] = {0};
            ds_parse_dsn_member(token, dsn, mbr);

            ZOS_DATASET *ds = ds_catalog_find(dsn);
            if (!ds) {
                printf(" IGD101I DATASET '%s' NOT IN CATALOG\n", dsn);
                if (mbr[0])
                    printf(" Tip: ALLOC %s PDS first\n", dsn);
                else
                    printf(" Tip: ALLOC %s PS RECFM(FB) LRECL(80) first\n", dsn);
                printf("\n Press ENTER to continue...");
                fflush(stdout); getchar(); continue;
            }

            /* ---- PS write ---- */
            if (ds->dsorg == DSORG_PS) {
                if (mbr[0]) {
                    printf(" IEC020I '%s' is PS — use WRITE %s (no member)\n", dsn, dsn);
                    printf("\n Press ENTER to continue...");
                    fflush(stdout); getchar(); continue;
                }
                ZOS_DCB *dcb = ps_open(dsn, "WRITE", OPEN_EXTEND,
                                       ds->recfm, ds->lrecl, ds->blksize);
                if (!dcb) {
                    printf(" IEC030I OPEN FAILED for %s\n", dsn);
                    printf("\n Press ENTER to continue...");
                    fflush(stdout); getchar(); continue;
                }
                int lrecl = dcb->lrecl > 0 ? dcb->lrecl : 80;
                printf("\n WRITE: %s  RECFM=%s  LRECL=%d\n", dsn,
                       ds->recfm==RECFM_FB?"FB":ds->recfm==RECFM_VB?"VB":
                       ds->recfm==RECFM_F?"F":ds->recfm==RECFM_V?"V":"U", lrecl);
                printf(" Enter records (blank line or PF3 to stop):\n");
                printf(" --------------------------------------------------------------------------------\n");
                int written = 0; char rec[32761];
                while (1) {
                    printf(" +%04d> ", written + 1); fflush(stdout);
                    PF_KEY wpf = term_read_input(rec, (int)sizeof(rec));
                    if (wpf == PF3 || wpf == PF12 || !rec[0]) break;
                    if (ps_write(dcb, rec, (int)strlen(rec)) != DS_OK) {
                        printf(" IEC040I WRITE ERROR\n"); break;
                    }
                    written++;
                }
                ps_close(dcb);
                printf(" --------------------------------------------------------------------------------\n");
                printf(" IEF285I %s: %d RECORD(S) WRITTEN\n", dsn, written);

            /* ---- PDS member write ---- */
            } else if (ds->dsorg == DSORG_PO) {
                if (!mbr[0]) {
                    printf(" IEC020I PDS requires member: WRITE %s(MEMBER)\n", dsn);
                    printf("\n Press ENTER to continue...");
                    fflush(stdout); getchar(); continue;
                }
                if (!ds_valid_member(mbr)) {
                    printf(" IEC130I INVALID MEMBER NAME '%s'\n", mbr);
                    printf("   Rules: 1-8 chars, first=alpha|#,");
                    printf(" rest=alnum|#|-\n");
                    printf("\n Press ENTER to continue...");
                    fflush(stdout); getchar(); continue;
                }
                ZOS_DCB *dcb = pds_open(dsn, "WRITE", OPEN_OUTPUT);
                if (!dcb) {
                    printf(" IEC030I PDS OPEN FAILED for %s\n", dsn);
                    printf("\n Press ENTER to continue...");
                    fflush(stdout); getchar(); continue;
                }
                int lrecl = dcb->lrecl > 0 ? dcb->lrecl : 80;
                /* Check if member exists — STOW R will replace */
                int replacing = (pds_find_member(dcb->dataset, mbr) != NULL);
                printf("\n WRITE: %s(%s)  RECFM=%s  LRECL=%d%s\n",
                       dsn, mbr,
                       ds->recfm==RECFM_FB?"FB":ds->recfm==RECFM_VB?"VB":
                       ds->recfm==RECFM_F?"F":ds->recfm==RECFM_V?"V":"U",
                       lrecl, replacing ? "  (REPLACING)" : "");
                printf(" Enter records (blank line or PF3 to STOW and close):\n");
                printf(" --------------------------------------------------------------------------------\n");
                int written = 0; char rec[32761];
                while (1) {
                    printf(" +%04d> ", written + 1); fflush(stdout);
                    PF_KEY wpf = term_read_input(rec, (int)sizeof(rec));
                    if (wpf == PF3 || wpf == PF12 || !rec[0]) break;
                    if (pds_write(dcb, rec, (int)strlen(rec)) != DS_OK) {
                        printf(" IEC040I WRITE ERROR\n"); break;
                    }
                    written++;
                }
                /* STOW R — Add or Replace (IBM STOW option R) */
                int stow_rc = pds_stow(dcb, mbr);
                pds_close(dcb);
                printf(" --------------------------------------------------------------------------------\n");
                if (stow_rc == DS_OK) {
                    printf(" IEF285I %s(%s): %d RECORD(S) STOW'D%s\n",
                           dsn, mbr, written, replacing ? " (REPLACED)" : "");
                } else {
                    printf(" IEC026I STOW FAILED RC=%d FOR %s(%s)\n",
                           stow_rc, dsn, mbr);
                }
            } else {
                printf(" IEC020I WRITE: DSORG not PS or PO for '%s'\n", dsn);
            }
            printf("\n Press ENTER to continue...");
            fflush(stdout); getchar(); continue;
        }

        /* ---- BROWSE (PS and PDS member) ---- */
        if (strcmp(cmd, "DELETE") == 0) {
            char dsn[DS_DSN_LEN + 1] = {0};
            if (sscanf(input, "%*s %44s", dsn) != 1) {
                printf(" Syntax: DELETE <dsn>\n");
            } else {
                char del_cmd[64];
                snprintf(del_cmd, sizeof(del_cmd), "DELETE %s", dsn);
                printf("\n --- IDCAMS DELETE ---\n");
                int rc = idcams_run(del_cmd);
                printf(" --- RC=%d ---\n", rc);
            }
            printf("\n Press ENTER to continue...");
            fflush(stdout);
            getchar();
            continue;
        }

        if (strcmp(cmd, "DEFINE") == 0) {
            printf("\n --- IDCAMS DEFINE ---\n");
            int rc = idcams_run(input);
            printf(" --- RC=%d ---\n", rc);
            printf("\n Press ENTER to continue...");
            fflush(stdout);
            getchar();
            continue;
        }
        if (strcmp(cmd, "IDCAMS") == 0) {
            const char *rest = input + 6;
            while (*rest == ' ') rest++;
            if (*rest) {
                printf("\n --- IDCAMS OUTPUT ---\n");
                int rc = idcams_run(rest);
                printf(" --- IDCAMS RC=%d ---\n", rc);
            } else {
                printf(" Enter IDCAMS commands (blank line=execute, END=cancel):\n");
                char cmdbuf[4096] = {0};
                char line[256];
                while (1) {
                    printf(" > ");
                    fflush(stdout);
                    if (!fgets(line, sizeof(line), stdin)) break;
                    int ll = (int)strlen(line);
                    while (ll > 0 && (line[ll-1]=='\n'||line[ll-1]=='\r'))
                        line[--ll] = '\0';
                    if (!line[0] || strcmp(line,"END")==0) break;
                    if (strlen(cmdbuf)+strlen(line)+2 < sizeof(cmdbuf)) {
                        strcat(cmdbuf, line);
                        strcat(cmdbuf, "\n");
                    }
                }
                if (cmdbuf[0]) {
                    printf("\n --- IDCAMS OUTPUT ---\n");
                    int rc = idcams_run(cmdbuf);
                    printf(" --- IDCAMS RC=%d ---\n", rc);
                }
            }
            printf("\n Press ENTER to continue...");
            fflush(stdout);
            getchar();
            continue;
        }

        if (strcmp(cmd, "BROWSE") == 0) {
            char token[DS_DSN_LEN + DS_MEMBER_LEN + 4] = {0};
            if (sscanf(input, "%*s %79s", token) != 1) {
                printf(" Syntax: BROWSE <dsn>         -- sequential / PDS directory\n");
                printf("         BROWSE <dsn(member)> -- PDS member contents\n");
                printf("\n Press ENTER to continue...");
                fflush(stdout); getchar(); continue;
            }
            char dsn[DS_DSN_LEN + 1] = {0};
            char mbr[DS_MEMBER_LEN + 1] = {0};
            ds_parse_dsn_member(token, dsn, mbr);

            ZOS_DATASET *ds = ds_catalog_find(dsn);
            if (!ds) {
                printf(" IGG001I DATASET '%s' NOT IN CATALOG\n", dsn);
            } else if (ds->dsorg == DSORG_PS) {
                ZOS_DCB *dcb = ps_open(dsn, "BROWSE", OPEN_INPUT,
                                       ds->recfm, ds->lrecl, ds->blksize);
                if (dcb) {
                    printf("\n BROWSE: %s  RECFM=%s  LRECL=%d\n", dsn,
                           ds->recfm==RECFM_FB?"FB":ds->recfm==RECFM_VB?"VB":
                           ds->recfm==RECFM_F?"F":ds->recfm==RECFM_V?"V":"U",
                           ds->lrecl);
                    printf(" --------------------------------------------------------------------------------\n");
                    char buf[32761]; int rlen, shown = 0;
                    while (ps_read(dcb, buf, &rlen)==DS_OK && shown < 50) {
                        buf[rlen] = '\0';
                        int tl = rlen; while (tl>0 && buf[tl-1]==' ') tl--;
                        buf[tl] = '\0';
                        printf(" %04d  %s\n", shown+1, buf); shown++;
                    }
                    if (shown==0) printf(" (EMPTY DATASET)\n");
                    else if (shown==50) printf(" ... (first 50 records shown)\n");
                    ps_close(dcb);
                }
            } else if (ds->dsorg == DSORG_PO) {
                ZOS_DCB *dcb = pds_open(dsn, "BROWSE", OPEN_INPUT);
                if (dcb) {
                    if (!mbr[0]) {
                        /* No member — show directory listing */
                        pds_list_members(dcb);
                    } else {
                        /* Browse specific member */
                        int rc = pds_find(dcb, mbr);
                        if (rc == DS_MEMBER_NF) {
                            printf(" IEC143I MEMBER '%s' NOT FOUND IN %s\n",
                                   mbr, dsn);
                        } else {
                            printf("\n BROWSE: %s(%s)  RECFM=%s  LRECL=%d\n",
                                   dsn, mbr,
                                   ds->recfm==RECFM_FB?"FB":ds->recfm==RECFM_VB?"VB":
                                   ds->recfm==RECFM_F?"F":ds->recfm==RECFM_V?"V":"U",
                                   ds->lrecl);
                            printf(" --------------------------------------------------------------------------------\n");
                            char buf[32761]; int rlen, shown = 0;
                            while (pds_read(dcb, buf, &rlen)==DS_OK && shown<50) {
                                buf[rlen] = '\0';
                                int tl = rlen;
                                while (tl>0 && buf[tl-1]==' ') tl--;
                                buf[tl] = '\0';
                                printf(" %04d  %s\n", shown+1, buf); shown++;
                            }
                            if (shown==0) printf(" (EMPTY MEMBER)\n");
                            else if (shown==50) printf(" ... (first 50 records)\n");
                        }
                        pds_close(dcb);
                    }
                }
            } else {
                printf(" BROWSE: DSORG not supported for '%s'\n", dsn);
            }
            printf("\n Press ENTER to continue...");
            fflush(stdout); getchar(); continue;
        }
        if (pf == PF1) {
            printf("\n Dataset Manager Help:\n");
            printf("  ALLOC dsn PS [RECFM(FB)] [LRECL(80)]  -- allocate sequential dataset\n");
            printf("  ALLOC dsn PDS                         -- allocate partitioned dataset\n");
            printf("  WRITE dsn                             -- append records (blank=end)\n");
            printf("  BROWSE dsn                            -- view records\n");
            printf("  DELETE dsn                            -- delete from catalog\n");
            printf("  LISTCAT [dsn]                         -- list catalog\n");
            printf("  DEFINE CLUSTER (NAME(...) INDEXED ...) -- define VSAM cluster\n");
            printf("  IDCAMS [cmd]                          -- run raw IDCAMS command\n");
            printf("  PF3 or END                            -- return to main menu\n");
            printf("\n Press ENTER to continue...");
            fflush(stdout);
            getchar();
            continue;
        }

        printf(" Unknown command: %s  (PF1=Help PF3=End)\n", cmd);
        printf("\n Press ENTER to continue...");
        fflush(stdout);
        getchar();
    }
}

int terminal_run(void) {
    char input[256];
    int result;
    
    terminal_init();
    
    while (1) {
        /* Display appropriate panel based on mode */
        switch (terminal.mode) {
            case TERM_MODE_MAIN:
                terminal_main_menu();
                break;
            case TERM_MODE_DB:
                terminal_display_databases();
                break;
            case TERM_MODE_TM:
                terminal_clear();
                terminal_header("TRANSACTION MANAGER");
                terminal_display_transactions();
                terminal_display_regions();
                terminal_display_queues();
                printf("\n  Commands: /QUEUE, /REGION, /BACK\n");
                terminal_footer();
                break;
            case TERM_MODE_DLI:
                terminal_dli_panel();
                break;
            case TERM_MODE_DISPLAY:
                terminal_clear();
                ims_display_status();
                printf("\n Press ENTER to return...");
                break;
            case TERM_MODE_HELP:
                terminal_help();
                break;
        }
        
        /* Get input */
        if (terminal_get_input(input, sizeof(input)) < 0) {
            break;
        }
        
        /* Process command */
        result = terminal_process_command(input);
        if (result < 0) {
            break;  /* Exit */
        }
    }
    
    terminal_shutdown();
    return 0;
}
