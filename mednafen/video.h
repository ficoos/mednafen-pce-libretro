#ifndef __MDFN_VIDEO_H
#define __MDFN_VIDEO_H

#include "video/surface.h"

void MDFN_ResetMessages(void);
void MDFN_DispMessage(const char *format, ...) throw() MDFN_FORMATSTR(printf, 1, 2);

#endif
