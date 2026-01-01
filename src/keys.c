#include "keys.h"
#include "canvas.h"
#include "commands.h"
#include "fileio.h"
#include "layers.h"
#include "main.h"
#include "palette.h"
#include "undo.h"

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                      gpointer data) {
  if (cmd_mode) {
    if (event->keyval == GDK_KEY_Escape) {
      tab_reset();
      cmd_mode = FALSE;
      status_update();
    } else if (event->keyval == GDK_KEY_Return) {
      tab_reset();
      cmd_mode = FALSE;
      if (cmd_len > 1) {
        if (cmd_history_count == 0 ||
            strcmp(cmd_history[(cmd_history_count - 1) % CMD_HISTORY_MAX],
                   cmd_buf) != 0) {
          snprintf(cmd_history[cmd_history_count % CMD_HISTORY_MAX],
                   sizeof(cmd_buf), "%s", cmd_buf);
          cmd_history_count++;
        }
      }
      cmd_history_idx = -1;
      cmd_history_draft[0] = '\0';
      cmd_execute();
    } else if (event->keyval == GDK_KEY_Up) {
      tab_reset();
      if (cmd_history_count > 0) {
        if (cmd_history_idx == -1)
          snprintf(cmd_history_draft, sizeof(cmd_history_draft), "%s", cmd_buf);
        int next = cmd_history_idx + 1;
        int avail = cmd_history_count < CMD_HISTORY_MAX ? cmd_history_count
                                                        : CMD_HISTORY_MAX;
        if (next < avail) {
          cmd_history_idx = next;
          int hidx =
              (cmd_history_count - 1 - cmd_history_idx) % CMD_HISTORY_MAX;
          snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd_history[hidx]);
          cmd_len = strlen(cmd_buf);
          cmd_set(cmd_buf);
        }
      }
    } else if (event->keyval == GDK_KEY_Down) {
      tab_reset();
      if (cmd_history_idx > 0) {
        cmd_history_idx--;
        int hidx = (cmd_history_count - 1 - cmd_history_idx) % CMD_HISTORY_MAX;
        snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd_history[hidx]);
        cmd_len = strlen(cmd_buf);
        cmd_set(cmd_buf);
      } else if (cmd_history_idx == 0) {
        cmd_history_idx = -1;
        snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd_history_draft);
        cmd_len = strlen(cmd_buf);
        cmd_set(cmd_buf);
      }
    } else if (event->keyval == GDK_KEY_BackSpace) {
      tab_reset();
      if (cmd_len > 1) {
        cmd_buf[--cmd_len] = '\0';
        cmd_set(cmd_buf);
      }
    } else if (event->keyval == GDK_KEY_Tab) {
      static const char *const file_pfxs[] = {
          ":loadp ", ":savep ", ":export ", ":importp ", ":wq ",
          ":w ",     ":e! ",    ":e ",      NULL};
      const char *pfx = NULL;
      for (int i = 0; file_pfxs[i]; i++) {
        size_t plen = strlen(file_pfxs[i]);
        if (strncmp(cmd_buf, file_pfxs[i], plen) == 0) {
          pfx = file_pfxs[i];
          break;
        }
      }
      if (pfx) {
        if (!tab_glob_valid) {
          const char *partial = cmd_buf + strlen(pfx);
          char pattern[sizeof(cmd_buf) + 2];
          snprintf(pattern, sizeof(pattern), "%s*", partial);
          memset(&tab_glob, 0, sizeof(tab_glob));
          if (glob(pattern, GLOB_MARK | GLOB_TILDE, NULL, &tab_glob) == 0 &&
              tab_glob.gl_pathc > 0) {
            tab_glob_valid = TRUE;
            tab_glob_idx = 0;
            snprintf(tab_cmd_prefix, sizeof(tab_cmd_prefix), "%s", pfx);
          } else {
            globfree(&tab_glob);
          }
        } else {
          tab_glob_idx = (tab_glob_idx + 1) % (int)tab_glob.gl_pathc;
        }
        if (tab_glob_valid) {
          int written =
              snprintf(cmd_buf, sizeof(cmd_buf), "%s%s", tab_cmd_prefix,
                       tab_glob.gl_pathv[tab_glob_idx]);
          if (written >= (int)sizeof(cmd_buf)) {
            tab_reset();
            cmd_flash("Path too long.");
          } else {
            cmd_len = strlen(cmd_buf);
            cmd_set(cmd_buf);
          }
        }
      } else {
        /* Color name completion for :set bg, :set color, :find color */
        static const char *const color_pfxs[] = {":set bg ", ":set color ",
                                                 ":find color ", NULL};
        const char *cpfx = NULL;
        for (int i = 0; color_pfxs[i]; i++) {
          size_t plen = strlen(color_pfxs[i]);
          if (strncmp(cmd_buf, color_pfxs[i], plen) == 0) {
            cpfx = color_pfxs[i];
            break;
          }
        }
        /* Also match :set color <N> <partial> */
        char dyn_pfx[64] = "";
        if (!cpfx && strncmp(cmd_buf, ":set color ", 11) == 0) {
          const char *after = cmd_buf + 11;
          if (*after >= '0' && *after <= '9') {
            const char *sp = strchr(after, ' ');
            if (sp) {
              size_t plen = (size_t)(sp + 1 - cmd_buf);
              if (plen < sizeof(dyn_pfx)) {
                strncpy(dyn_pfx, cmd_buf, plen);
                dyn_pfx[plen] = '\0';
                cpfx = dyn_pfx;
              }
            }
          }
        }
        if (cpfx) {
          const char *partial = cmd_buf + strlen(cpfx);
          if (!color_tab_valid) {
            color_tab_count = 0;
            color_tab_idx = 0;
            size_t plen = strlen(partial);
            for (int i = 0; i < NAMED_COLORS_COUNT; i++)
              if (color_tab_count < 256 &&
                  strncmp(named_colors[i].name, partial, plen) == 0)
                color_tab_matches[color_tab_count++] = i;
            if (color_tab_count > 0) {
              color_tab_valid = TRUE;
              snprintf(color_tab_prefix, sizeof(color_tab_prefix), "%s", cpfx);
            }
          } else {
            color_tab_idx = (color_tab_idx + 1) % color_tab_count;
          }
          if (color_tab_valid) {
            int written =
                snprintf(cmd_buf, sizeof(cmd_buf), "%s%s", color_tab_prefix,
                         named_colors[color_tab_matches[color_tab_idx]].name);
            if (written >= (int)sizeof(cmd_buf)) {
              color_tab_valid = FALSE;
              color_tab_count = 0;
              cmd_flash("Color name too long.");
            } else {
              cmd_len = strlen(cmd_buf);
              cmd_set(cmd_buf);
            }
          }
        }
      }
    } else {
      tab_reset();
      guint32 uc = gdk_keyval_to_unicode(event->keyval);
      if (uc >= 0x20 && uc < 0x7f && cmd_len < (int)sizeof(cmd_buf) - 2) {
        cmd_buf[cmd_len++] = (char)uc;
        cmd_buf[cmd_len] = '\0';
        cmd_set(cmd_buf);
      }
    }
    return TRUE;
  }

  if (event->is_modifier)
    return FALSE;

  static gboolean pending_g = FALSE;
  static gboolean pending_d = FALSE;
  static gboolean pending_f = FALSE;
  static gboolean pending_q = FALSE;
  static gboolean pending_at = FALSE;
  static int d_count = 1;
  static int f_count = 1;

  /* Stop recording: q while already recording */
  if (macro_recording && event->keyval == GDK_KEY_q &&
      !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
    macro_recording = FALSE;
    char msg[40];
    snprintf(msg, sizeof(msg), "Recorded @%c  (%d events)", 'a' + macro_reg,
             macro_len[macro_reg]);
    cmd_flash(msg);
    return TRUE;
  }

  /* Append to macro buffer while recording */
  if (macro_recording && !macro_playing) {
    if (macro_len[macro_reg] < MACRO_MAX_EVENTS) {
      macro_buf[macro_reg][macro_len[macro_reg]].keyval = event->keyval;
      macro_buf[macro_reg][macro_len[macro_reg]].state = event->state;
      macro_len[macro_reg]++;
    } else {
      cmd_flash("Macro buffer full.");
    }
  }

  /* q<letter>: start recording */
  if (pending_q) {
    pending_q = FALSE;
    if (event->keyval >= GDK_KEY_a && event->keyval <= GDK_KEY_z) {
      macro_reg = event->keyval - GDK_KEY_a;
      macro_len[macro_reg] = 0;
      macro_recording = TRUE;
      status_update();
    }
    return TRUE;
  }

  /* @<letter>: replay macro */
  if (pending_at) {
    pending_at = FALSE;
    if (!macro_playing && event->keyval >= GDK_KEY_a &&
        event->keyval <= GDK_KEY_z) {
      int reg = event->keyval - GDK_KEY_a;
      if (macro_len[reg] > 0) {
        macro_playing = TRUE;
        GdkEventKey fake = *event;
        fake.send_event = TRUE;
        for (int i = 0; i < macro_len[reg]; i++) {
          fake.keyval = macro_buf[reg][i].keyval;
          fake.state = macro_buf[reg][i].state;
          on_key_press(widget, &fake, data);
        }
        macro_playing = FALSE;
        gtk_widget_queue_draw(main_canvas);
      }
    }
    return TRUE;
  }

  if (event->keyval == GDK_KEY_colon) {
    pending_g = FALSE;
    pending_d = FALSE;
    pending_f = FALSE;
    pending_q = FALSE;
    pending_at = FALSE;
    cmd_mode = TRUE;
    cmd_buf[0] = ':';
    cmd_buf[1] = '\0';
    cmd_len = 1;
    cmd_set(cmd_buf);
    return TRUE;
  }
  static int count = 0;
  static guint last_action = 0;
  static int last_radius = 0;
  static gboolean last_was_visual = FALSE;
  static int last_visual_dx = 0, last_visual_dy = 0;
  static int last_find_dir =
      0; /* +1 = forward (f), -1 = backward (F), 0 = none */
  static int gg_count = 0;

  if (pending_g) {
    pending_g = FALSE;
    if (event->keyval == GDK_KEY_g) {
      cursor_y = gg_count > 0 ? CLAMP(gg_count - 1, 0, CANVAS_H - 1) : 0;
      count = 0;
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    if (event->keyval == GDK_KEY_t) {
      tab_switch((tab_current + 1) % tab_count);
      return TRUE;
    }
    if (event->keyval == GDK_KEY_T) {
      tab_switch((tab_current - 1 + tab_count) % tab_count);
      return TRUE;
    }
  }

  if (pending_d) {
    pending_d = FALSE;
    int x0 = cursor_x, x1 = cursor_x;
    int y0 = cursor_y, y1 = cursor_y;
    gboolean whole_row = FALSE;
    switch (event->keyval) {
    case GDK_KEY_d:
      whole_row = TRUE;
      break;
    case GDK_KEY_h:
      x0 = MAX(cursor_x - d_count, 0);
      break;
    case GDK_KEY_l:
      x1 = MIN(cursor_x + d_count, CANVAS_W - 1);
      break;
    case GDK_KEY_w:
      x1 = MIN(cursor_x + 5 * d_count, CANVAS_W - 1);
      break;
    case GDK_KEY_b:
      x0 = MAX(cursor_x - 5 * d_count, 0);
      break;
    case GDK_KEY_0:
      x0 = 0;
      break;
    case GDK_KEY_dollar:
      x1 = CANVAS_W - 1;
      break;
    case GDK_KEY_j:
      whole_row = TRUE;
      y1 = MIN(cursor_y + d_count, CANVAS_H - 1);
      break;
    case GDK_KEY_k:
      whole_row = TRUE;
      y0 = MAX(cursor_y - d_count, 0);
      break;
    default:
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    begin_undo_action();
    if (whole_row) {
      for (int ey = y0; ey <= y1; ey++)
        for (int ex = 0; ex < CANVAS_W; ex++)
          paint_pixel(ex, ey, bg_color);
    } else {
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, cursor_y, bg_color);
    }
    commit_undo_action();
    last_action = GDK_KEY_x;
    last_was_visual = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (pending_f) {
    pending_f = FALSE;
    void (*fn)(void) = find_right;
    if (event->keyval == GDK_KEY_j) {
      last_find_dir = 2;
      fn = find_down;
    } else if (event->keyval == GDK_KEY_k) {
      last_find_dir = -2;
      fn = find_up;
    } else if (event->keyval == GDK_KEY_h) {
      last_find_dir = -1;
      fn = find_left;
    } else {
      last_find_dir = 1;
    }
    for (int i = 0; i < f_count; i++)
      fn();
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  /* Ctrl+1–9: select palette slot directly */
  if ((event->state & GDK_CONTROL_MASK) && event->keyval >= GDK_KEY_1 &&
      event->keyval <= GDK_KEY_9) {
    int slot = event->keyval - GDK_KEY_0;
    if (slot < PALETTE_SIZE) {
      int pr, pg, pb;
      palette_to_rgb(slot, &pr, &pg, &pb);
      fg_color = PACK_RGBA(pr, pg, pb, 255);
      gtk_widget_queue_draw(palette_bar);
      flash_color(slot);
    } else {
      cmd_flash("No such palette slot.");
    }
    return TRUE;
  }

  /* Accumulate numeric prefix; treat 0 as line-start only when count is 0 */
  if (event->keyval >= GDK_KEY_1 && event->keyval <= GDK_KEY_9) {
    count = count * 10 + (event->keyval - GDK_KEY_0);
    return TRUE;
  }
  if (event->keyval == GDK_KEY_0 && count > 0) {
    count = count * 10;
    return TRUE;
  }

  int orig_count = count;
  int n = count > 0 ? count : 1;
  int radius = count;
  count = 0;

  if (event->keyval == GDK_KEY_v) {
    visual_mode = !visual_mode;
    visual_anchor_x = cursor_x;
    visual_anchor_y = cursor_y;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (event->keyval == GDK_KEY_Escape) {
    visual_mode = FALSE;
    insert_mode = FALSE;
    gradient_tool = FALSE;
    grad_dragging = FALSE;
    pending_g = FALSE;
    pending_d = FALSE;
    pending_f = FALSE;
    pending_q = FALSE;
    pending_at = FALSE;
    macro_recording = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_y) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    yank_w = x1 - x0 + 1;
    yank_h = y1 - y0 + 1;
    free(yank_buf);
    yank_buf = malloc(yank_w * yank_h * sizeof(guint32));
    if (!yank_buf) {
      cmd_flash("Out of memory.");
      visual_mode = FALSE;
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    for (int ey = 0; ey < yank_h; ey++)
      for (int ex = 0; ex < yank_w; ex++)
        yank_buf[ey * yank_w + ex] = PX(y0 + ey, x0 + ex);
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_T) {
    snprintf(cmd_buf, sizeof(cmd_buf), ":seltolay");
    cmd_execute();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode &&
      (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_x)) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    guint32 val = (event->keyval == GDK_KEY_r) ? fg_color : bg_color;
    begin_undo_action();
    for (int ey = y0; ey <= y1; ey++)
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, ey, val);
    commit_undo_action();
    last_action = event->keyval;
    last_was_visual = TRUE;
    last_visual_dx = visual_anchor_x - cursor_x;
    last_visual_dy = visual_anchor_y - cursor_y;
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_backslash) {
    begin_undo_action();
    draw_line(visual_anchor_x, visual_anchor_y, cursor_x, cursor_y, fg_color);
    commit_undo_action();
    last_action = GDK_KEY_backslash;
    last_was_visual = TRUE;
    last_visual_dx = visual_anchor_x - cursor_x;
    last_visual_dy = visual_anchor_y - cursor_y;
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode &&
      (event->keyval == GDK_KEY_H || event->keyval == GDK_KEY_V)) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    begin_undo_action();
    if (event->keyval == GDK_KEY_H) {
      /* Flip selection horizontally */
      for (int ey = y0; ey <= y1; ey++) {
        for (int ex = x0; ex <= x0 + (x1 - x0) / 2; ex++) {
          int ex2 = x1 - (ex - x0);
          if (ex == ex2)
            continue;
          push_undo(ex, ey);
          push_undo(ex2, ey);
          guint32 tmp = PX(ey, ex);
          PX(ey, ex) = PX(ey, ex2);
          PX(ey, ex2) = tmp;
        }
      }
    } else {
      /* Flip selection vertically */
      for (int ey = y0; ey <= y0 + (y1 - y0) / 2; ey++) {
        int ey2 = y1 - (ey - y0);
        if (ey == ey2)
          continue;
        for (int ex = x0; ex <= x1; ex++) {
          push_undo(ex, ey);
          push_undo(ex, ey2);
          guint32 tmp = PX(ey, ex);
          PX(ey, ex) = PX(ey2, ex);
          PX(ey2, ex) = tmp;
        }
      }
    }
    commit_undo_action();
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_R) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    int W = x1 - x0 + 1, H = y1 - y0 + 1;
    guint32 *tmp = malloc((size_t)W * H * sizeof(guint32));
    if (!tmp) {
      cmd_flash("Out of memory.");
      visual_mode = FALSE;
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    /* 90° CW: dst[dr][dc] = src[H-1-dc][dr], new dims = H wide × W tall */
    for (int dr = 0; dr < W; dr++)
      for (int dc = 0; dc < H; dc++)
        tmp[dr * H + dc] = PX(y0 + (H - 1 - dc), x0 + dr);
    begin_undo_action();
    for (int dr = 0; dr < W && y0 + dr < CANVAS_H; dr++)
      for (int dc = 0; dc < H && x0 + dc < CANVAS_W; dc++)
        paint_pixel(x0 + dc, y0 + dr, tmp[dr * H + dc]);
    free(tmp);
    commit_undo_action();
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode &&
      (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal)) {
    int amount = MAX(1, n);
    int x0 = CLAMP(MIN(cursor_x, visual_anchor_x) - amount, 0, CANVAS_W - 1);
    int x1 = CLAMP(MAX(cursor_x, visual_anchor_x) + amount, 0, CANVAS_W - 1);
    int y0 = CLAMP(MIN(cursor_y, visual_anchor_y) - amount, 0, CANVAS_H - 1);
    int y1 = CLAMP(MAX(cursor_y, visual_anchor_y) + amount, 0, CANVAS_H - 1);
    visual_anchor_x = x0;
    visual_anchor_y = y0;
    cursor_x = x1;
    cursor_y = y1;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_minus) {
    int amount = MAX(1, n);
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    x0 = MIN(x0 + amount, (x0 + x1) / 2);
    x1 = MAX(x1 - amount, (x0 + x1) / 2);
    y0 = MIN(y0 + amount, (y0 + y1) / 2);
    y1 = MAX(y1 - amount, (y0 + y1) / 2);
    visual_anchor_x = x0;
    visual_anchor_y = y0;
    cursor_x = x1;
    cursor_y = y1;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (event->keyval == GDK_KEY_s && (event->state & GDK_CONTROL_MASK)) {
    if (*last_filename)
      cmd_write(last_filename);
    else
      cmd_flash("No filename. Use :w filename");
    return TRUE;
  }

  if (event->keyval == GDK_KEY_g && (event->state & GDK_CONTROL_MASK)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "\"%s\"  %dx%d  col: %d  row: %d",
             *last_filename ? last_filename : "[No Name]", CANVAS_W, CANVAS_H,
             cursor_x + 1, cursor_y + 1);
    cmd_flash(buf);
    return TRUE;
  }

  if (event->keyval == GDK_KEY_r && (event->state & GDK_CONTROL_MASK)) {
    if (redo_count > 0) {
      redo_top--;
      redo_count--;
      int ridx = redo_top % undo_levels;
      UndoAction a = redo_stack[ridx];
      redo_stack[ridx] = (UndoAction){NULL, 0};
      int uidx = undo_top % undo_levels;
      if (undo_count == undo_levels)
        free_action(&undo_stack[uidx]);
      undo_stack[uidx] = a;
      undo_top++;
      if (undo_count < undo_levels)
        undo_count++;
      if (a.after_snap) {
        guint32 *np = malloc(a.after_w * a.after_h * sizeof(guint32));
        if (np) {
          memcpy(np, a.after_snap, a.after_w * a.after_h * sizeof(guint32));
          free(pixels);
          pixels = np;
          layer_bufs[layer_active] = pixels;
          CANVAS_W = a.after_w;
          CANVAS_H = a.after_h;
          cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
          cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
          zoom_resize();
        }
      } else {
        for (int i = 0; i < a.count; i++) {
          PX(a.changes[i].y, a.changes[i].x) = a.changes[i].after;
          cursor_x = a.changes[i].x;
          cursor_y = a.changes[i].y;
        }
      }
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
    }
    return TRUE;
  }

  if (event->keyval == GDK_KEY_g) {
    pending_g = TRUE;
    gg_count = orig_count;
    return TRUE;
  }

  switch (event->keyval) {
  case GDK_KEY_q:
    pending_q = TRUE;
    return TRUE;
  case GDK_KEY_at:
    pending_at = TRUE;
    return TRUE;
  case GDK_KEY_plus:
  case GDK_KEY_equal:
    if (CELL_SIZE < 64) {
      CELL_SIZE += 2;
      zoom_resize();
    }
    break;
  case GDK_KEY_minus:
    if (CELL_SIZE > 4) {
      CELL_SIZE -= 2;
      zoom_resize();
    }
    break;
  case GDK_KEY_bar:
    show_grid = !show_grid;
    break;
  case GDK_KEY_percent:
    show_ruler = !show_ruler;
    break;
  case GDK_KEY_numbersign:
    show_checker = !show_checker;
    break;
  case GDK_KEY_i:
    insert_mode = TRUE;
    break;
  case GDK_KEY_H:
    cursor_x = MAX(cursor_x - 5 * n, 0);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_h:
  case GDK_KEY_Left:
    cursor_x = MAX(cursor_x - n, 0);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_L:
    cursor_x = MIN(cursor_x + 5 * n, CANVAS_W - 1);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_l:
  case GDK_KEY_Right:
    cursor_x = MIN(cursor_x + n, CANVAS_W - 1);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_K:
    cursor_y = MAX(cursor_y - 5 * n, 0);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_k:
  case GDK_KEY_Up:
    cursor_y = MAX(cursor_y - n, 0);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_J:
    cursor_y = MIN(cursor_y + 5 * n, CANVAS_H - 1);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_j:
  case GDK_KEY_Down:
    cursor_y = MIN(cursor_y + n, CANVAS_H - 1);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_G:
    cursor_y =
        orig_count > 0 ? CLAMP(orig_count - 1, 0, CANVAS_H - 1) : CANVAS_H - 1;
    break;
  case GDK_KEY_0:
    cursor_x = 0;
    break;
  case GDK_KEY_dollar:
    cursor_x = CANVAS_W - 1;
    break;
  case GDK_KEY_w:
    cursor_x = MIN(cursor_x + 5 * n, CANVAS_W - 1);
    break;
  case GDK_KEY_b:
    cursor_x = MAX(cursor_x - 5 * n, 0);
    break;
  case GDK_KEY_f:
    pending_f = TRUE;
    f_count = n;
    return TRUE;
  case GDK_KEY_n:
    if (last_find_dir == 1)
      find_right();
    else if (last_find_dir == -1)
      find_left();
    else if (last_find_dir == 2)
      find_down();
    else if (last_find_dir == -2)
      find_up();
    break;
  case GDK_KEY_N:
    if (last_find_dir == 1)
      find_left();
    else if (last_find_dir == -1)
      find_right();
    else if (last_find_dir == 2)
      find_up();
    else if (last_find_dir == -2)
      find_down();
    break;
  case GDK_KEY_d:
    pending_d = TRUE;
    d_count = n;
    return TRUE;
  case GDK_KEY_c: {
    /* Find the palette slot that matches fg_color, then cycle forward */
    if (PALETTE_SIZE <= 1)
      break;
    int cur_slot = -1;
    for (int i = 0; i < PALETTE_SIZE; i++) {
      int pr, pg, pb;
      palette_to_rgb(i, &pr, &pg, &pb);
      if (PACK_RGBA(pr, pg, pb, 255) == fg_color) {
        cur_slot = i;
        break;
      }
    }
    if (cur_slot < 1)
      cur_slot = 1;
    int next_slot = (cur_slot % (PALETTE_SIZE - 1)) + 1;
    int pr, pg, pb;
    palette_to_rgb(next_slot, &pr, &pg, &pb);
    fg_color = PACK_RGBA(pr, pg, pb, 255);
    gtk_widget_queue_draw(palette_bar);
    flash_color(next_slot);
    break;
  }
  case GDK_KEY_C: {
    if (PALETTE_SIZE <= 1)
      break;
    int cur_slot = -1;
    for (int i = 0; i < PALETTE_SIZE; i++) {
      int pr, pg, pb;
      palette_to_rgb(i, &pr, &pg, &pb);
      if (PACK_RGBA(pr, pg, pb, 255) == fg_color) {
        cur_slot = i;
        break;
      }
    }
    if (cur_slot < 1)
      cur_slot = 1;
    int prev_slot =
        (cur_slot - 2 + (PALETTE_SIZE - 1)) % (PALETTE_SIZE - 1) + 1;
    int pr, pg, pb;
    palette_to_rgb(prev_slot, &pr, &pg, &pb);
    fg_color = PACK_RGBA(pr, pg, pb, 255);
    gtk_widget_queue_draw(palette_bar);
    flash_color(prev_slot);
    break;
  }
  case GDK_KEY_e: {
    if (visual_mode) {
      /* ellipse outline fitting selection */
      int vx0 = MIN(cursor_x, visual_anchor_x);
      int vx1 = MAX(cursor_x, visual_anchor_x);
      int vy0 = MIN(cursor_y, visual_anchor_y);
      int vy1 = MAX(cursor_y, visual_anchor_y);
      int ecx = (vx0 + vx1) / 2, ecy = (vy0 + vy1) / 2;
      int erx = MAX(1, (vx1 - vx0 + 1) / 2);
      int ery = MAX(1, (vy1 - vy0 + 1) / 2);
      begin_undo_action();
#define EPSET(px, py)                                                          \
  do {                                                                         \
    int _ex = (px), _ey = (py);                                                \
    if (_ex >= 0 && _ex < CANVAS_W && _ey >= 0 && _ey < CANVAS_H)              \
      paint_pixel(_ex, _ey, fg_color);                                         \
  } while (0)
      /* scan x: fills left/right arcs */
      for (int oex = -erx; oex <= erx; oex++) {
        double t = 1.0 - (double)oex * oex / ((double)erx * erx);
        int dy = (int)round(ery * sqrt(MAX(0.0, t)));
        EPSET(ecx + oex, ecy + dy);
        EPSET(ecx + oex, ecy - dy);
      }
      /* scan y: fills top/bottom arcs */
      for (int oey = -ery; oey <= ery; oey++) {
        double t = 1.0 - (double)oey * oey / ((double)ery * ery);
        int dx = (int)round(erx * sqrt(MAX(0.0, t)));
        EPSET(ecx + dx, ecy + oey);
        EPSET(ecx - dx, ecy + oey);
      }
#undef EPSET
      commit_undo_action();
      visual_mode = FALSE;
      gtk_widget_queue_draw(main_canvas);
      status_update();
    } else {
      fg_color = PX(cursor_y, cursor_x); /* eyedropper */
      gtk_widget_queue_draw(palette_bar);
      guchar er = (fg_color >> 24) & 0xff;
      guchar eg = (fg_color >> 16) & 0xff;
      guchar eb = (fg_color >> 8) & 0xff;
      char ebuf[32];
      snprintf(ebuf, sizeof(ebuf), "color #%02x%02x%02x", er, eg, eb);
      cmd_flash(ebuf);
    }
    break;
  }
  case GDK_KEY_o: {
    int cx = cursor_x, cy = cursor_y, r = radius;
    begin_undo_action();
    if (r == 0) {
      paint_pixel(cx, cy, fg_color);
    } else {
      int bx = r, by = 0, err = 0;
#define CPSET(px, py)                                                          \
  do {                                                                         \
    paint_pixel((px), (py), fg_color);                                         \
  } while (0)
      while (bx >= by) {
        CPSET(cx + bx, cy + by);
        CPSET(cx + by, cy + bx);
        CPSET(cx - by, cy + bx);
        CPSET(cx - bx, cy + by);
        CPSET(cx - bx, cy - by);
        CPSET(cx - by, cy - bx);
        CPSET(cx + by, cy - bx);
        CPSET(cx + bx, cy - by);
        if (err <= 0) {
          by++;
          err += 2 * by + 1;
        }
        if (err > 0) {
          bx--;
          err -= 2 * bx + 1;
        }
      }
#undef CPSET
    }
    commit_undo_action();
    break;
  }
  case GDK_KEY_O: {
    int cx = cursor_x, cy = cursor_y, r = radius;
    begin_undo_action();
    for (int ey = cy - r; ey <= cy + r; ey++)
      for (int ex = cx - r; ex <= cx + r; ex++)
        if ((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy) <= r * r && ex >= 0 &&
            ex < CANVAS_W && ey >= 0 && ey < CANVAS_H)
          paint_pixel(ex, ey, fg_color);
    commit_undo_action();
    break;
  }
  case GDK_KEY_E: {
    int cx, cy, rx, ry;
    if (visual_mode) {
      int vx0 = MIN(cursor_x, visual_anchor_x);
      int vx1 = MAX(cursor_x, visual_anchor_x);
      int vy0 = MIN(cursor_y, visual_anchor_y);
      int vy1 = MAX(cursor_y, visual_anchor_y);
      cx = (vx0 + vx1) / 2;
      cy = (vy0 + vy1) / 2;
      rx = (vx1 - vx0 + 1) / 2;
      ry = (vy1 - vy0 + 1) / 2;
    } else {
      cx = cursor_x;
      cy = cursor_y;
      rx = MAX(1, radius);
      ry = ellipse_ry > 0 ? ellipse_ry : rx;
    }
    if (rx < 1)
      rx = 1;
    if (ry < 1)
      ry = 1;
    begin_undo_action();
    /* filled ellipse: scan lines */
    for (int ey = cy - ry; ey <= cy + ry; ey++) {
      if (ey < 0 || ey >= CANVAS_H)
        continue;
      double dy = (double)(ey - cy) / ry;
      double dx_f = rx * sqrt(MAX(0.0, 1.0 - dy * dy));
      int ex0 = CLAMP((int)(cx - dx_f), 0, CANVAS_W - 1);
      int ex1 = CLAMP((int)(cx + dx_f), 0, CANVAS_W - 1);
      for (int ex = ex0; ex <= ex1; ex++)
        paint_pixel(ex, ey, fg_color);
    }
    commit_undo_action();
    if (visual_mode) {
      visual_mode = FALSE;
      gtk_widget_queue_draw(main_canvas);
      status_update();
    }
    break;
  }
  case GDK_KEY_space:
  case GDK_KEY_r: {
    RECT_BOUNDS(radius);
    begin_undo_action();
    for (int ey = y0; ey <= y1; ey++)
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, ey, fg_color);
    commit_undo_action();
    last_action = GDK_KEY_r;
    last_radius = radius;
    last_was_visual = FALSE;
    break;
  }
  case GDK_KEY_R: {
    RECT_BOUNDS(radius);
    begin_undo_action();
    for (int ex = x0; ex <= x1; ex++) {
      paint_pixel(ex, y0, fg_color);
      paint_pixel(ex, y1, fg_color);
    }
    for (int ey = y0 + 1; ey < y1; ey++) {
      paint_pixel(x0, ey, fg_color);
      paint_pixel(x1, ey, fg_color);
    }
    commit_undo_action();
    last_action = GDK_KEY_R;
    last_radius = radius;
    last_was_visual = FALSE;
    break;
  }
  case GDK_KEY_D:
    begin_undo_action();
    for (int ex = cursor_x; ex < CANVAS_W; ex++)
      paint_pixel(ex, cursor_y, bg_color);
    commit_undo_action();
    last_action = GDK_KEY_x;
    last_was_visual = FALSE;
    break;
  case GDK_KEY_S:
    flood_fill(cursor_x, cursor_y, fg_color);
    break;
  case GDK_KEY_x: {
    RECT_BOUNDS(radius);
    begin_undo_action();
    for (int ey = y0; ey <= y1; ey++)
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, ey, bg_color);
    commit_undo_action();
    last_action = GDK_KEY_x;
    last_radius = radius;
    last_was_visual = FALSE;
    break;
  }
  case GDK_KEY_period:
    if (last_was_visual) {
      int ax = cursor_x + last_visual_dx;
      int ay = cursor_y + last_visual_dy;
      begin_undo_action();
      if (last_action == GDK_KEY_backslash) {
        draw_line(ax, ay, cursor_x, cursor_y, fg_color);
      } else {
        guint32 val = (last_action == GDK_KEY_r) ? fg_color : bg_color;
        int x0 = CLAMP(MIN(cursor_x, ax), 0, CANVAS_W - 1);
        int x1 = CLAMP(MAX(cursor_x, ax), 0, CANVAS_W - 1);
        int y0 = CLAMP(MIN(cursor_y, ay), 0, CANVAS_H - 1);
        int y1 = CLAMP(MAX(cursor_y, ay), 0, CANVAS_H - 1);
        for (int ey = y0; ey <= y1; ey++)
          for (int ex = x0; ex <= x1; ex++)
            paint_pixel(ex, ey, val);
      }
      commit_undo_action();
    } else if (last_action == GDK_KEY_r || last_action == GDK_KEY_x ||
               last_action == GDK_KEY_R) {
      RECT_BOUNDS(last_radius);
      begin_undo_action();
      if (last_action == GDK_KEY_R) {
        for (int ex = x0; ex <= x1; ex++) {
          paint_pixel(ex, y0, fg_color);
          paint_pixel(ex, y1, fg_color);
        }
        for (int ey = y0 + 1; ey < y1; ey++) {
          paint_pixel(x0, ey, fg_color);
          paint_pixel(x1, ey, fg_color);
        }
      } else {
        guint32 val = (last_action == GDK_KEY_r) ? fg_color : bg_color;
        for (int ey = y0; ey <= y1; ey++)
          for (int ex = x0; ex <= x1; ex++)
            paint_pixel(ex, ey, val);
      }
      commit_undo_action();
    }
    break;
  case GDK_KEY_p:
    if (!yank_buf) {
      cmd_flash("Nothing yanked.");
    } else {
      begin_undo_action();
      for (int ey = 0; ey < yank_h; ey++)
        for (int ex = 0; ex < yank_w; ex++) {
          int px = cursor_x + ex, py = cursor_y + ey;
          if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
            push_undo(px, py);
            PX(py, px) = yank_buf[ey * yank_w + ex];
          }
        }
      commit_undo_action();
    }
    break;
  case GDK_KEY_P:
    if (!yank_buf) {
      cmd_flash("Nothing yanked.");
    } else {
      int ox = cursor_x - yank_w / 2;
      int oy = cursor_y - yank_h / 2;
      begin_undo_action();
      for (int ey = 0; ey < yank_h; ey++)
        for (int ex = 0; ex < yank_w; ex++) {
          int px = ox + ex, py = oy + ey;
          if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
            push_undo(px, py);
            PX(py, px) = yank_buf[ey * yank_w + ex];
          }
        }
      commit_undo_action();
    }
    break;
  case GDK_KEY_u:
    if (undo_count > 0) {
      undo_top--;
      undo_count--;
      int uidx = undo_top % undo_levels;
      UndoAction a = undo_stack[uidx];
      undo_stack[uidx] = (UndoAction){NULL, 0};
      int ridx = redo_top % undo_levels;
      if (redo_count == undo_levels)
        free_action(&redo_stack[ridx]);
      redo_stack[ridx] = a;
      redo_top++;
      if (redo_count < undo_levels)
        redo_count++;
      if (a.before_snap) {
        guint32 *np = malloc(a.before_w * a.before_h * sizeof(guint32));
        if (np) {
          memcpy(np, a.before_snap, a.before_w * a.before_h * sizeof(guint32));
          free(pixels);
          pixels = np;
          layer_bufs[layer_active] = pixels;
          CANVAS_W = a.before_w;
          CANVAS_H = a.before_h;
          cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
          cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
          zoom_resize();
        }
      } else {
        for (int i = a.count - 1; i >= 0; i--) {
          PX(a.changes[i].y, a.changes[i].x) = a.changes[i].before;
          cursor_x = a.changes[i].x;
          cursor_y = a.changes[i].y;
        }
      }
    }
    break;
  default:
    return FALSE;
  }

  status_update();
  gtk_widget_queue_draw(GTK_WIDGET(data));
  return TRUE;
}
