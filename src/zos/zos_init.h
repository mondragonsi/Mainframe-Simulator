/*
 * z/OS Initialization and Dynamic Region Management
 */

#ifndef ZOS_INIT_H
#define ZOS_INIT_H

#include "address_space.h"
#include "svc.h"
#include "console.h"

/* System boot (IPL) and shutdown */
int  zos_init(const char *sysname, const char *plexname);
void zos_shutdown(void);

/* Dynamic address space creation */
ZOS_ADDRESS_SPACE *zos_start_mpp_region(const char *txn_code,
                                         const char *psb_name);
ZOS_ADDRESS_SPACE *zos_start_bmp_region(const char *pgm_name);
ZOS_ADDRESS_SPACE *zos_start_batch_job(const char *jobname,
                                        const char *pgm_name,
                                        char job_class);

#endif /* ZOS_INIT_H */
