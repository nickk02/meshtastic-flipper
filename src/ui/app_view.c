/* Frames modelled on the Meshtastic device UI.
 *
 * Layouts follow the firmware's own renderers rather than being invented:
 *
 *   drawCommonHeader        SharedUIDisplay.cpp:104
 *   drawDeviceFocused       UIRenderer.cpp
 *   drawTextMessageFrame    MessageRenderer.cpp
 *   drawLastHeardScreen     NodeListRenderer.cpp
 *   drawHopSignalScreen     NodeListRenderer.cpp
 *   drawLoRaFocused         DebugRenderer.cpp
 *
 * Real nodes cycle frames with a button; here that is Left and Right. The
 * Flipper's 128x64 screen matches the OLED most Meshtastic devices use, so the
 * layouts carry over almost directly.
 *
 * Drawing only. No protocol logic. */
#include "src/ui/app_view.h"

#include "src/ble/meshtastic_service.h"

#include <furi_hal_power.h>
#include <datetime/datetime.h>
#include <furi_hal_rtc.h>
#include <stdio.h>
#include <string.h>

/* A filled header bar, as on a real device, then body rows below it. */
#define HEADER_H  13
#define ROW_H     10
#define BODY_TOP  23
#define BODY_ROWS 3
#define SCREEN_W  128

static const char* page_title(AppPage page) {
    switch(page) {
    case PageHome:
        return "Home";
    case PageMessages:
        return "Messages";
    case PageNodesHeard:
        return "Nodes";
    case PageNodesSignal:
        return "Signal";
    case PageLora:
        return "LoRa";
    case PagePhone:
        return "Phone";
    case PageCount:
    default:
        return "";
    }
}

/* drawCommonHeader: battery percent left, centred title, clock right, on an
 * inverted bar. */
static void draw_header(Canvas* canvas, MeshApp* app) {
    char left[16];
    char right[16];
    DateTime now;

    canvas_draw_box(canvas, 0, 0, SCREEN_W, HEADER_H);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);

    snprintf(left, sizeof(left), "%u%%", (unsigned)furi_hal_power_get_pct());
    canvas_draw_str(canvas, 2, 10, left);

    canvas_draw_str_aligned(
        canvas, SCREEN_W / 2, 10, AlignCenter, AlignBottom, page_title(app->page));

    furi_hal_rtc_get_datetime(&now);
    snprintf(right, sizeof(right), "%02u:%02u", now.hour, now.minute);
    canvas_draw_str_aligned(canvas, SCREEN_W - 2, 10, AlignRight, AlignBottom, right);

    canvas_set_color(canvas, ColorBlack);
}

/* "45s", "5m", "2h", "3d", or "?" when nothing has been heard. Matches
 * drawEntryLastHeard, which prints a number and a unit letter. */
static void format_age(char* out, size_t cap, uint32_t last_seen_ms, uint32_t now_ms) {
    if(last_seen_ms == 0 || now_ms < last_seen_ms) {
        snprintf(out, cap, "?");
        return;
    }

    uint32_t seconds = (now_ms - last_seen_ms) / 1000;
    if(seconds < 60) {
        snprintf(out, cap, "%lus", (unsigned long)seconds);
    } else if(seconds < 3600) {
        snprintf(out, cap, "%lum", (unsigned long)(seconds / 60));
    } else if(seconds < 86400) {
        snprintf(out, cap, "%luh", (unsigned long)(seconds / 3600));
    } else {
        snprintf(out, cap, "%lud", (unsigned long)(seconds / 86400));
    }
}

