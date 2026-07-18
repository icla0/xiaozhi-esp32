#include "nora_display.h"

#include "application.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <font_awesome.h>
#include <esp_log.h>
#include <lvgl.h>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdlib>

#define TAG "NoraDisplay"

/* Fonts. English UI, so the ASCII-covering "basic" faces suffice (~134KB total
 * vs 3.3MB for the full CJK faces). The 30px centre face is declared here
 * directly rather than carried on the theme. */
LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);
LV_FONT_DECLARE(font_puhui_basic_30_4);

namespace {

/* Round-screen layout — see simulator/src/nora_ui.cc for the derivations. */
const int kArcRadius = 150;
const int kArcStepDeg = 22;
const int kArcTextTop = 54;
const int kArcTextWidth = 238;
const int kChatWidth = 280;

const int kPttWidth = 160;
const int kPttHeight = 44;
const int kPttBottomGap = 30;
const char* kPttTextIdle = "Hold to Talk";
const char* kPttTextActive = "Release";

const int kRingDiameter = 350;
const int kRingStroke = 6;

const int kWaveBars = 11;
const int kWaveBarWidth = 4;
const int kWaveHeight = 34;
const int kWaveMinBar = 5;
const int kWaveTop = 44;

/* Angle is degrees clockwise from 12 o'clock; the object's centre lands on the
 * arc point, leaving the glyph upright. */
void AlignOnArc(lv_obj_t* obj, int angle_deg, int radius) {
    double rad = angle_deg * M_PI / 180.0;
    lv_obj_align(obj, LV_ALIGN_CENTER,
                 (int32_t)lround(radius * sin(rad)),
                 (int32_t)lround(-radius * cos(rad)));
}

/* lv_anim exec_cb signature is void(void*, int32_t) — wrap the styled setter. */
void RingOpaAnim(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)v, 0);
}

/* The basic font covers ASCII but not typographic punctuation. LLMs emit curly
 * quotes / dashes / ellipsis (all UTF-8 E2 80 xx), which would render as boxes.
 * Fold them down to ASCII equivalents. (Belt-and-braces; ideally the server
 * prompt also constrains the model to plain ASCII punctuation.) */
std::string SanitizeText(const char* s) {
    std::string in(s ? s : ""), out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        unsigned char b0 = (unsigned char)in[i];
        if (i + 2 < in.size()) {
            unsigned char b1 = (unsigned char)in[i + 1];
            unsigned char b2 = (unsigned char)in[i + 2];
            // U+2000–U+203F general punctuation: curly quotes / dashes / ellipsis
            if (b0 == 0xE2 && b1 == 0x80) {
                if (b2 == 0x98 || b2 == 0x99) { out += '\''; i += 3; continue; }  // ' '
                if (b2 == 0x9C || b2 == 0x9D) { out += '"';  i += 3; continue; }  // " "
                if (b2 == 0x93 || b2 == 0x94) { out += '-';  i += 3; continue; }  // – —
                if (b2 == 0xA6)               { out += "..."; i += 3; continue; }  // …
            }
            // Decode any well-formed 3-byte code point and fold full-width /
            // CJK punctuation down to ASCII (the basic font has no glyph for these).
            if ((b0 & 0xF0) == 0xE0 && (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                unsigned int cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                // Full-width ASCII forms U+FF01–U+FF5E map 1:1 to ASCII 0x21–0x7E
                // (covers ，！？：；（）and the full-width tilde ～ U+FF5E).
                if (cp >= 0xFF01 && cp <= 0xFF5E) { out += (char)(cp - 0xFEE0); i += 3; continue; }
                switch (cp) {
                    case 0x3000: out += ' ';  i += 3; continue;  // ideographic space
                    case 0x3001: out += ',';  i += 3; continue;  // 、
                    case 0x3002: out += '.';  i += 3; continue;  // 。
                    case 0x301C: out += '~';  i += 3; continue;  // 〜 wave dash
                }
            }
        }
        out += in[i++];
    }
    return out;
}

}  // namespace

/* ── Themes ──────────────────────────────────────────── */

