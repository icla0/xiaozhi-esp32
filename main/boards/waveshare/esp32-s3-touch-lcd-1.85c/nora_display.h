#ifndef NORA_DISPLAY_H
#define NORA_DISPLAY_H

#include "display/lcd_display.h"
#include "lvgl_theme.h"

/*
 * NoraDisplay — round-screen push-to-talk UI for the 1.85C board.
 *
 * Ported from the SDL simulator (simulator/src/nora_ui.*). It fully replaces
 * the base LcdDisplay UI tree with: arc top bar, centred clock <-> single
 * centred message, a push-to-talk capsule, and the three status animations
 * (listening ring / thinking spinner / speaking wave). It reuses the base
 * protected members (status_label_, network_label_, battery_label_,
 * mute_label_, notification_label_, low_battery_popup_, chat_message_label_)
 * so the inherited status-bar plumbing keeps working.
 */
class NoraDisplay : public SpiLcdDisplay {
public:
    NoraDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                int width, int height, int offset_x, int offset_y,
                bool mirror_x, bool mirror_y, bool swap_xy);
    ~NoraDisplay() override;

    void SetupUI() override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetEmotion(const char* emotion) override;
    void SetTheme(Theme* theme) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetStatus(const char* status) override;

private:
    // Nora-specific widgets (base class owns the rest).
    lv_obj_t* clock_label_       = nullptr;
    lv_obj_t* ptt_button_        = nullptr;
    lv_obj_t* ptt_label_         = nullptr;
    lv_obj_t* listening_ring_    = nullptr;
    lv_obj_t* thinking_spinner_  = nullptr;
    lv_obj_t* speaking_wave_     = nullptr;
    lv_timer_t* wave_timer_      = nullptr;
    uint32_t wave_phase_         = 0;

    void ShowListeningRing(bool on);
    void ShowThinking(bool on);
    void ShowSpeaking(bool on);
    void UpdateWave();
    void UpdateClock();

    // PTT button press/release -> Application::StartListening/StopListening.
    static void PttEventHandler(lv_event_t* e);

    // Registers Nora's light/dark themes under the "light"/"dark" names,
    // replacing the stock ones so `self.screen.set_theme` works unchanged.
    static void RegisterNoraThemes();
};

#endif // NORA_DISPLAY_H
