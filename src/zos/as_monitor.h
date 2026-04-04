/*
 * z/OS Address Space Monitor Panel
 *
 * An ISPF-like panel showing all active address spaces, their status,
 * CPU time, and the virtual storage layout for each.
 *
 * Students learn: what address spaces are running, how they relate to
 * each other (IMS CTL vs MPP regions), and how storage is organized.
 */

#ifndef ZOS_AS_MONITOR_H
#define ZOS_AS_MONITOR_H

#include "address_space.h"
#include "console.h"

/* Display the full address space monitor panel */
void as_monitor_show(void);

/* Display the virtual storage map for a single address space */
void as_monitor_show_storage(int asid);

/* Display the subsystem architecture diagram (educational) */
void as_monitor_show_diagram(void);

/* Display the JES2 spool contents */
void as_monitor_show_spool(void);

/* Display the system syslog (recent WTO messages) */
void as_monitor_show_syslog(void);

/* Interactive address space monitor loop */
int as_monitor_run(void);

#endif /* ZOS_AS_MONITOR_H */
