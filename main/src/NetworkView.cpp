#include "NetworkView.hpp"
#include <cstdio>
#include <cstring>
extern "C" { LV_FONT_DECLARE(font_roboto_20); }
namespace {
struct NetworkChoice { NetworkView *view; WifiNetwork network; };
constexpr uint32_t INK = 0x293246, MUTED = 0x7C879B, ACCENT = 0x6854D9;
void surface(lv_obj_t *o, uint32_t color, int radius = 16) {
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_shadow_width(o, 0, 0); lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0); lv_obj_set_scrollable(o, false);
}
lv_obj_t *text(lv_obj_t *parent, const char *s, int x, int y, int width, uint32_t color = INK) {
    auto *o = lv_label_create(parent); lv_label_set_text(o, s); lv_obj_set_pos(o, x, y); lv_obj_set_width(o, width);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0); return o;
}
lv_obj_t *button(lv_obj_t *parent, const char *s, int x, int y, int width, bool primary = false) {
    auto *o = lv_button_create(parent); surface(o, primary ? ACCENT : 0xEFEDF8, 12);
    lv_obj_set_size(o, width, 48); lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_color(o, lv_color_hex(primary ? 0xFFFFFF : ACCENT), 0);
    auto *l = lv_label_create(o); lv_label_set_text(l, s); lv_obj_center(l); return o;
}
}
NetworkView::~NetworkView() { if (root_) lv_obj_delete(root_); }
void NetworkView::show(lv_obj_t *parent) {
    if (!root_) create(parent);
    lv_obj_set_hidden(root_, false); lv_obj_move_foreground(root_); lv_obj_update_layout(root_);
}
void NetworkView::hide() { if (root_) { closeScan(); lv_obj_set_hidden(root_, true); edit(false); } }
void NetworkView::create(lv_obj_t *parent) {
    root_ = lv_obj_create(parent); surface(root_, 0xF3F4F8, 0); lv_obj_set_floating(root_, true);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0); lv_obj_set_size(root_, 1024, 600); lv_obj_set_pos(root_, -20, -84);
    lv_obj_set_style_pad_top(root_, 64, 0);
    text(root_, "Подключение к сети", 32, 16, 600);
    const char *names[] = {"Провод", "Wi-Fi", "Нет подключения"};
    for (int i = 0; i < 3; ++i) {
        cards_[i] = lv_obj_create(root_); surface(cards_[i], 0xFFFFFF, 16);
        lv_obj_set_pos(cards_[i], 32 + i * 324, 58); lv_obj_set_size(cards_[i], 312, 70);
        labels_[i] = text(cards_[i], names[i], 16, 12, 280);
    }
    hint_ = text(root_, "Wi-Fi 2.4 ГГц / провод имеет приоритет", 32, 144, 760, MUTED);
    edit_ = button(root_, "Изменить", 816, 138, 176);
    lv_obj_add_event_cb(edit_, [](lv_event_t *e) { auto *self = static_cast<NetworkView *>(lv_event_get_user_data(e)); self->edit(!self->editing_); }, LV_EVENT_CLICKED, this);
    ssid_label_ = text(root_, "Имя сети (SSID)", 32, 208, 220);
    password_label_ = text(root_, "Пароль", 32, 264, 180);
    ssid_ = lv_textarea_create(root_); password_ = lv_textarea_create(root_);
    for (auto *input : {ssid_, password_}) {
        surface(input, 0xFFFFFF, 12); lv_obj_set_size(input, 760, 46); lv_obj_set_style_pad_all(input, 10, 0);
        lv_textarea_set_one_line(input, true);
        lv_obj_add_event_cb(input, [](lv_event_t *e) {
            auto *self = static_cast<NetworkView *>(lv_event_get_user_data(e));
            if (self->editing_) lv_keyboard_set_textarea(self->keyboard_, static_cast<lv_obj_t *>(lv_event_get_current_target(e)));
        }, LV_EVENT_FOCUSED, this);
    }
    lv_obj_set_pos(ssid_, 232, 196); lv_obj_set_pos(password_, 232, 252);
    lv_textarea_set_max_length(ssid_, 32); lv_textarea_set_max_length(password_, 64);
    lv_textarea_set_placeholder_text(ssid_, "Название вашей сети");
    lv_textarea_set_password_mode(password_, true); lv_textarea_set_password_bullet(password_, "*");
    lv_textarea_set_placeholder_text(password_, "Пусто: оставить пароль для той же сети");
    lv_obj_set_width(password_, 560);
    reveal_ = button(root_, "Показать", 804, 252, 188);
    lv_obj_add_event_cb(reveal_, [](lv_event_t *e) {
        auto *self = static_cast<NetworkView *>(lv_event_get_user_data(e));
        if (!lv_textarea_get_text(self->password_)[0] && !std::strcmp(lv_textarea_get_text(self->ssid_), self->snapshot_.ssid))
            lv_textarea_set_text(self->password_, self->snapshot_.saved_password);
        const bool hidden = lv_textarea_get_password_mode(self->password_);
        lv_textarea_set_password_mode(self->password_, !hidden);
        lv_label_set_text(lv_obj_get_child(self->reveal_, 0), hidden ? "Скрыть" : "Показать");
    }, LV_EVENT_CLICKED, this);
    password_status_ = text(root_, "", 520, 190, 472, MUTED);
    lv_obj_add_event_cb(ssid_, [](lv_event_t *e) {
        auto *self = static_cast<NetworkView *>(lv_event_get_user_data(e));
        if (self->editing_) { lv_textarea_set_text(self->password_, ""); self->maskPassword(); }
        self->updatePasswordStatus();
    }, LV_EVENT_VALUE_CHANGED, this);
    open_ = lv_checkbox_create(root_); lv_checkbox_set_text(open_, "Открытая сеть"); lv_obj_set_pos(open_, 32, 316);
    save_ = button(root_, "Подключить Wi-Fi", 344, 308, 252, true);
    disconnect_ = button(root_, "Забыть Wi-Fi", 612, 308, 216);
    lv_obj_add_event_cb(save_, [](lv_event_t *e) { static_cast<NetworkView *>(lv_event_get_user_data(e))->send(WifiAction::Save); }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(disconnect_, [](lv_event_t *e) { static_cast<NetworkView *>(lv_event_get_user_data(e))->send(WifiAction::Disconnect); }, LV_EVENT_CLICKED, this);
    scan_button_ = button(root_, "Найти сети", 32, 308, 296);
    lv_obj_add_event_cb(scan_button_, [](lv_event_t *e) { static_cast<NetworkView *>(lv_event_get_user_data(e))->openScan(); }, LV_EVENT_CLICKED, this);
    message_ = text(root_, "", 32, 380, 960, MUTED);
    keyboard_ = lv_keyboard_create(root_); lv_obj_set_size(keyboard_, 1024, 210);
    lv_obj_set_align(keyboard_, LV_ALIGN_TOP_LEFT); lv_obj_set_pos(keyboard_, 0, 326);
    lv_obj_set_style_text_font(keyboard_, LV_FONT_DEFAULT, LV_PART_ITEMS);
    // Keyboard editing has a compact layout; the overview keeps all three indicators visible.
    scan_panel_ = lv_obj_create(root_); surface(scan_panel_, 0xF3F4F8, 0);
    lv_obj_set_size(scan_panel_, 1024, 536); lv_obj_set_pos(scan_panel_, 0, 0);
    text(scan_panel_, "Доступные сети Wi-Fi", 32, 16, 700);
    auto *close = button(scan_panel_, "Закрыть", 816, 8, 176);
    lv_obj_add_event_cb(close, [](lv_event_t *e) { static_cast<NetworkView *>(lv_event_get_user_data(e))->closeScan(); }, LV_EVENT_CLICKED, this);
    scan_message_ = text(scan_panel_, "", 32, 72, 730, MUTED);
    scan_refresh_ = button(scan_panel_, "Обновить", 788, 66, 204);
    lv_obj_add_event_cb(scan_refresh_, [](lv_event_t *e) { static_cast<NetworkView *>(lv_event_get_user_data(e))->scan(); }, LV_EVENT_CLICKED, this);
    scan_list_ = lv_obj_create(scan_panel_); surface(scan_list_, 0xF3F4F8, 0);
    lv_obj_set_pos(scan_list_, 32, 128); lv_obj_set_size(scan_list_, 960, 384);
    lv_obj_set_scrollable(scan_list_, true); lv_obj_set_scroll_dir(scan_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scan_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_hidden(scan_panel_, true);
    edit(false); update(snapshot_);
}
void NetworkView::edit(bool enabled) {
    editing_ = enabled;
    if (!root_) return;
    maskPassword();
    if (!enabled) {
        lv_textarea_set_text(password_, snapshot_.saved_password); lv_keyboard_set_textarea(keyboard_, nullptr);
        lv_textarea_set_text(ssid_, snapshot_.ssid);
    }
    lv_obj_set_hidden(keyboard_, !enabled);
    lv_obj_set_pos(open_, enabled ? 232 : 32, enabled ? 190 : 316);
    lv_obj_set_y(scan_button_, enabled ? 258 : 308);
    lv_obj_set_y(save_, enabled ? 258 : 308); lv_obj_set_y(disconnect_, enabled ? 258 : 308);
    lv_obj_set_y(ssid_, enabled ? 82 : 196); lv_obj_set_y(password_, enabled ? 138 : 252);
    // Text captions and cards move out of the editor rather than overlap its keyboard.
    for (auto *card : cards_) lv_obj_set_hidden(card, enabled);
    lv_obj_set_hidden(hint_, enabled);
    lv_obj_set_y(ssid_label_, enabled ? 92 : 208);
    lv_obj_set_y(password_label_, enabled ? 148 : 264);
    lv_obj_set_y(reveal_, enabled ? 138 : 252);
    lv_obj_set_pos(password_status_, enabled ? 520 : 32, enabled ? 190 : 364);
    updatePasswordStatus();
    lv_obj_set_y(message_, enabled ? 228 : 404);
    lv_label_set_long_mode(message_, enabled ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    lv_obj_set_height(message_, enabled ? 26 : LV_SIZE_CONTENT);
    lv_obj_set_disabled(ssid_, !enabled); lv_obj_set_disabled(password_, !enabled);
    lv_obj_set_hidden(open_, !enabled);
    lv_obj_set_disabled(save_, snapshot_.busy || (!enabled && !snapshot_.wifi_configured));
    if (enabled) lv_keyboard_set_textarea(keyboard_, ssid_);
    auto *caption = lv_obj_get_child(edit_, 0); lv_label_set_text(caption, enabled ? "Готово" : "Изменить");
    lv_obj_set_y(edit_, enabled ? 8 : 138);
}
void NetworkView::send(WifiAction action) {
    WifiCommand command; command.action = action;
    const auto *ssid = lv_textarea_get_text(ssid_), *password = lv_textarea_get_text(password_);
    // LVGL limits characters; ESP-IDF limits bytes (important for Cyrillic SSIDs).
    if (std::char_traits<char>::length(ssid) >= sizeof(command.ssid) || std::char_traits<char>::length(password) >= sizeof(command.password)) {
        lv_label_set_text(message_, "Слишком длинное имя сети или пароль в байтах UTF-8."); return;
    }
    std::snprintf(command.ssid, sizeof(command.ssid), "%s", ssid);
    std::snprintf(command.password, sizeof(command.password), "%s", password);
    command.open_network = lv_obj_has_state(open_, LV_STATE_CHECKED);
    const bool accepted = handler_ && handler_(command);
    lv_textarea_set_text(password_, ""); maskPassword();
    lv_label_set_text(message_, accepted ? "Настройки отправлены..." : "Не удалось отправить настройки. Повторите попытку.");
    pending_ = accepted;
    if (accepted) { lv_obj_set_disabled(save_, true); lv_obj_set_disabled(disconnect_, true); }
}
void NetworkView::update(const NetworkSnapshot &snapshot) {
    snapshot_ = snapshot; if (!root_) return;
    if (scan_waiting_ && snapshot.scan.generation != scan_requested_generation_) scan_waiting_ = false;
    renderScan();
    const char *names[] = {"Провод", "Wi-Fi", "Нет подключения"};
    const NetworkPath paths[] = {NetworkPath::Ethernet, NetworkPath::Wifi, NetworkPath::None};
    for (int i = 0; i < 3; ++i) {
        const bool selected = paths[i] == snapshot.active;
        const auto color = selected ? i == 2 ? 0xFFF0DF : 0xE5F4EC : 0xFFFFFF;
        lv_obj_set_style_bg_color(cards_[i], lv_color_hex(color), 0);
        lv_obj_set_style_text_color(labels_[i], lv_color_hex(selected ? i == 2 ? 0xAE6342 : 0x238469 : MUTED), 0);
        const char *detail = i == 0 ? snapshot.ethernet_ip : i == 1 ? snapshot.wifi_ip : "";
        lv_label_set_text_fmt(labels_[i], "%s\n%s", names[i], detail[0] ? detail : i == 2 ? "Ожидание сети" : "Нет IP");
    }
    if (!editing_) {
        lv_textarea_set_text(ssid_, snapshot.ssid);
        lv_textarea_set_text(password_, snapshot.saved_password);
        if (snapshot.wifi_open) lv_obj_add_state(open_, LV_STATE_CHECKED); else lv_obj_remove_state(open_, LV_STATE_CHECKED);
    }
    updatePasswordStatus();
    if (!editing_ || pending_) lv_label_set_text(message_, snapshot.message);
    if (!snapshot.busy) pending_ = false;
    lv_obj_set_disabled(save_, snapshot.busy || (!editing_ && !snapshot.wifi_configured)); lv_obj_set_disabled(disconnect_, snapshot.busy || !snapshot.wifi_configured);
}

void NetworkView::openScan() {
    maskPassword();
    lv_obj_set_hidden(keyboard_, true); lv_obj_set_hidden(scan_panel_, false); lv_obj_move_foreground(scan_panel_);
    renderScan();
    if (snapshot_.scan.state != WifiScanState::Scanning) scan();
}
void NetworkView::closeScan() {
    if (!scan_panel_) return;
    lv_obj_set_hidden(scan_panel_, true); lv_obj_set_hidden(keyboard_, !editing_);
}
void NetworkView::scan() {
    if (scan_waiting_ || snapshot_.scan.state == WifiScanState::Scanning) return;
    WifiCommand command; command.action = WifiAction::Scan;
    if (!handler_ || !handler_(command)) {
        lv_label_set_text(scan_message_, "Не удалось начать поиск. Нажмите Обновить."); return;
    }
    scan_waiting_ = true; scan_requested_generation_ = snapshot_.scan.generation;
    renderScan();
}
void NetworkView::renderScan() {
    const auto &scan = snapshot_.scan;
    const bool searching = scan_waiting_ || scan.state == WifiScanState::Scanning;
    lv_obj_set_disabled(scan_refresh_, searching);
    if (searching) {
        lv_label_set_text(scan_message_, "Поиск сетей 2.4 ГГц...");
        lv_obj_clean(scan_list_); scan_rendered_generation_ = UINT32_MAX; return;
    }
    lv_label_set_text(scan_message_, scan.message);
    if (scan_rendered_generation_ == scan.generation) return;
    scan_rendered_generation_ = scan.generation; lv_obj_clean(scan_list_);
    for (size_t i = 0; i < scan.count && i < WIFI_SCAN_LIMIT; ++i) {
        const auto &network = scan.networks[i];
        auto *row = button(scan_list_, "", 0, static_cast<int>(i) * 78, 944);
        lv_obj_set_height(row, 70); surface(row, 0xFFFFFF, 14);
        text(row, network.ssid, 16, 10, 900);
        char detail[160]; std::snprintf(detail, sizeof(detail), "%s  /  %d dBm  /  Канал %u", wifiSecurityName(network.security), network.rssi, network.channel);
        text(row, detail, 16, 38, 900, MUTED);
        lv_obj_set_disabled(row, network.security == WifiSecurity::Unsupported);
        auto *choice = new NetworkChoice{this, network};
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            auto *choice = static_cast<NetworkChoice *>(lv_event_get_user_data(e));
            choice->view->selectNetwork(choice->network);
        }, LV_EVENT_CLICKED, choice);
        lv_obj_add_event_cb(row, [](lv_event_t *e) { delete static_cast<NetworkChoice *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, choice);
    }
    lv_obj_scroll_to_y(scan_list_, 0, LV_ANIM_OFF);
}
void NetworkView::selectNetwork(const WifiNetwork &network) {
    if (network.security == WifiSecurity::Unsupported) return;
    closeScan(); edit(true); maskPassword();
    lv_textarea_set_text(ssid_, network.ssid); lv_textarea_set_text(password_, "");
    if (network.security == WifiSecurity::Open) lv_obj_add_state(open_, LV_STATE_CHECKED);
    else lv_obj_remove_state(open_, LV_STATE_CHECKED);
    lv_keyboard_set_textarea(keyboard_, network.security == WifiSecurity::Open ? ssid_ : password_);
    lv_label_set_text(message_, network.security == WifiSecurity::Open ? "Открытая сеть. Нажмите Подключить Wi-Fi." : "Введите пароль и нажмите Подключить Wi-Fi.");
}

void NetworkView::maskPassword() {
    lv_textarea_set_password_mode(password_, true);
    // Do not expose the last typed character while the field is masked.
    lv_textarea_set_password_show_time(password_, 0);
    lv_label_set_text(lv_obj_get_child(reveal_, 0), "Показать");
}
void NetworkView::updatePasswordStatus() {
    const bool same = !std::strcmp(lv_textarea_get_text(ssid_), snapshot_.ssid);
    lv_label_set_text(password_status_, same && snapshot_.wifi_configured ?
        snapshot_.saved_password[0] ? "Пароль сохранён" : snapshot_.wifi_open ? "Сеть без пароля" : "Пароль не сохранён" : "Пароль для этой сети не сохранён");
}
