#include "ConnectionsView.hpp"
#include <cstdio>
extern "C" { LV_FONT_DECLARE(font_roboto_20); }
namespace {
constexpr uint32_t INK = 0x293246, MUTED = 0x7C879B, ACCENT = 0x6854D9;
void surface(lv_obj_t *o, uint32_t color, int radius = 20) {
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0); lv_obj_set_style_shadow_width(o, 0, 0); lv_obj_set_scrollable(o, false);
}
lv_obj_t *text(lv_obj_t *parent, const char *s, int x, int y, int width, uint32_t color = INK) {
    auto *o = lv_label_create(parent); lv_label_set_text(o, s); lv_obj_set_pos(o, x, y); lv_obj_set_width(o, width);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0); return o;
}
lv_obj_t *button(lv_obj_t *parent, const char *s, int x, int y, int width, bool primary = false) {
    auto *o = lv_button_create(parent); surface(o, primary ? ACCENT : 0xEFEDF8, 12);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, width, 50);
    lv_obj_set_style_text_color(o, lv_color_hex(primary ? 0xFFFFFF : ACCENT), 0);
    auto *l = lv_label_create(o); lv_label_set_text(l, s); lv_obj_center(l); return o;
}
}
ConnectionsView::~ConnectionsView() { if (root_) lv_obj_delete(root_); }
void ConnectionsView::hide() { if (root_) lv_obj_set_hidden(root_, true); closeSettings(); }
void ConnectionsView::closeSettings() {
    editing_ = false;
    if (form_) { lv_textarea_set_text(token_, ""); lv_keyboard_set_textarea(keyboard_, nullptr); lv_obj_set_hidden(form_, true); }
}
void ConnectionsView::show(lv_obj_t *parent) {
    if (!root_) create(parent);
    lv_obj_set_hidden(root_, false); lv_obj_move_foreground(root_); lv_obj_update_layout(root_);
}
void ConnectionsView::create(lv_obj_t *parent) {
    root_ = lv_obj_create(parent); surface(root_, 0xF3F4F8, 0); lv_obj_set_floating(root_, true);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);
    lv_obj_set_size(root_, 1024, 600); lv_obj_set_pos(root_, -20, -84); lv_obj_set_style_pad_top(root_, 64, 0);
    text(root_, "Подключения", 32, 22, 600);
    text(root_, "Устройства из разных систем в одном доме", 32, 58, 900, MUTED);
    for (int i = 0; i < 2; ++i) {
        auto *card = lv_obj_create(root_); surface(card, 0xFFFFFF, 24); lv_obj_set_size(card, 472, 310);
        lv_obj_set_pos(card, 32 + i * 488, 112);
        auto *mark = lv_obj_create(card); surface(mark, i ? 0xE2F4FD : 0xFFF0EC, 14);
        lv_obj_set_size(mark, 56, 56); lv_obj_set_pos(mark, 24, 24);
        auto *letter = text(mark, i ? "HA" : "Я", 0, 0, 56, i ? 0x1687B6 : 0xE64D3A);
        lv_obj_set_style_text_align(letter, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(letter);
        text(card, i ? "Home Assistant" : "Яндекс", 100, 37, 340);
        auto *status = text(card, "", 24, 106, 424, MUTED);
        if (i) ha_status_ = status; else yandex_status_ = status;
        auto *description = text(card, i ? "" : "Устройства Дома с Алисой.\nВход по QR с телефона.", 24, 150, 424, MUTED);
        if (i) address_label_ = description;
        auto *open = button(card, i ? "Настроить" : "Открыть аккаунт", 24, 236, 244, true);
        if (!i) lv_obj_add_event_cb(open, [](lv_event_t *e) {
            auto *self = static_cast<ConnectionsView *>(lv_event_get_user_data(e));
            if (self->yandex_handler_) self->yandex_handler_(self->context_);
        }, LV_EVENT_CLICKED, this);
        if (i) {
            lv_obj_add_event_cb(open, [](lv_event_t *e) { static_cast<ConnectionsView *>(lv_event_get_user_data(e))->openSettings(); }, LV_EVENT_CLICKED, this);
        }
    }
    text(root_, "Подключения независимы. План дома сохраняется при отключении аккаунта.", 32, 456, 960, MUTED);
    form_ = lv_obj_create(root_); surface(form_, 0xF3F4F8, 0); lv_obj_set_size(form_, 1024, 536); lv_obj_set_pos(form_, 0, 0);
    text(form_, "Настройка Home Assistant", 32, 12, 600);
    auto *close = button(form_, "Закрыть", 820, 8, 172);
    lv_obj_add_event_cb(close, [](lv_event_t *e) { static_cast<ConnectionsView *>(lv_event_get_user_data(e))->closeSettings(); }, LV_EVENT_CLICKED, this);
    text(form_, "Адрес сервера", 32, 80, 190);
    text(form_, "Токен доступа", 32, 142, 190);
    address_ = lv_textarea_create(form_); token_ = lv_textarea_create(form_);
    for (auto *input : {address_, token_}) {
        surface(input, 0xFFFFFF, 12); lv_obj_set_size(input, 760, 48); lv_textarea_set_one_line(input, true);
        lv_obj_set_style_pad_all(input, 10, 0);
        lv_obj_add_event_cb(input, [](lv_event_t *e) {
            auto *self = static_cast<ConnectionsView *>(lv_event_get_user_data(e));
            lv_keyboard_set_textarea(self->keyboard_, static_cast<lv_obj_t *>(lv_event_get_current_target(e)));
        }, LV_EVENT_FOCUSED, this);
    }
    lv_obj_set_pos(address_, 232, 68); lv_obj_set_pos(token_, 232, 130);
    lv_textarea_set_max_length(address_, sizeof(ConnectionCommand::address) - 1);
    lv_textarea_set_placeholder_text(address_, "http://192.168.1.10:8123");
    lv_textarea_set_max_length(token_, sizeof(ConnectionCommand::token) - 1);
    lv_textarea_set_password_mode(token_, true);
    lv_textarea_set_password_bullet(token_, "*");
    lv_textarea_set_placeholder_text(token_, "Долгосрочный токен из профиля Home Assistant");
    save_ = button(form_, "Подключить", 232, 190, 220, true);
    disconnect_ = button(form_, "Отключить", 468, 190, 180);
    lv_obj_add_event_cb(save_, [](lv_event_t *e) { static_cast<ConnectionsView *>(lv_event_get_user_data(e))->send(ConnectionAction::Save); }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(disconnect_, [](lv_event_t *e) { static_cast<ConnectionsView *>(lv_event_get_user_data(e))->send(ConnectionAction::Disconnect); }, LV_EVENT_CLICKED, this);
    form_message_ = text(form_, "", 32, 250, 960, MUTED);
    keyboard_ = lv_keyboard_create(form_); lv_obj_set_size(keyboard_, 1024, 228);
    lv_obj_set_align(keyboard_, LV_ALIGN_TOP_LEFT); lv_obj_set_pos(keyboard_, 0, 308);
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
    // The default keyboard uses icon glyphs absent from the Cyrillic font.
    lv_obj_set_style_text_font(keyboard_, LV_FONT_DEFAULT, LV_PART_ITEMS);
    lv_obj_set_hidden(form_, true);
    updateYandex(yandex_); update(snapshot_);
}
void ConnectionsView::openSettings() {
    editing_ = true; lv_textarea_set_text(address_, snapshot_.address);
    lv_textarea_set_text(token_, "");
    lv_label_set_text(form_message_, snapshot_.configured ? "Пустое поле токена сохранит текущий токен для того же адреса." : "Создайте долгосрочный токен в профиле Home Assistant, раздел Безопасность.");
    lv_obj_set_disabled(disconnect_, !snapshot_.configured); lv_obj_set_disabled(save_, false);
    lv_obj_set_hidden(form_, false); lv_obj_move_foreground(form_); lv_keyboard_set_textarea(keyboard_, address_);
}
void ConnectionsView::send(ConnectionAction action) {
    ConnectionCommand command; command.action = action;
    std::snprintf(command.address, sizeof(command.address), "%s", lv_textarea_get_text(address_));
    std::snprintf(command.token, sizeof(command.token), "%s", lv_textarea_get_text(token_));
    const bool accepted = handler_ && handler_(command);
    // Credentials are never returned in snapshots or kept in a hidden editor.
    lv_textarea_set_text(token_, "");
    lv_label_set_text(form_message_, accepted ? "Проверяем подключение..." : "Не удалось отправить запрос. Повторите попытку.");
    if (accepted) { lv_obj_set_disabled(save_, true); lv_obj_set_disabled(disconnect_, true); }
}
void ConnectionsView::updateYandex(const AuthSnapshot &snapshot) {
    yandex_ = snapshot; if (!root_) return;
    lv_label_set_text(yandex_status_, snapshot.authorized ? "Вход выполнен" : snapshot.state == AuthState::Waiting ? "Ожидается подтверждение входа" : snapshot.state == AuthState::Error ? "Ошибка входа" : "Вход не выполнен");
    lv_obj_set_style_text_color(yandex_status_, lv_color_hex(snapshot.authorized ? 0x238469 : MUTED), 0);
}
void ConnectionsView::update(const ConnectionSnapshot &snapshot) {
    snapshot_ = snapshot; if (!root_) return;
    char state[100];
    if (snapshot.connected) std::snprintf(state, sizeof(state), "Подключён / устройств: %u", snapshot.devices);
    else std::snprintf(state, sizeof(state), "%s", snapshot.configured ? "Нет соединения" : "Не настроен");
    lv_label_set_text(ha_status_, state);
    lv_obj_set_style_text_color(ha_status_, lv_color_hex(snapshot.connected ? 0x238469 : MUTED), 0);
    lv_label_set_text(address_label_, snapshot.message[0] ? snapshot.message : "Локальный сервер умного дома.\nСвет, датчики, техника и сценарии.");
    if (editing_) {
        lv_label_set_text(form_message_, snapshot.message);
        lv_obj_set_disabled(save_, false); lv_obj_set_disabled(disconnect_, !snapshot.configured);
    }
}
