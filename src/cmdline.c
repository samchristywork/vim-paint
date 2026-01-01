#include "cmdline.h"
#include "commands.h"
#include "main.h"

gboolean cmdline_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
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