static void draw_home(Canvas* canvas, MeshApp* app) {
    char line[32];
    uint32_t uptime_s = furi_get_tick() / furi_kernel_get_tick_frequency();

    /* Row 1: node count left, uptime right. */
    snprintf(line, sizeof(line), "%u online", (unsigned)node_roster_count(&app->roster));
    canvas_draw_str(canvas, 2, BODY_TOP, line);

    if(uptime_s < 3600) {
        snprintf(line, sizeof(line), "Up: %lum", (unsigned long)(uptime_s / 60));
    } else {
        snprintf(
            line,
            sizeof(line),
            "Up: %luh%lum",
            (unsigned long)(uptime_s / 3600),
            (unsigned long)((uptime_s % 3600) / 60));
    }
    canvas_draw_str_aligned(canvas, SCREEN_W - 2, BODY_TOP, AlignRight, AlignBottom, line);

    /* Row 2: radio state. A real device shows battery voltage here, but the
     * Flipper's own status bar already carries that, so this row shows the
     * thing the user cannot otherwise see. */
    snprintf(line, sizeof(line), "Radio: %s", app->source_started ? "SX1262" : "not found");
    canvas_draw_str(canvas, 2, BODY_TOP + ROW_H, line);

    /* Row 3: frames received. A real device draws a channel utilization bar
     * here. That needs airtime accounting we do not have, and inventing a
     * percentage would be worse than showing a real count. */
    snprintf(
        line,
        sizeof(line),
        "Rx: %lu  BLE: %s",
        (unsigned long)app->total_frames,
        meshtastic_ble_state_name(meshtastic_ble_state()));
    canvas_draw_str(canvas, 2, BODY_TOP + 2 * ROW_H, line);

    /* Our own name, centred, as drawDeviceFocused does. */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 62, AlignCenter, AlignBottom, app->node_name);
    canvas_set_font(canvas, FontSecondary);
    /* Row 4: heap. RAM is this project's governing constraint, so the number
     * that decides what else can be added belongs on screen rather than in an
     * estimate.
     *
     * Free is the heap right now. Low is memmgr_get_minimum_free_heap, the
     * smallest free heap since boot, which is the high-water mark of everything
     * this app has allocated including the BLE stack's buffers. Low is the
     * figure to budget against, not Free. */
    snprintf(
        line,
        sizeof(line),
        "Heap:%luk Low:%luk",
        (unsigned long)(memmgr_get_free_heap() / 1024),
        (unsigned long)(memmgr_get_minimum_free_heap() / 1024));
    canvas_draw_str(canvas, 2, BODY_TOP + 3 * ROW_H, line);
}

static void draw_messages(Canvas* canvas, MeshApp* app) {
    char line[40];
    size_t count = message_ring_count(&app->messages);

    if(count == 0) {
        /* An empty list looks the same whether the channel is quiet or the
         * radio never started. Say which. */
        if(!app->source_started) {
            canvas_draw_str(canvas, 2, BODY_TOP, "No SX1262 detected");
            canvas_draw_str(canvas, 2, BODY_TOP + ROW_H, "Check the add-on board");
        } else {
            canvas_draw_str(canvas, 2, BODY_TOP, "Listening, nothing yet");
        }
        return;
    }

    for(size_t row = 0; row < BODY_ROWS; row++) {
        size_t index = app->scroll + row;
        if(index >= count) break;

        const MeshMessage* m = message_ring_get(&app->messages, index);
        if(m == NULL) break;

        char text[30];
        size_t len = m->text_len < sizeof(text) - 1 ? m->text_len : sizeof(text) - 1;
        memcpy(text, m->text, len);
        text[len] = '\0';

        /* Anything unprintable becomes a dot, so a bad decode cannot scribble
         * control characters across the display. */
        for(size_t i = 0; i < len; i++) {
            if(text[i] < 0x20 || (unsigned char)text[i] > 0x7E) text[i] = '.';
        }

        snprintf(
            line,
            sizeof(line),
            "%04lx %s%s",
            (unsigned long)(m->from & 0xFFFF),
            text,
            m->text_truncated ? "~" : "");
        canvas_draw_str(canvas, 2, BODY_TOP + (int)row * ROW_H, line);
    }
}

/* drawLastHeardScreen: name on the left, time since heard on the right. */
static void draw_nodes_heard(Canvas* canvas, MeshApp* app) {
    char scratch[16];
    char age[8];
    size_t count = node_roster_count(&app->roster);
    uint32_t now = furi_get_tick();

    if(count == 0) {
        canvas_draw_str(canvas, 2, BODY_TOP, "No nodes heard");
        return;
    }

    for(size_t row = 0; row < BODY_ROWS; row++) {
        size_t index = app->scroll + row;
        if(index >= count) break;

        const MeshNode* n = node_roster_get(&app->roster, index);
        if(n == NULL) break;

        int y = BODY_TOP + (int)row * ROW_H;
        canvas_draw_str(canvas, 2, y, node_roster_display_name(n, scratch, sizeof(scratch)));

        format_age(age, sizeof(age), n->last_seen_ms, now);
        canvas_draw_str_aligned(canvas, SCREEN_W - 2, y, AlignRight, AlignBottom, age);
    }
}

