#include "src/ui/app_view.h"

#include <stdio.h>
#include <string.h>

/* 128x64 display. FontSecondary is about 6px tall, so 10px rows leave four
 * body lines under a header. */
#define ROW_HEIGHT 10
#define BODY_TOP   22
#define BODY_ROWS  4

static const char* page_title(AppPage page) {
    switch(page) {
    case PageMessages:
        return "Messages";
    case PageNodes:
        return "Nodes";
    case PageStats:
        return "Stats";
    case PageCount:
    default:
        return "";
    }
}

static void draw_header(Canvas* canvas, MeshApp* app, uint32_t total) {
    char line[32];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, page_title(app->page));

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "%lu rx", (unsigned long)total);
    canvas_draw_str(canvas, 84, 10, line);

    canvas_draw_line(canvas, 0, 12, 127, 12);
}

/* Node numbers are shown as the low 4 hex digits. The full 32 bits do not fit
 * alongside anything useful, and the low bytes are what distinguishes nodes in
 * practice. */
static void format_node(char* out, size_t out_len, uint32_t node) {
    snprintf(out, out_len, "%04lx", (unsigned long)(node & 0xFFFF));
}

static void draw_messages(Canvas* canvas, MeshApp* app) {
    char line[40];
    size_t count = message_ring_count(&app->messages);

    if(count == 0) {
        canvas_draw_str(canvas, 2, BODY_TOP, "Nothing received yet");
        canvas_draw_str(canvas, 2, BODY_TOP + ROW_HEIGHT, app->source->name);
        return;
    }

    for(size_t row = 0; row < BODY_ROWS; row++) {
        size_t index = app->scroll + row;
        if(index >= count) break;

        const MeshMessage* m = message_ring_get(&app->messages, index);
        if(m == NULL) break;

        char who[8];
        char text[28];
        size_t len = m->text_len < sizeof(text) - 1 ? m->text_len : sizeof(text) - 1;

        format_node(who, sizeof(who), m->from);
        memcpy(text, m->text, len);
        text[len] = '\0';

        /* Replace anything unprintable so a bad decode cannot scribble on the
         * display with control characters. */
        for(size_t i = 0; i < len; i++) {
            if(text[i] < 0x20 || (unsigned char)text[i] > 0x7E) text[i] = '.';
        }

        snprintf(line, sizeof(line), "%s %s%s", who, text, m->text_truncated ? "~" : "");
        canvas_draw_str(canvas, 2, BODY_TOP + (int)row * ROW_HEIGHT, line);
    }
}

static void draw_nodes(Canvas* canvas, MeshApp* app) {
    char line[40];
    size_t count = node_roster_count(&app->roster);

    if(count == 0) {
        canvas_draw_str(canvas, 2, BODY_TOP, "No nodes heard");
        return;
    }

    for(size_t row = 0; row < BODY_ROWS; row++) {
        size_t index = app->scroll + row;
        if(index >= count) break;

        const MeshNode* n = node_roster_get(&app->roster, index);
        if(n == NULL) break;

        char who[8];
        format_node(who, sizeof(who), n->node_num);
        snprintf(
            line,
            sizeof(line),
            "%s %ddBm %d x%lu",
            who,
            (int)n->rssi,
            (int)n->snr,
            (unsigned long)n->packets);
        canvas_draw_str(canvas, 2, BODY_TOP + (int)row * ROW_HEIGHT, line);
    }
}

static void draw_stats(Canvas* canvas, MeshApp* app) {
    char line[44];

    snprintf(
        line, sizeof(line), "Src: %s%s", app->source->name, app->source_is_radio ? "" : " (sim)");
    canvas_draw_str(canvas, 2, BODY_TOP, line);

    snprintf(
        line,
        sizeof(line),
        "ok %lu  chan %lu  proto %lu",
        (unsigned long)app->counters[MESH_OK],
        (unsigned long)app->counters[MESH_ERR_CHANNEL_MISMATCH],
        (unsigned long)app->counters[MESH_ERR_BAD_PROTOBUF]);
    canvas_draw_str(canvas, 2, BODY_TOP + ROW_HEIGHT, line);

    snprintf(
        line,
        sizeof(line),
        "short %lu notxt %lu crc %lu",
        (unsigned long)app->counters[MESH_ERR_TOO_SHORT],
        (unsigned long)app->counters[MESH_ERR_NOT_TEXT],
        (unsigned long)app->crc_errors);
    canvas_draw_str(canvas, 2, BODY_TOP + 2 * ROW_HEIGHT, line);

    /* First bytes of the last frame, which is the header. Enough to eyeball
     * whether what arrived looks like Meshtastic at all. */
    if(app->last_raw_len > 0) {
        char hex[44];
        size_t shown = app->last_raw_len < 10 ? app->last_raw_len : 10;
        size_t pos = 0;
        for(size_t i = 0; i < shown && pos + 3 < sizeof(hex); i++) {
            pos += (size_t)snprintf(hex + pos, sizeof(hex) - pos, "%02x", app->last_raw[i]);
        }
        hex[pos] = '\0';
        canvas_draw_str(canvas, 2, BODY_TOP + 3 * ROW_HEIGHT, hex);
    }
}

void app_view_draw(Canvas* canvas, void* context) {
    MeshApp* app = context;

    canvas_clear(canvas);

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    draw_header(canvas, app, app->total_frames);
    canvas_set_font(canvas, FontSecondary);

    switch(app->page) {
    case PageMessages:
        draw_messages(canvas, app);
        break;
    case PageNodes:
        draw_nodes(canvas, app);
        break;
    case PageStats:
        draw_stats(canvas, app);
        break;
    case PageCount:
    default:
        break;
    }

    furi_mutex_release(app->mutex);

    canvas_draw_line(canvas, 0, 53, 127, 53);
    canvas_draw_str(canvas, 2, 62, "< >  page");
    canvas_draw_str(canvas, 86, 62, "Back");
}

size_t app_view_row_count(MeshApp* app) {
    size_t rows = 0;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    switch(app->page) {
    case PageMessages:
        rows = message_ring_count(&app->messages);
        break;
    case PageNodes:
        rows = node_roster_count(&app->roster);
        break;
    case PageStats:
    case PageCount:
    default:
        rows = 0;
        break;
    }
    furi_mutex_release(app->mutex);

    return rows;
}
