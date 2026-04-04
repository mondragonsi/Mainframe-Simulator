/*
 * z/OS Operator Console Simulation
 *
 * The operator console is how systems programmers and operators
 * monitor and control z/OS subsystems at runtime.
 *
 * Command format: VERB [OBJECT[,OPTION...]]
 * Examples:
 *   D A,L               Display all address spaces
 *   D IMS,STATUS        Display IMS status
 *   F IMSCTL,STATUS     Modify (send command to) IMS control region
 *   S IMSMPP01          Start an MPP region
 *   P IMSMPP01          Stop (pause) an MPP region
 *   CANCEL JOB00123     Cancel a running job (S222 abend)
 *
 * IBM Reference: z/OS MVS System Commands (SA38-0666)
 */

#ifndef ZOS_CONSOLE_H
#define ZOS_CONSOLE_H

#include "address_space.h"

/* Maximum tokens in a console command */
#define CONSOLE_MAX_TOKENS 8
#define CONSOLE_TOKEN_LEN  32

/* Console command result */
typedef struct {
    bool   success;
    char   response[2048];  /* Multi-line response text */
} CONSOLE_RESULT;

/* Initialize console */
void console_init(void);

/* Process a console command string, return result */
CONSOLE_RESULT console_process(const char *command);

/* Display the syslog (recent WTO messages) */
void console_display_syslog(int last_n_lines);

/* Individual command handlers */
CONSOLE_RESULT console_cmd_display(const char **tokens, int count);
CONSOLE_RESULT console_cmd_modify(const char **tokens, int count);
CONSOLE_RESULT console_cmd_start(const char **tokens, int count);
CONSOLE_RESULT console_cmd_stop(const char **tokens, int count);
CONSOLE_RESULT console_cmd_cancel(const char **tokens, int count);
CONSOLE_RESULT console_cmd_syslog(const char **tokens, int count);

#endif /* ZOS_CONSOLE_H */
