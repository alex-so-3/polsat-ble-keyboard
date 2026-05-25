#include "ir_rmt.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

// 1 MHz resolution => 1 RMT tick == 1 us, matching the RP2040 capture units.
#define IR_RMT_RESOLUTION_HZ  1000000

// RMT memory, in symbols. One symbol holds two transitions; a Sejin frame is
// well under 40 transitions, so 64 symbols is comfortably large. On the
// ESP32-C3 a single RX channel can borrow up to 96 symbols of block memory.
#define IR_RMT_MEM_SYMBOLS    64

// Frames are separated by an idle gap longer than this. RMT stops a receive
// once the (idle, HIGH) line stays put for longer than signal_range_max, so
// this both bounds in-frame spaces and delimits frames. Matches FRAME_GAP_US
// in the RP2040 firmware.
#define IR_FRAME_GAP_US       2200

// Anything shorter than this is treated as a glitch and dropped. The RMT glitch
// filter is hardware-limited (must be < ~3187 ns at this resolution), so keep it
// at 1 us; real Sejin pulses are >100 us, so this only removes spurious edges.
#define IR_GLITCH_NS          1000

// A trailing space at least this long is the inter-frame gap, not data; drop it
// so the captured frame ends on the last real mark (as the RP2040 does).
#define IR_TRAILING_GAP_US    1500

static const char *TAG = "ir_rmt";

static rmt_channel_handle_t s_rx_chan;
static QueueHandle_t        s_queue;
static rmt_symbol_word_t    s_symbols[IR_RMT_MEM_SYMBOLS];
static rmt_receive_config_t s_rx_cfg;

static bool on_recv_done(rmt_channel_handle_t chan,
                         const rmt_rx_done_event_data_t *edata,
                         void *user_ctx) {
    BaseType_t high_task_woken = pdFALSE;
    QueueHandle_t q = (QueueHandle_t)user_ctx;
    // edata (which points at the user buffer) is copied by value into the queue.
    xQueueSendFromISR(q, edata, &high_task_woken);
    return high_task_woken == pdTRUE;
}

void ir_rmt_init(int gpio_num) {
    rmt_rx_channel_config_t ch_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = IR_RMT_RESOLUTION_HZ,
        .mem_block_symbols = IR_RMT_MEM_SYMBOLS,
        .gpio_num          = gpio_num,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&ch_cfg, &s_rx_chan));

    s_queue = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
    assert(s_queue);

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_recv_done };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_queue));

    s_rx_cfg.signal_range_min_ns = IR_GLITCH_NS;
    s_rx_cfg.signal_range_max_ns = IR_FRAME_GAP_US * 1000;

    ESP_ERROR_CHECK(rmt_enable(s_rx_chan));
    ESP_ERROR_CHECK(rmt_receive(s_rx_chan, s_symbols, sizeof(s_symbols), &s_rx_cfg));

    ESP_LOGI(TAG, "RMT RX ready on GPIO%d (1us res, gap %dus)", gpio_num, IR_FRAME_GAP_US);
}

// Convert RMT symbols to the signed-microsecond pulse list used by the decoder.
// For an active-low receiver a LOW level (0) is a mark (carrier present) and is
// stored positive; a HIGH level (1) is a space and is stored negative.
static void symbols_to_frame(const rmt_symbol_word_t *syms, size_t n, ir_frame_t *f) {
    int count = 0;
    for (size_t i = 0; i < n && count < IR_MAX_PULSES; i++) {
        uint32_t d0 = syms[i].duration0;
        if (d0 == 0) {
            break;  // zero duration marks the end of valid data
        }
        f->durations[count++] = syms[i].level0 ? -(int32_t)d0 : (int32_t)d0;

        if (count >= IR_MAX_PULSES) {
            break;
        }
        uint32_t d1 = syms[i].duration1;
        if (d1 == 0) {
            break;
        }
        f->durations[count++] = syms[i].level1 ? -(int32_t)d1 : (int32_t)d1;
    }

    // Drop the trailing inter-frame gap so the frame ends on the last mark.
    if (count > 0 && f->durations[count - 1] <= -(int32_t)IR_TRAILING_GAP_US) {
        count--;
    }
    f->count = count;
}

bool ir_rmt_get_frame(ir_frame_t *out, uint32_t timeout_ms) {
    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return false;  // still armed, just nothing received yet
    }

    symbols_to_frame(ev.received_symbols, ev.num_symbols, out);

    // Re-arm for the next frame. received_symbols points into s_symbols, which
    // we have just finished consuming, so it is safe to reuse the buffer.
    ESP_ERROR_CHECK(rmt_receive(s_rx_chan, s_symbols, sizeof(s_symbols), &s_rx_cfg));
    return true;
}
