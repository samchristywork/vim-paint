#ifndef VIMPAINT_UNDO_H
#define VIMPAINT_UNDO_H

#include "state.h"

void free_action(UndoAction *a);
void clear_history(void);
void begin_undo_action(void);
void push_undo(int x, int y);
void commit_undo_action(void);
void commit_canvas_snapshot(guint32 *before_snap, int bw, int bh);

#endif
