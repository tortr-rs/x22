/*
 * x22present.h: what the Present extension (present.c) needs from a screen's driver,
 * and what the driver calls back. A screen without hooks gets a 60 Hz clock in software.
 *
 * Copyright (c) 2026 torter and the Chamesle Linux people. MIT license, see present.c.
 */
#ifndef X22PRESENT_H
#define X22PRESENT_H

#include <stdint.h>
#include "scrnintstr.h"

typedef struct {
    /* the frame counter (msc) right now, and when it last ticked (ust, microseconds) */
    Bool (*get_msc) (ScreenPtr pScreen, uint64_t *ust, uint64_t *msc);
    /* call PresentVblankEvent(pScreen, id, ...) when the counter reaches msc */
    Bool (*queue) (ScreenPtr pScreen, uint64_t id, uint64_t msc);
} PresentHooksRec, *PresentHooksPtr;

void PresentSetHooks (ScreenPtr pScreen, PresentHooksPtr hooks);
void PresentVblankEvent (ScreenPtr pScreen, uint64_t id, uint64_t ust, uint64_t msc);

#endif
