#include "HttpsClient.hpp"
#include <string.h>
#include <cstring>
#include "esp_log.h"
#include "esp_heap_caps.h"

HttpsClient::HttpsClient(const char *baseUrl)
{
    esp_http_client_config_t config{};

    if (baseUrl == nullptr || baseUrl[0] == '\0')
    {
        isFail = true;
        return;
    }
    this->baseUrl = baseUrl;

    response_buffer_ =
        static_cast<char *>(
            heap_caps_malloc(
                HTTPS_RESPONSE_BUFFER_SIZE,
                MALLOC_CAP_SPIRAM |
                    MALLOC_CAP_8BIT));

    if (response_buffer_ == nullptr)
    {
        ESP_LOGE(
            "HTTPS",
            "Failed to allocate response buffer");

        isFail = true;
        return;
    }

    response_buffer_[0] = '\0';

    config.url = this->baseUrl.c_str();

    config.crt_bundle_attach =
        esp_crt_bundle_attach;

    config.event_handler =
        event_handler;

    config.user_data =
        this;

    config.timeout_ms =
        10000;

    client_ =
        esp_http_client_init(&config);

    if (client_ == nullptr)
    {
        isFail = true;
    }
}

HttpsClient::~HttpsClient()
{
    if (client_ != nullptr)
    {
        esp_http_client_cleanup(client_);
        client_ = nullptr;
    }

    if (response_buffer_ != nullptr)
    {
        heap_caps_free(response_buffer_);
        response_buffer_ = nullptr;
    }
}

