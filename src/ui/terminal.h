/*
 * IMS Terminal Interface
 * 
 * Simulates a mainframe-like terminal interface similar to TSO/ISPF.
 * Provides a DBA-friendly interface for IMS operations.
 */

#ifndef IMS_TERMINAL_H
#define IMS_TERMINAL_H

#include "../core/ims.h"

/* Screen dimensions (typical 3270 terminal) */
#define TERM_ROWS 24
#define TERM_COLS 80

/* Colors (ANSI escape codes) */
#define TERM_RESET    "\033[0m"
#define TERM_BOLD     "\033[1m"
#define TERM_GREEN    "\033[32m"
#define TERM_YELLOW   "\033[33m"
#define TERM_BLUE     "\033[34m"
#define TERM_CYAN     "\033[36m"
#define TERM_WHITE    "\033[37m"
#define TERM_RED      "\033[31m"
#define TERM_REVERSE  "\033[7m"

/* Terminal modes */
typedef enum {
    TERM_MODE_MAIN,
    TERM_MODE_DB,
    TERM_MODE_TM,
    TERM_MODE_DLI,
    TERM_MODE_DISPLAY,
    TERM_MODE_HELP,
} TERM_MODE;

/* Terminal context */
typedef struct {
    TERM_MODE mode;
    char current_db[9];
    char current_psb[9];
    IMS_PCB *current_pcb;
    IMS_IOPCB *io_pcb;
    int line;
    char message[256];
    bool show_message;
    char last_command[256];
} IMS_TERMINAL;

/* Initialize terminal */
int terminal_init(void);
void terminal_shutdown(void);

/* Screen operations */
void terminal_clear(void);
void terminal_header(const char *title);
void terminal_footer(void);
void terminal_message(const char *msg);
void terminal_error(const char *msg);

/* Main menu */
void terminal_main_menu(void);

/* Input handling */
int terminal_get_input(char *buffer, int max_length);
int terminal_process_command(const char *command);

/* Display panels */
void terminal_display_databases(void);
void terminal_display_psbs(void);
void terminal_display_transactions(void);
void terminal_display_regions(void);
void terminal_display_queues(void);

/* DL/I panel */
void terminal_dli_panel(void);
int terminal_execute_dli(const char *command);
void display_segment_data(const char *segment_name, const char *data);

/* Dataset Manager panel */
void terminal_ds_panel(void);

/* Help panel */
void terminal_help(void);

/* Run main loop */
int terminal_run(void);

#endif /* IMS_TERMINAL_H */
