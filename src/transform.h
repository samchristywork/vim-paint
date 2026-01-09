#ifndef VIMPAINT_TRANSFORM_H
#define VIMPAINT_TRANSFORM_H

#include "state.h"

void exec_find_color(const char *arg);
void exec_goto(const char *arg);
void exec_scale(const char *arg);
void exec_fliph(const char *arg);
void exec_flipv(const char *arg);
void exec_rotate(const char *arg);
void exec_resize(const char *arg);
void exec_center(const char *arg);
void exec_crop(const char *arg);
void exec_stroke(const char *arg);
void exec_text(const char *arg);

#endif
