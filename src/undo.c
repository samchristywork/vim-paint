#include "vimpaint.h"

void free_action(UndoAction *a) {
  free(a->changes);
  a->changes = NULL;
  a->count = 0;
  free(a->before_snap);
  a->before_snap = NULL;
  free(a->after_snap);
  a->after_snap = NULL;
}

void clear_history(void) {
  for (int i = 0; i < undo_count; i++)
    free_action(&undo_stack[(undo_top - undo_count + i + undo_levels * 2) %
                            undo_levels]);
  undo_top = 0;
  undo_count = 0;
  for (int i = 0; i < redo_count; i++)
    free_action(&redo_stack[(redo_top - redo_count + i + undo_levels * 2) %
                            undo_levels]);
  redo_top = 0;
  redo_count = 0;
  staged_count = 0;
}

void begin_undo_action(void) { staged_count = 0; }

void push_undo(int x, int y) {
  for (int i = 0; i < staged_count; i++)
    if (staged[i].x == x && staged[i].y == y)
      return;
  if (staged_count >= staged_cap) {
    int new_cap = staged_cap ? staged_cap * 2 : 16;
    PixelChange *tmp = realloc(staged, new_cap * sizeof(PixelChange));
    if (!tmp)
      return;
    staged = tmp;
    staged_cap = new_cap;
  }
  staged[staged_count++] = (PixelChange){x, y, PX(y, x), 0};
}

void commit_undo_action(void) {
  if (staged_count == 0)
    return;
  for (int i = 0; i < redo_count; i++)
    free_action(&redo_stack[(redo_top - redo_count + i + undo_levels * 2) %
                            undo_levels]);
  redo_top = 0;
  redo_count = 0;
  for (int i = 0; i < staged_count; i++)
    staged[i].after = PX(staged[i].y, staged[i].x);
  int idx = undo_top % undo_levels;
  if (undo_count == undo_levels)
    free_action(&undo_stack[idx]);
  PixelChange *copy = malloc(staged_count * sizeof(PixelChange));
  if (!copy)
    return;
  memcpy(copy, staged, staged_count * sizeof(PixelChange));
  undo_stack[idx] = (UndoAction){copy, staged_count};
  undo_top++;
  if (undo_count < undo_levels)
    undo_count++;
  staged_count = 0;
  canvas_dirty = TRUE;
  title_refresh();
}

/* Record a full-canvas snapshot undo entry (for ops that change dimensions). */
void commit_canvas_snapshot(guint32 *before_snap, int bw, int bh) {
  guint32 *after_snap = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  if (!after_snap) {
    free(before_snap);
    return;
  }
  memcpy(after_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  for (int i = 0; i < redo_count; i++)
    free_action(&redo_stack[(redo_top - redo_count + i + undo_levels * 2) %
                            undo_levels]);
  redo_top = 0;
  redo_count = 0;
  int idx = undo_top % undo_levels;
  if (undo_count == undo_levels)
    free_action(&undo_stack[idx]);
  undo_stack[idx] = (UndoAction){.before_snap = before_snap,
                                 .before_w = bw,
                                 .before_h = bh,
                                 .after_snap = after_snap,
                                 .after_w = CANVAS_W,
                                 .after_h = CANVAS_H};
  undo_top++;
  if (undo_count < undo_levels)
    undo_count++;
  canvas_dirty = TRUE;
  title_refresh();
}