void NoraDisplay::RegisterNoraThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    auto dark = new LvglTheme("dark");
    dark->set_background_color(lv_color_hex(0x000000));  // OLED black
    dark->set_text_color(lv_color_hex(0xFFFFFF));
    dark->set_chat_background_color(lv_color_hex(0x0A0A0A));
    dark->set_user_bubble_color(lv_color_hex(0x2196F3));
    dark->set_assistant_bubble_color(lv_color_hex(0x1E1E1E));
    dark->set_system_bubble_color(lv_color_hex(0x000000));
    dark->set_system_text_color(lv_color_hex(0x888888));
    dark->set_border_color(lv_color_hex(0x333333));
    dark->set_low_battery_color(lv_color_hex(0xFF0000));
    dark->set_accent_color(lv_color_hex(0x2196F3));
    dark->set_accent_pressed_color(lv_color_hex(0x1976D2));
    dark->set_text_font(text_font);
    dark->set_icon_font(icon_font);
    dark->set_large_icon_font(large_icon_font);

    auto light = new LvglTheme("light");
    light->set_background_color(lv_color_hex(0xFFFFFF));
    light->set_text_color(lv_color_hex(0x000000));
    light->set_chat_background_color(lv_color_hex(0xF5F5F5));
    light->set_user_bubble_color(lv_color_hex(0x2196F3));
    light->set_assistant_bubble_color(lv_color_hex(0xF0F0F0));
    light->set_system_bubble_color(lv_color_hex(0xFFFFFF));
    light->set_system_text_color(lv_color_hex(0x888888));
    light->set_border_color(lv_color_hex(0xCCCCCC));
    light->set_low_battery_color(lv_color_hex(0xFF0000));
    light->set_accent_color(lv_color_hex(0x2196F3));
    light->set_accent_pressed_color(lv_color_hex(0x1976D2));
    light->set_text_font(text_font);
    light->set_icon_font(icon_font);
    light->set_large_icon_font(large_icon_font);

    // Overwrite the stock "light"/"dark" so `self.screen.set_theme` and the
    // NVS theme name resolve to Nora's palette unchanged.
    auto& mgr = LvglThemeManager::GetInstance();
    mgr.RegisterTheme("dark", dark);
    mgr.RegisterTheme("light", light);
}

NoraDisplay::NoraDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y,
                         bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy) {
    RegisterNoraThemes();
    // Re-resolve the theme with a dark default (D4); the base ctor already ran
    // with the stock "light" default before Nora's themes existed.
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "dark");
    auto theme = LvglThemeManager::GetInstance().GetTheme(theme_name);
    if (theme != nullptr) {
        current_theme_ = theme;
    }
}

NoraDisplay::~NoraDisplay() {
    if (wave_timer_) {
        lv_timer_del(wave_timer_);
    }
}

/* ── UI ──────────────────────────────────────────────── */

void NoraDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    Display::SetupUI();  // marks setup_ui_called_; does not build the base tree

    LvglTheme* th = static_cast<LvglTheme*>(current_theme_);
    auto text_font = th->text_font()->font();
    auto icon_font = th->icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, th->text_color(), 0);
    lv_obj_set_style_bg_color(screen, th->background_color(), 0);

    /* Container — full-screen background. */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, th->background_color(), 0);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    /* Arc top bar — status icons on the round-screen arc (D8). */
    top_bar_ = lv_obj_create(screen);
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_VER_RES);
    lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_center(top_bar_);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, FONT_AWESOME_WIFI);
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, th->text_color(), 0);
    AlignOnArc(network_label_, -kArcStepDeg, kArcRadius);

    mute_label_ = lv_label_create(top_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, th->text_color(), 0);
    AlignOnArc(mute_label_, 0, kArcRadius);

    battery_label_ = lv_label_create(top_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, th->text_color(), 0);
    AlignOnArc(battery_label_, kArcStepDeg, kArcRadius);

    /* Shared text line directly below the arc icons (D8). */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, kArcTextWidth, LV_SIZE_CONTENT);
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, kArcTextTop);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, kArcTextWidth);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, th->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, kArcTextWidth);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, th->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 0);

    /* Centre message — 30px, wraps, both-axis centred. */
    chat_message_label_ = lv_label_create(screen);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, kChatWidth);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(chat_message_label_, &font_puhui_basic_30_4, 0);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, th->text_color(), 0);
    lv_obj_center(chat_message_label_);
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

    /* Idle clock — HH:MM, 30px centred, shown only in standby (D5). */
    clock_label_ = lv_label_create(screen);
    lv_obj_set_style_text_font(clock_label_, &font_puhui_basic_30_4, 0);
    lv_obj_set_style_text_color(clock_label_, th->text_color(), 0);
    lv_label_set_text(clock_label_, "");
    lv_obj_center(clock_label_);
    UpdateClock();

    /* Listening ring (D7) — rim circle, opacity breathes forever; visibility
     * toggled by state. */
    listening_ring_ = lv_obj_create(screen);
    lv_obj_remove_flag(listening_ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(listening_ring_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(listening_ring_, kRingDiameter, kRingDiameter);
    lv_obj_center(listening_ring_);
    lv_obj_set_style_radius(listening_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(listening_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listening_ring_, kRingStroke, 0);
    lv_obj_set_style_border_color(listening_ring_, th->accent_color(), 0);
    lv_obj_add_flag(listening_ring_, LV_OBJ_FLAG_HIDDEN);

    static lv_anim_t ring_anim;
    lv_anim_init(&ring_anim);
    lv_anim_set_var(&ring_anim, listening_ring_);
    lv_anim_set_exec_cb(&ring_anim, RingOpaAnim);
    lv_anim_set_values(&ring_anim, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&ring_anim, 900);
    lv_anim_set_playback_duration(&ring_anim, 900);
    lv_anim_set_repeat_count(&ring_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&ring_anim);

    /* Thinking spinner (D7) — centred, replaces text (B). */
    thinking_spinner_ = lv_spinner_create(screen);
    lv_obj_set_size(thinking_spinner_, 64, 64);
    lv_obj_center(thinking_spinner_);
    lv_obj_set_style_arc_color(thinking_spinner_, th->accent_color(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(thinking_spinner_, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(thinking_spinner_, 6, LV_PART_INDICATOR);
    lv_obj_add_flag(thinking_spinner_, LV_OBJ_FLAG_HIDDEN);

    /* Speaking wave (D7) — bars in the top text-line slot; centre keeps text. */
    speaking_wave_ = lv_obj_create(screen);
    lv_obj_remove_flag(speaking_wave_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(speaking_wave_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(speaking_wave_, kWaveBars * kWaveBarWidth * 2, kWaveHeight);
    lv_obj_align(speaking_wave_, LV_ALIGN_TOP_MID, 0, kWaveTop);
    lv_obj_set_style_bg_opa(speaking_wave_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(speaking_wave_, 0, 0);
    lv_obj_set_style_pad_all(speaking_wave_, 0, 0);
    lv_obj_set_flex_flow(speaking_wave_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speaking_wave_, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (int i = 0; i < kWaveBars; i++) {
        lv_obj_t* bar = lv_obj_create(speaking_wave_);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(bar, kWaveBarWidth, kWaveMinBar);
        lv_obj_set_style_radius(bar, kWaveBarWidth / 2, 0);
        lv_obj_set_style_bg_color(bar, th->accent_color(), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
    }
    lv_obj_add_flag(speaking_wave_, LV_OBJ_FLAG_HIDDEN);

    /* Push-to-talk button (D1) — always-present capsule at the bottom. */
    ptt_button_ = lv_button_create(screen);
    lv_obj_set_size(ptt_button_, kPttWidth, kPttHeight);
    lv_obj_align(ptt_button_, LV_ALIGN_BOTTOM_MID, 0, -kPttBottomGap);
    lv_obj_set_style_radius(ptt_button_, kPttHeight / 2, 0);
    lv_obj_set_style_bg_color(ptt_button_, th->accent_color(), 0);
    lv_obj_set_style_bg_color(ptt_button_, th->accent_pressed_color(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ptt_button_, 0, 0);
    lv_obj_add_event_cb(ptt_button_, PttEventHandler, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(ptt_button_, PttEventHandler, LV_EVENT_RELEASED, this);

    ptt_label_ = lv_label_create(ptt_button_);
    lv_label_set_text(ptt_label_, kPttTextIdle);
    lv_obj_set_style_text_font(ptt_label_, text_font, 0);
    lv_obj_set_style_text_color(ptt_label_, lv_color_white(), 0);
    lv_obj_center(ptt_label_);

    /* Low battery popup — raised into the circle (D8). */
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.66, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(low_battery_popup_, th->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 15, 0);

    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

/* ── Status + animations ─────────────────────────────── */

void NoraDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) return;

    lv_label_set_text(status_label_, status);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    if (notification_label_) lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    last_status_update_time_ = std::chrono::system_clock::now();

    bool standby = strcmp(status, Lang::Strings::STANDBY) == 0;
    bool listening = strcmp(status, Lang::Strings::LISTENING) == 0;
    bool speaking = strcmp(status, Lang::Strings::SPEAKING) == 0;

    // Clock owns the centre in standby only.
    if (clock_label_) {
        if (standby) {
            lv_obj_remove_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ShowListeningRing(listening);
    ShowThinking(false);
    ShowSpeaking(speaking);
}

void NoraDisplay::ShowListeningRing(bool on) {
    if (listening_ring_ == nullptr) return;
    if (on) {
        lv_obj_remove_flag(listening_ring_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(listening_ring_, LV_OBJ_FLAG_HIDDEN);
    }
}

void NoraDisplay::ShowThinking(bool on) {
    if (thinking_spinner_ == nullptr) return;
    if (on) {
        lv_obj_remove_flag(thinking_spinner_, LV_OBJ_FLAG_HIDDEN);
        if (chat_message_label_) lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        if (clock_label_) lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
        ShowListeningRing(false);
    } else {
        lv_obj_add_flag(thinking_spinner_, LV_OBJ_FLAG_HIDDEN);
    }
}

void NoraDisplay::ShowSpeaking(bool on) {
    if (speaking_wave_ == nullptr) return;
    if (on) {
        lv_obj_remove_flag(speaking_wave_, LV_OBJ_FLAG_HIDDEN);
        if (status_label_) lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        if (wave_timer_ == nullptr) {
            wave_timer_ = lv_timer_create(
                [](lv_timer_t* t) {
                    static_cast<NoraDisplay*>(lv_timer_get_user_data(t))->UpdateWave();
                },
                60, this);
        } else {
            lv_timer_resume(wave_timer_);
        }
    } else {
        lv_obj_add_flag(speaking_wave_, LV_OBJ_FLAG_HIDDEN);
        if (wave_timer_) lv_timer_pause(wave_timer_);
    }
}

void NoraDisplay::UpdateWave() {
    if (speaking_wave_ == nullptr) return;
    DisplayLockGuard lock(this);
    wave_phase_++;
    uint32_t n = lv_obj_get_child_count(speaking_wave_);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* bar = lv_obj_get_child(speaking_wave_, i);
        double s = 0.5 + 0.5 * sin(wave_phase_ * 0.35 + i * 0.6);
        int h = kWaveMinBar + (int)((kWaveHeight - kWaveMinBar) * s) + (rand() % 6);
        if (h > kWaveHeight) h = kWaveHeight;
        lv_obj_set_height(bar, h);
    }
}

void NoraDisplay::UpdateClock() {
    if (clock_label_ == nullptr) return;
    time_t now = time(nullptr);
    struct tm* lt = localtime(&now);
    if (lt == nullptr || lt->tm_year + 1900 < 2020) {
        lv_label_set_text(clock_label_, "");  // clock never set — hide time
        return;
    }
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", lt->tm_hour, lt->tm_min);
    lv_label_set_text(clock_label_, buf);
    lv_obj_center(clock_label_);
}

/* Full override: same icon/battery/network plumbing as the base, but the clock
 * drives Nora's centre clock_label_ instead of the top status line. */
void NoraDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) return;
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, FONT_AWESOME_VOLUME_XMARK);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    // Idle clock — refresh Nora's centre clock (HH:MM).
    if (app.GetDeviceState() == kDeviceStateIdle) {
        DisplayLockGuard lock(this);
        UpdateClock();
    }

    // Battery icon + low-battery popup.
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = FONT_AWESOME_BATTERY_BOLT;
        } else {
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY, FONT_AWESOME_BATTERY_QUARTER,
                FONT_AWESOME_BATTERY_HALF, FONT_AWESOME_BATTERY_THREE_QUARTERS,
                FONT_AWESOME_BATTERY_FULL, FONT_AWESOME_BATTERY_FULL,
            };
            icon = levels[battery_level / 20];
        }
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }
        if (low_battery_popup_ != nullptr && !update_all) {
            if (strcmp(icon, FONT_AWESOME_BATTERY_EMPTY) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_remove_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    app.Schedule([&app]() { app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY); });
                }
            } else if (!lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Network icon every ~10s.
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        static const std::vector<DeviceState> allowed_states = {
            kDeviceStateIdle, kDeviceStateStarting, kDeviceStateWifiConfiguring,
            kDeviceStateListening, kDeviceStateActivating,
        };
        auto device_state = app.GetDeviceState();
        if (std::find(allowed_states.begin(), allowed_states.end(), device_state) != allowed_states.end()) {
            icon = board.GetNetworkStateIcon();
            if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
                DisplayLockGuard lock(this);
                network_icon_ = icon;
                lv_label_set_text(network_label_, network_icon_);
            }
        }
    }
}

/* ── Messages ────────────────────────────────────────── */

void NoraDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) return;

    if (content == nullptr || content[0] == '\0') {
        lv_label_set_text(chat_message_label_, "");
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Both the user's ASR result and the assistant's answer show centred,
    // replacing each other — the child sees what they said, then the reply.
    // (Supersedes the earlier B behaviour where the user line was hidden
    // behind the thinking spinner.)
    (void)role;
    ShowThinking(false);
    std::string clean = SanitizeText(content);
    lv_label_set_text(chat_message_label_, clean.c_str());
    lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(chat_message_label_);
    if (clock_label_) lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
    if (listening_ring_) lv_obj_add_flag(listening_ring_, LV_OBJ_FLAG_HIDDEN);
}

void NoraDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) return;
    lv_label_set_text(chat_message_label_, "");
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    ShowThinking(false);
}

/* No-op: the screen shows the message only, no emoji (D1/D2 dropped). Kept
 * because the Display interface requires it and Application calls it. */
void NoraDisplay::SetEmotion(const char* emotion) {
    (void)emotion;
}

/* ── Theme ───────────────────────────────────────────── */

void NoraDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    LvglTheme* th = static_cast<LvglTheme*>(theme);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_color(screen, th->text_color(), 0);
    lv_obj_set_style_bg_color(screen, th->background_color(), 0);

    if (container_) lv_obj_set_style_bg_color(container_, th->background_color(), 0);
    if (network_label_) lv_obj_set_style_text_color(network_label_, th->text_color(), 0);
    if (mute_label_) lv_obj_set_style_text_color(mute_label_, th->text_color(), 0);
    if (battery_label_) lv_obj_set_style_text_color(battery_label_, th->text_color(), 0);
    if (status_label_) lv_obj_set_style_text_color(status_label_, th->text_color(), 0);
    if (notification_label_) lv_obj_set_style_text_color(notification_label_, th->text_color(), 0);
    if (chat_message_label_) lv_obj_set_style_text_color(chat_message_label_, th->text_color(), 0);
    if (clock_label_) lv_obj_set_style_text_color(clock_label_, th->text_color(), 0);
    if (low_battery_popup_) lv_obj_set_style_bg_color(low_battery_popup_, th->low_battery_color(), 0);
    if (listening_ring_) lv_obj_set_style_border_color(listening_ring_, th->accent_color(), 0);
    if (thinking_spinner_) lv_obj_set_style_arc_color(thinking_spinner_, th->accent_color(), LV_PART_INDICATOR);
    if (ptt_button_) {
        lv_obj_set_style_bg_color(ptt_button_, th->accent_color(), 0);
        lv_obj_set_style_bg_color(ptt_button_, th->accent_pressed_color(), LV_STATE_PRESSED);
    }
    if (speaking_wave_) {
        uint32_t n = lv_obj_get_child_count(speaking_wave_);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_set_style_bg_color(lv_obj_get_child(speaking_wave_, i), th->accent_color(), 0);
        }
    }

    Display::SetTheme(theme);  // persist name to NVS
}

/* ── PTT button ──────────────────────────────────────── */

void NoraDisplay::PttEventHandler(lv_event_t* e) {
    auto* self = static_cast<NoraDisplay*>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    auto& app = Application::GetInstance();
    if (code == LV_EVENT_PRESSED) {
        // Speaking->abort is handled inside StartListening (HandleStartListeningEvent).
        app.StartListening();
        if (self && self->ptt_label_) lv_label_set_text(self->ptt_label_, kPttTextActive);
    } else if (code == LV_EVENT_RELEASED) {
        app.StopListening();
        if (self && self->ptt_label_) lv_label_set_text(self->ptt_label_, kPttTextIdle);
    }
}
