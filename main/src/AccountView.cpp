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
    lv_obj_set_size(o, action == 0 ? 210 : action == 4 ? 190 : 180, 52); lv_obj_set_pos(o, x, 332);
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
    text(root_, "Аккаунт Яндекса", 32, 22, 400);
    text(root_, "Подключение к умному дому", 32, 54, 600, MUTED);
    auto *panel = lv_obj_create(root_); surface(panel, 0xFFFFFF, 24);
    lv_obj_set_size(panel, 960, 414); lv_obj_set_pos(panel, 32, 100);
    auto *identity = lv_obj_create(panel); surface(identity, 0xF5F4FA, 20);
    lv_obj_set_size(identity, 312, 366); lv_obj_set_pos(identity, 24, 24);
    auto *badge = lv_obj_create(identity); surface(badge, 0xF04438, 22);
    lv_obj_set_size(badge, 72, 72); lv_obj_set_pos(badge, 120, 76);
    auto *brand = text(badge, "Я", 0, 0, 72, 0xFFFFFF);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(brand);
    placeholder_ = text(identity, "Яндекс", 20, 172, 272);
    lv_obj_set_style_text_align(placeholder_, LV_TEXT_ALIGN_CENTER, 0);
    state_label_ = text(identity, "", 20, 212, 272, MUTED);
    lv_obj_set_style_text_align(state_label_, LV_TEXT_ALIGN_CENTER, 0);
    qr_ = lv_qrcode_create(identity); lv_qrcode_set_size(qr_, 272); lv_qrcode_set_quiet_zone(qr_, true);
    lv_obj_set_pos(qr_, 20, 20);
    url_label_ = text(identity, "ya.ru/device", 20, 316, 272, MUTED);
    lv_obj_set_style_text_align(url_label_, LV_TEXT_ALIGN_CENTER, 0);
    heading_ = text(panel, "", 376, 36, 550);
    steps_ = text(panel, "", 376, 84, 550, MUTED);
    lv_obj_set_style_text_line_space(steps_, 8, 0);
    code_ = text(panel, "", 376, 188, 550, ACCENT);
    lv_obj_set_style_text_letter_space(code_, 2, 0);
    expiry_ = text(panel, "", 376, 228, 550, MUTED);
    message_ = text(panel, "", 376, 270, 550);
    login_ = button(panel, "Войти по QR", 376, 0);
    cancel_ = button(panel, "Отменить вход", 376, 1);
    restart_ = button(panel, "Войти заново", 376, 4);
    logout_ = button(panel, "Выйти", 582, 2);
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
    const bool error = snapshot.state == AuthState::Error;
    lv_label_set_text(heading_, waiting ? "Подтвердите вход на телефоне" : snapshot.authorized ? "Умный дом подключён" : error ? "Не удалось войти" : "Подключите свой умный дом");
    lv_label_set_text(state_label_, snapshot.authorized ? "Вход выполнен" : error ? "Ошибка подключения" : "Вход не выполнен");
    lv_obj_set_style_text_color(state_label_, lv_color_hex(snapshot.authorized ? 0x238469 : error ? 0xBC3E4B : MUTED), 0);
    lv_label_set_text(url_label_, waiting ? snapshot.verification_url : "");
    lv_label_set_text(steps_, waiting ?
        "Сканируйте QR камерой телефона.\nВведите код и разрешите доступ." : snapshot.authorized ?
        "Устройства доступны на главном экране\nи в списке. Вход сохранится после\nперезагрузки панели." :
        "Войдите в свой аккаунт, чтобы видеть\nустройства и управлять ими с панели.");
    const bool normal_message = !snapshot.message[0] ||
        !std::strcmp(snapshot.message, "Аккаунт подключён") ||
        !std::strcmp(snapshot.message, "Сканируйте QR, введите код и разрешите доступ") ||
        !std::strcmp(snapshot.message, "Войдите в Яндекс для управления устройствами");
    lv_label_set_text(message_, !normal_message ? snapshot.message : snapshot.authorized ? "При смене аккаунта план дома сохранится." : waiting ? "" : "Пароль вводится на странице Яндекса.");
    lv_obj_set_y(message_, error ? 180 : 270);
    lv_obj_set_style_text_color(message_, lv_color_hex(snapshot.state == AuthState::Error ? 0xBC3E4B : INK), 0);
    if (waiting && qr_url_ != snapshot.verification_url) {
        qr_url_ = snapshot.verification_url;
        qr_valid_ = lv_qrcode_update(qr_, qr_url_.data(), qr_url_.size()) == LV_RESULT_OK;
    }
    if (!waiting) { qr_url_.clear(); qr_valid_ = false; }
    lv_obj_set_hidden(qr_, !waiting || !qr_valid_);
    if (waiting && !qr_valid_) lv_label_set_text(message_, "QR недоступен. Откройте ссылку слева.");
    lv_label_set_text_fmt(code_, waiting ? "Код: %s" : "%s", waiting ? snapshot.user_code : "");
    if (waiting) lv_label_set_text_fmt(expiry_, "Код действует ещё %lu:%02lu", static_cast<unsigned long>(snapshot.seconds_left / 60), static_cast<unsigned long>(snapshot.seconds_left % 60));
    else lv_label_set_text(expiry_, "");
    lv_obj_set_hidden(login_, waiting || snapshot.authorized); lv_obj_set_hidden(cancel_, !waiting); lv_obj_set_hidden(logout_, !snapshot.authorized);
    lv_obj_set_hidden(restart_, !snapshot.authorized);
    lv_obj_set_disabled(login_, false); lv_obj_set_disabled(cancel_, false); lv_obj_set_disabled(logout_, false);
    lv_obj_set_disabled(restart_, false);
    lv_obj_update_layout(root_);
}
