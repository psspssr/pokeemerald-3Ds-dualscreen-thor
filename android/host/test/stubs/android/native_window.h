#pragma once
/* Platform I/O declarations only; shared host state is the production code. */
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_acquire(ANativeWindow *window);
void ANativeWindow_release(ANativeWindow *window);
