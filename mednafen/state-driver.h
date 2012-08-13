#ifndef __MDFN_STATE_DRIVER_H
#define __MDFN_STATE_DRIVER_H

#include "video.h"
#include "state-common.h"

void MDFNI_SelectState(int);

/* "fname" overrides the default save state filename code if non-NULL. */
/* If suffix is set, just override the default suffix(mc0-mc9) */
void MDFNI_SaveState(const char *fname, const char *suffix);
void MDFNI_LoadState(const char *fname, const char *suffix);

void MDFND_SetStateStatus(StateStatusStruct *status);

#endif
