#ifndef __MDFN_STATE_COMMON_H
#define __MDFN_STATE_COMMON_H

typedef struct
{
 int status[10];
 int current;
 // The most recently-saved-to slot
 int recently_saved;
} StateStatusStruct;

#endif