/* drawHopSignalScreen: name, then signal bars for direct neighbours or a hop
 * count for anything relayed. Signal only appears at zero hops, because on a
 * relayed frame RSSI describes the last hop rather than the sender. */
static void draw_nodes_signal(Canvas* canvas, MeshApp* app) {
    char scratch[16];
    char detail[16];
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

        int y = BODY_TOP + (int)row * ROW_H;
        canvas_draw_str(canvas, 2, y, node_roster_display_name(n, scratch, sizeof(scratch)));

        if(n->has_hops && n->hops_away > 0) {
            snprintf(detail, sizeof(detail), "%u hops", (unsigned)n->hops_away);
            canvas_draw_str_aligned(canvas, SCREEN_W - 2, y, AlignRight, AlignBottom, detail);
        } else {
            /* Four bars. Thresholds from drawEntryHopSignal. */
            int bars = (n->snr > 5)   ? 4 :
                       (n->snr > 0)   ? 3 :
                       (n->snr > -5)  ? 2 :
                       (n->snr > -10) ? 1 :
                                        0;
            for(int b = 0; b < 4; b++) {
                int h = 2 + b * 2;
                int bx = SCREEN_W - 20 + b * 4;
                if(b < bars) {
                    canvas_draw_box(canvas, bx, y - h, 3, h);
                } else {
                    canvas_draw_frame(canvas, bx, y - h, 3, h);
                }
            }
        }
    }
}

/* drawLoRaFocused: BLE id, role, region and modem, then frequency and slot. */
static void draw_lora(Canvas* canvas, MeshApp* app) {
    char line[36];

    snprintf(line, sizeof(line), "BLE: %s", app->ble_id);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, BODY_TOP, AlignCenter, AlignBottom, line);

    /* Every BLE failure mode here is otherwise silent, so the state goes on
     * screen rather than only into the log. */
    snprintf(line, sizeof(line), "Adv: %s", meshtastic_ble_state_name(meshtastic_ble_state()));
    canvas_draw_str_aligned(
        canvas, SCREEN_W / 2, BODY_TOP + ROW_H, AlignCenter, AlignBottom, line);

    snprintf(
        line,
        sizeof(line),
        "US/BW%u-SF%u-CR%u",
        (unsigned)app->lora.bw_khz,
        (unsigned)app->lora.sf,
        (unsigned)app->lora.cr);
    canvas_draw_str_aligned(
        canvas, SCREEN_W / 2, BODY_TOP + 2 * ROW_H, AlignCenter, AlignBottom, line);

    /* The slot is shown one-based, which is how nodes report channel_num. */
    snprintf(
        line,
        sizeof(line),
        "%lu.%03luMHz (%lu)",
        (unsigned long)(app->lora.freq_hz / 1000000u),
        (unsigned long)((app->lora.freq_hz % 1000000u) / 1000u),
        (unsigned long)(app->lora.channel_num + 1));
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 62, AlignCenter, AlignBottom, line);
}

/* Everything the BLE side has actually done. A phone that says "communicating"
 * says nothing about which messages arrived, so this is what turns guessing
 * into diagnosis. */
