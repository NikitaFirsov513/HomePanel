#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "LoadingPage.hpp"
#include "HomePage.hpp"
#include <type_traits>
#include "DeviceManager.hpp"
#define UI_TASK_STACK 8192
#define UI_TASK_PRIORITY 5

#define UI_QUEUE_LENGTH 10

extern "C" { LV_FONT_DECLARE(font_roboto_20); }

enum class UiMessageType : uint8_t
{
    SET_LOAD_PAGE,
    SET_ERROR_PAGE,
    SET_HOME_PAGE,
    CONTROL_RESULT,
    AUTH_STATE,
    CONNECTION_STATE,
};

struct UiMessage
{
    UiMessageType type;

    char text[512]{};
    DeviceManager *device_manager = nullptr;
    bool error = false;
    bool finished = true;
    AuthSnapshot auth;
    ConnectionSnapshot connection;
};
static_assert(std::is_trivially_copyable<UiMessage>::value,
              "FreeRTOS queues require trivially copyable messages");

class UiManager
{
public:
    UiManager() = default;
    ~UiManager() = default;
    UiManager(const UiManager &) = delete;
    UiManager &operator=(const UiManager &) = delete;
    esp_err_t init();

    esp_err_t updateMainPage(DeviceManager *device_manager);

    esp_err_t setLoadPage(const char *text = "Loading...");
    esp_err_t setErrorPage(const char *text);
    esp_err_t setHomePage(DeviceManager *device_manager);
    void setActionHandler(HomePage::ActionHandler handler) { home_page_.setActionHandler(handler); }
    void setAuthHandler(AccountView::Handler handler) { home_page_.setAuthHandler(handler); }
    esp_err_t setAuthState(const AuthSnapshot &snapshot, DeviceManager *devices = nullptr);
    void setConnectionHandler(ConnectionsView::Handler handler) { home_page_.setConnectionHandler(handler); }
    esp_err_t setConnectionState(const ConnectionSnapshot &snapshot, DeviceManager *devices);
    esp_err_t setControlResult(DeviceManager *devices, const char *text, bool error, bool finished = true);

private:
    QueueHandle_t ui_queue_ = nullptr;
    LoadingPage loading_page_;
    HomePage home_page_;
    static void ui_task_entry(void *arg);

    void ui_task(void *arg);
    bool ui_send(const UiMessage &msg);
    void ui_process_message(const UiMessage &msg);
};
