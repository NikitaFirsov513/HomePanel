#include "AccountView.hpp"
#include <cstring>
extern "C" { LV_FONT_DECLARE(font_roboto_20); }
namespace {
constexpr uint32_t INK = 0x293246, MUTED = 0x7C879B, ACCENT = 0x6854D9;
void surface(lv_obj_t *o, uint32_t color, int radius = 18) {
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0); lv_obj_set_style_radius(o, radius, 0); lv_obj_set_scrollable(o, false);
}
lv_obj_t *text(lv_obj_t *parent, const char *s, int x, int y, int width, uint32_t color = INK) {
    auto *o = lv_label_create(parent); lv_label_set_text(o, s); lv_obj_set_pos(o, x, y); lv_obj_set_width(o, width);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0); return o;
}
}
AccountView::~AccountView() { if (root_) lv_obj_delete(root_); }
lv_obj_t *AccountView::button(lv_obj_t *parent, const char *caption, int x, int action) {
    const bool primary = action == 0 || action == 4;
    auto *o = lv_button_create(parent); surface(o, primary ? ACCENT : 0xEFEDF8, 12);
    lv_obj_set_size(o, action == 0 ? 190 : action == 4 ? 170 : 160, 48); lv_obj_set_pos(o, x, 354);
    lv_obj_set_style_text_color(o, lv_color_hex(primary ? 0xFFFFFF : ACCENT), 0);
    auto *l = lv_label_create(o); lv_label_set_text(l, caption); lv_obj_center(l);
    lv_obj_set_user_data(o, reinterpret_cast<void *>(static_cast<intptr_t>(action)));
    lv_obj_add_event_cb(o, [](lv_event_t *e) {
        auto *self = static_cast<AccountView *>(lv_event_get_user_data(e));
        auto *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
        const int action = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
        if (action == 3) { lv_obj_set_hidden(self->root_, true); return; }
        const AuthAction commands[] = {AuthAction::Begin, AuthAction::Cancel, AuthAction::Logout, AuthAction::Cancel, AuthAction::Restart};
        if (!self->handler_ || !self->handler_(commands[action])) {
            lv_label_set_text(self->message_, "Не удалось отправить запрос. Повторите попытку."); return;
        }
        // Keep repeat clicks out of the worker queue until its next snapshot.
        lv_obj_set_disabled(target, true);
        lv_label_set_text(self->message_, action == 0 || action == 4 ? "Получаем код входа..." : "Подождите...");
    }, LV_EVENT_CLICKED, this);
    return o;
}
void AccountView::create(lv_obj_t *parent) {
    root_ = lv_obj_create(parent); surface(root_, 0xF3F4F8, 0); lv_obj_set_floating(root_, true);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);
    lv_obj_set_size(root_, lv_display_get_horizontal_resolution(lv_obj_get_display(parent)), lv_display_get_vertical_resolution(lv_obj_get_display(parent)));
    lv_obj_set_pos(root_, -lv_obj_get_style_pad_left(parent, LV_PART_MAIN), -lv_obj_get_style_pad_top(parent, LV_PART_MAIN));
    lv_obj_set_style_pad_top(root_, 64, 0);
    text(root_, "Аккаунт Яндекса", 28, 8, 400);
    auto *panel = lv_obj_create(root_); surface(panel, 0xFFFFFF); lv_obj_set_size(panel, 920, 470); lv_obj_center(panel);
    qr_ = lv_qrcode_create(panel); lv_qrcode_set_size(qr_, 272); lv_qrcode_set_quiet_zone(qr_, true);
    lv_obj_set_pos(qr_, 20, 45);
    placeholder_ = text(panel, "Яндекс", 48, 160, 240, ACCENT);
    message_ = text(panel, "", 332, 34, 550);
    steps_ = text(panel, "", 332, 113, 550, MUTED);
    code_ = text(panel, "", 332, 220, 550, ACCENT);
    expiry_ = text(panel, "", 332, 266, 550, MUTED);
    url_label_ = text(panel, "ya.ru/device", 30, 322, 290, MUTED);
    login_ = button(panel, "Войти по QR", 332, 0);
    cancel_ = button(panel, "Отменить вход", 332, 1);
    logout_ = button(panel, "Выйти", 332, 2);
    restart_ = button(panel, "Войти заново", 510, 4);
    update(snapshot_);
}
void AccountView::show(lv_obj_t *parent) {
    if (!root_) create(parent);
    lv_obj_set_hidden(root_, false); lv_obj_move_foreground(root_);
    lv_obj_update_layout(root_);
}
void AccountView::update(const AuthSnapshot &snapshot) {
    snapshot_ = snapshot;
    if (!root_) return;
    const bool waiting = snapshot.state == AuthState::Waiting;
    lv_label_set_text(url_label_, waiting ? snapshot.verification_url : "ya.ru/device");
    lv_label_set_text(steps_, snapshot.authorized ?
        "Сессия сохранена в памяти панели.\n«Войти заново» сбросит сессию и покажет QR.\nПлан дома сохранится." :
        "1. Сканируйте QR камерой телефона\n2. Введите код с этого экрана\n3. Разрешите доступ к умному дому");
    lv_label_set_text(message_, snapshot.message[0] ? snapshot.message : "Войдите в Яндекс для управления устройствами");
    lv_obj_set_style_text_color(message_, lv_color_hex(snapshot.state == AuthState::Error ? 0xBC3E4B : INK), 0);
    lv_obj_set_hidden(qr_, !waiting); lv_obj_set_hidden(placeholder_, waiting);
    if (waiting && qr_url_ != snapshot.verification_url) {
        qr_url_ = snapshot.verification_url;
        if (lv_qrcode_update(qr_, qr_url_.data(), qr_url_.size()) != LV_RESULT_OK) {
            lv_obj_set_hidden(qr_, true); lv_label_set_text(message_, "Не удалось построить QR. Откройте oauth.yandex.ru/device вручную.");
        }
    }
    if (!waiting) qr_url_.clear();
    lv_label_set_text_fmt(code_, waiting ? "Код: %s" : "%s", waiting ? snapshot.user_code : snapshot.authorized ? "Аккаунт подключён" : "Вход не выполнен");
    if (waiting) lv_label_set_text_fmt(expiry_, "Код действует ещё %lu:%02lu", static_cast<unsigned long>(snapshot.seconds_left / 60), static_cast<unsigned long>(snapshot.seconds_left % 60));
    else lv_label_set_text(expiry_, snapshot.authorized ? "Вход сохранён на панели" : "Пароль вводится только на странице Яндекса");
    lv_obj_set_hidden(login_, waiting || snapshot.authorized); lv_obj_set_hidden(cancel_, !waiting); lv_obj_set_hidden(logout_, !snapshot.authorized);
    lv_obj_set_hidden(restart_, !snapshot.authorized);
    lv_obj_set_disabled(login_, false); lv_obj_set_disabled(cancel_, false); lv_obj_set_disabled(logout_, false);
    lv_obj_set_disabled(restart_, false);
    lv_obj_update_layout(root_);
}