HttpsRes_t HttpsClient::send(const HttpsReq_t &req)
{

    HttpsRes_t res;

    if (client_ == nullptr || isFail)
    {
        res.transport_error = ESP_ERR_INVALID_STATE;
        return res;
    }

    if (req.path == nullptr || req.timeout_ms <= 0)
    {
        res.transport_error = ESP_ERR_INVALID_ARG;
        return res;
    }
    res.transport_error = set_token(req.token);
    if (res.transport_error != ESP_OK)
        return res;
    res.transport_error = esp_http_client_set_timeout_ms(client_, req.timeout_ms);
    if (res.transport_error != ESP_OK)
        return res;

    switch (req.method)
    {
    case HTTP_METHOD_GET:
        res = this->get(req);
        break;
    case HTTP_METHOD_POST:
        res = this->post(req);
        break;
    default:
        res.transport_error = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    return res;
}

esp_err_t HttpsClient::set_token(const char *token)
{
    // A reused client must never inherit credentials from an earlier request.
    esp_err_t clear_error = esp_http_client_delete_header(client_, "Authorization");
    if (clear_error != ESP_OK && clear_error != ESP_ERR_NOT_FOUND)
        return clear_error;
    if (token == nullptr || token[0] == '\0')
    {
        return esp_http_client_set_header(client_, "Accept", "application/json");
    }

    int len = snprintf(
        auth_buffer_,
        sizeof(auth_buffer_),
        "Bearer %s",
        token);

    if (len < 0 ||
        len >= sizeof(auth_buffer_))
    {
        ESP_LOGE(
            "HTTPS",
            "Token is too large");

        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_http_client_set_header(
        client_,
        "Authorization",
        auth_buffer_);
    if (err != ESP_OK)
        return err;

    return esp_http_client_set_header(
        client_,
        "Accept",
        "application/json");
}

HttpsRes_t HttpsClient::perform()
{
    HttpsRes_t response;

    response_length_ = 0;
    response_buffer_[0] = '\0';
    response_too_large_ = false;

    response.transport_error =
        esp_http_client_perform(client_);

    if (response_too_large_)
    {
        response.transport_error =
            ESP_ERR_INVALID_SIZE;

        return response;
    }

    if (response.transport_error != ESP_OK)
    {
        ESP_LOGE(
            "HTTPS",
            "Transport error: %s",
            esp_err_to_name(
                response.transport_error));

        return response;
    }

    response.http_status =
        esp_http_client_get_status_code(
            client_);

    response.body =
        response_buffer_;

    response.body_len =
        response_length_;

    ESP_LOGI(
        "HTTPS",
        "HTTP %d, body=%u bytes",
        response.http_status,
        (unsigned)response.body_len);

    return response;
}

esp_err_t HttpsClient::event_handler(
    esp_http_client_event_t *event)
{
    if (event == nullptr || event->user_data == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    HttpsClient *self =
        static_cast<HttpsClient *>(event->user_data);

    switch (event->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGE("HTTPS", "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD("HTTPS", "Connected");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD("HTTPS", "Headers sent");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(
            "HTTPS",
            "Header: %s: %s",
            event->header_key,
            event->header_value);
        break;

    case HTTP_EVENT_ON_DATA:
    {
        if (self->response_too_large_)
            return ESP_FAIL;
        if (event->data == nullptr || event->data_len <= 0)
        {
            break;
        }

        /*
         * -1 нужен потому, что последний байт
         * оставляем для '\0'
         */
        size_t free_space =
            HTTPS_RESPONSE_BUFFER_SIZE -
            self->response_length_ -
            1;

        if ((size_t)event->data_len > free_space)
        {
            self->response_too_large_ = true;

            ESP_LOGE(
                "HTTPS",
                "Response too large. Buffer=%u bytes",
                HTTPS_RESPONSE_BUFFER_SIZE);

            return ESP_FAIL;
        }

        /*
         * event->data НЕ обязательно заканчивается '\0',
         * поэтому используем memcpy, а не strcpy/strcat.
         */
        memcpy(
            self->response_buffer_ +
                self->response_length_,
            event->data,
            event->data_len);

        self->response_length_ +=
            event->data_len;

        /*
         * Делаем накопленный ответ нормальной C-строкой.
         */
        self->response_buffer_[self->response_length_] = '\0';

        break;
    }

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(
            "HTTPS",
            "Response finished, %u bytes",
            (unsigned)self->response_length_);
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD("HTTPS", "Disconnected");
        break;

    case HTTP_EVENT_REDIRECT:
        ESP_LOGD("HTTPS", "Redirect");

        esp_http_client_set_redirection(
            event->client);
        break;

    default:
        break;
    }

    return ESP_OK;
}

HttpsRes_t HttpsClient::get(const HttpsReq_t &req)
{
    HttpsRes_t res;

    char url[256];

    int len = snprintf(
        url,
        sizeof(url),
        "%s%s",
        baseUrl.c_str(),
        req.path);

    if (len < 0 || len >= sizeof(url))
    {
        res.transport_error =
            ESP_ERR_INVALID_SIZE;

        return res;
    }

    esp_err_t err =
        esp_http_client_set_url(
            client_,
            url);

    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }

    err =
        esp_http_client_set_method(
            client_,
            HTTP_METHOD_GET);

    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }

    /*
     * Сбрасываем body,
     * если предыдущий запрос был POST.
     */
    err = esp_http_client_set_post_field(
        client_,
        nullptr,
        0);
    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }

    return perform();
}


HttpsRes_t HttpsClient::post(const HttpsReq_t &req)
{
    HttpsRes_t res;

    if (req.path == nullptr)
    {
        res.transport_error = ESP_ERR_INVALID_ARG;
        return res;
    }

    char url[256];

    int len = snprintf(
        url,
        sizeof(url),
        "%s%s",
        baseUrl.c_str(),
        req.path
    );

    if (len < 0 ||
        static_cast<size_t>(len) >= sizeof(url))
    {
        res.transport_error = ESP_ERR_INVALID_SIZE;
        return res;
    }


    /*
     * Полный URL запроса.
     */
    esp_err_t err =
        esp_http_client_set_url(
            client_,
            url
        );

    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }


    /*
     * HTTP POST.
     */
    err =
        esp_http_client_set_method(
            client_,
            HTTP_METHOD_POST
        );

    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }


    /*
     * Мы отправляем JSON.
     */
    err =
        esp_http_client_set_header(
            client_,
            "Content-Type",
            "application/json"
        );

    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }


    /*
     * Тело POST-запроса.
     */
    if (req.body != nullptr)
    {
        err =
            esp_http_client_set_post_field(
                client_,
                req.body,
                strlen(req.body)
            );
    }
    else
    {
        err =
            esp_http_client_set_post_field(
                client_,
                nullptr,
                0
            );
    }

    if (err != ESP_OK)
    {
        res.transport_error = err;
        return res;
    }


    /*
     * TCP -> TLS -> HTTP
     */
    return perform();
}