static void draw_phone(Canvas* canvas, MeshApp* app) {
    char line[36];
    MeshBleStats st;

    if(app->ble == NULL) {
        canvas_draw_str(canvas, 2, BODY_TOP, "BLE not started");
        canvas_draw_str(
            canvas, 2, BODY_TOP + ROW_H, meshtastic_ble_state_name(meshtastic_ble_state()));
        return;
    }

    meshtastic_ble_service_stats(app->ble, &st);

    static const char* const stage_name[] = {"idle", "cfg", "nodes", "done"};
    const char* stage = st.stage < 4 ? stage_name[st.stage] : "?";

    /* W is ToRadio writes in, Pub is publishes attempted. Fr and Fn are the
     * refusals for FromRadio and FromNum, kept apart because only Fr costs the
     * phone its data. */
    snprintf(
        line,
        sizeof(line),
        "W:%lu P:%lu Fr:%lu Fn:%lu",
        (unsigned long)st.writes,
        (unsigned long)st.publishes,
        (unsigned long)st.fail_radio,
        (unsigned long)st.fail_num);
    canvas_draw_str(canvas, 2, BODY_TOP, line);

    snprintf(line, sizeof(line), "Stage:%s N:%lu", stage, (unsigned long)st.last_nonce);
    canvas_draw_str(canvas, 2, BODY_TOP + ROW_H, line);

    snprintf(
        line,
        sizeof(line),
        "Q:%lu Dr:%lu Now:%lu",
        (unsigned long)st.queued,
        (unsigned long)st.drained,
        (unsigned long)st.pending);
    canvas_draw_str(canvas, 2, BODY_TOP + 2 * ROW_H, line);

    /* Handles the stack gave the three characteristics. A zero for R or N
     * means that characteristic was never added, so the phone cannot see it and
     * every update against it fails. Ev is GATT events reaching our handler; a
     * zero there means the dispatcher never calls us at all. */
    /* Wk is the worker loop's proof of life. If the app is wedged and Wk is
     * not climbing between two looks, the worker stopped and the fault is
     * inside it. If Wk climbs while nothing else moves, the worker is fine and
     * the fault is elsewhere. Dr counts FromNum notifications, which should be
     * about one per batch rather than one per message. */
    snprintf(
        line,
        sizeof(line),
        "Wk:%lu Db:%lu Ev:%lu",
        (unsigned long)st.worker_ticks,
        (unsigned long)st.doorbells,
        (unsigned long)st.events);
    canvas_draw_str(canvas, 2, BODY_TOP + 3 * ROW_H, line);
}

/* Runs on the GUI thread. view_port.h marks the draw callback
 * "@warning called from GUI thread", and that thread is a shared system
 * service rather than something this app owns. Waiting here blocks every
 * other app's drawing too, so a lock this callback cannot get is a system
 * freeze, not a dropped frame.
 *
 * The same mistake was fixed once already, on the BLE service mutex in
 * v0.7.5. This is the other lock, and it was left holding FuriWaitForever.
 *
 * The radio thread's critical section is short and pure computation, so the
 * lock is almost always free. When it is not, drawing the frame with slightly
 * stale model data is correct and freezing is not. */
void app_view_draw(Canvas* canvas, void* context) {
    MeshApp* app = context;
    bool locked;

    canvas_clear(canvas);

    locked = furi_mutex_acquire(app->mutex, 0) == FuriStatusOk;

    draw_header(canvas, app);
    canvas_set_font(canvas, FontSecondary);

    switch(app->page) {
    case PageHome:
        draw_home(canvas, app);
        break;
    case PageMessages:
        draw_messages(canvas, app);
        break;
    case PageNodesHeard:
        draw_nodes_heard(canvas, app);
        break;
    case PageNodesSignal:
        draw_nodes_signal(canvas, app);
        break;
    case PageLora:
        draw_lora(canvas, app);
        break;
    case PagePhone:
        draw_phone(canvas, app);
        break;
    case PageCount:
    default:
        break;
    }

    if(locked) furi_mutex_release(app->mutex);
}

/* Called from the input handler to clamp scrolling, so it runs on the GUI
 * thread as well and must not block for the same reason as app_view_draw. A
 * stale count clamps to a row that existed a moment ago, which the next redraw
 * corrects. */
size_t app_view_row_count(MeshApp* app) {
    size_t rows = 0;
    bool locked = furi_mutex_acquire(app->mutex, 0) == FuriStatusOk;

    switch(app->page) {
    case PageMessages:
        rows = message_ring_count(&app->messages);
        break;
    case PageNodesHeard:
    case PageNodesSignal:
        rows = node_roster_count(&app->roster);
        break;
    case PageHome:
    case PageLora:
    case PagePhone:
    case PageCount:
    default:
        rows = 0;
        break;
    }
    if(locked) furi_mutex_release(app->mutex);

    return rows;
}
