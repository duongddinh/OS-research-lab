#ifndef JORDY_LARP_H
#define JORDY_LARP_H

/* NULL on success, otherwise the first failed controller check.
 * Uses synthetic feedback; no TCP connection or network traffic is created. */
const char *larp_selftest(void);
void larp_command(const char *arguments);

#endif
