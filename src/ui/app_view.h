/* Drawing only. No protocol logic and no decisions about what a frame means.
 *
 * The draw function acquires the app mutex itself, since it reads state the
 * radio thread writes. */
#ifndef APP_VIEW_H
#define APP_VIEW_H

#include "src/app.h"

void app_view_draw(Canvas* canvas, void* context);

/* Number of rows the current page can scroll through, so input handling does
 * not need to know each page's layout. */
size_t app_view_row_count(MeshApp* app);

#endif
