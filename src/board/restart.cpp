// Restart logic shared by the board layers (see board.h: cleanResetIfNeeded, restart).
#include "board.h"

#include <esp_ota_ops.h>
#include <esp_rom_sys.h>
#include <esp_system.h>

namespace board {
namespace {
constexpr uint32_t CLEAN_RESET_MAGIC = 0x7F7C1EA2;
RTC_NOINIT_ATTR uint32_t s_cleanResetMagic;   // survives a system reset, not a power cycle
RTC_NOINIT_ATTR uint32_t s_resetBefore;
bool s_cleanResetDeferred = false;

const char *reasonName(uint32_t r) {
    switch ((esp_reset_reason_t)r) {
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash (panic)";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT: return "watchdog";
        default: return "other";
    }
}
}  // namespace

void restart() {
    if (s_cleanResetMagic != CLEAN_RESET_MAGIC) s_resetBefore = ESP_RST_SW;
    s_cleanResetMagic = CLEAN_RESET_MAGIC;
    Serial.flush();
    esp_rom_software_reset_system();
    for (;;) {
    }
}

bool cleanResetDeferred() { return s_cleanResetDeferred; }

void cleanResetIfNeeded() {
    if (s_cleanResetMagic == CLEAN_RESET_MAGIC) {   // second pass: the system reset is done
        s_cleanResetMagic = 0;
        Serial.printf("[board] full system reset done (the previous reset was a %s)\n", reasonName(s_resetBefore));
        return;
    }
    esp_reset_reason_t r = esp_reset_reason();
    bool cpuOnly = r == ESP_RST_SW || r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
                   r == ESP_RST_WDT;
    if (!cpuOnly) return;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        // Booted by an older firmware's CPU-only restart right after an update: resetting now
        // would count as a failed boot. Reset once the app has confirmed the image.
        s_cleanResetDeferred = true;
        Serial.printf("[board] %s before an unconfirmed update: full reset deferred until it is confirmed\n",
                      reasonName(r));
        return;
    }
    s_resetBefore = r;
    s_cleanResetMagic = CLEAN_RESET_MAGIC;
    Serial.printf("[board] %s left the display peripherals running: full system reset\n", reasonName(r));
    restart();
}

}  // namespace board
