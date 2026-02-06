#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_vfs_eventfd.h"

#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"

#include "openthread/dataset.h"
#include "openthread/thread.h"
#include "openthread/ip6.h"

#include "thread_comm.h"
#include "esp_ot_config.h"

#define TAG "NODE_2"

/* =========================================================
 * RX CALLBACK
 * ========================================================= */
static void on_thread_data(const char *from,
                           const char *payload,
                           const otIp6Address *src)
{
    char ip[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(src, ip, sizeof(ip));

    ESP_LOGI(TAG,
             "RX from %s (%s): %s",
             from, ip, payload);
}

/* =========================================================
 * NETIF INIT
 * ========================================================= */
static esp_netif_t *init_openthread_netif(
        const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif);

    ESP_ERROR_CHECK(
        esp_netif_attach(
            netif,
            esp_openthread_netif_glue_init(config)));

    return netif;
}

/* =========================================================
 * APPLICATION TASK
 * Chỉ gửi announce khi đã JOIN Thread
 * ========================================================= */
static void app_task(void *arg)
{
    otInstance *instance = esp_openthread_get_instance();
    bool announced = false;

    while (1) {
        esp_openthread_lock_acquire(portMAX_DELAY);

        otDeviceRole role = otThreadGetDeviceRole(instance);


        if (!announced && role >= OT_DEVICE_ROLE_CHILD) {
            ESP_LOGI(TAG, "Joined Thread → send announce");

            thread_comm_announce();
            announced = true;
        }

        esp_openthread_lock_release();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* =========================================================
 * OPENTHREAD WORKER TASK
 * ========================================================= */
static void ot_task_worker(void *arg)
{
    esp_log_level_set("OPENTHREAD", ESP_LOG_NONE);

    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config  = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    /* ---------- Init OpenThread ---------- */
    ESP_ERROR_CHECK(esp_openthread_init(&config));

    esp_netif_t *netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(netif);

    /* ---------- Dataset (các node giốg nhau) ---------- */
    otOperationalDataset dataset;
    otOperationalDatasetTlvs dataset_tlvs;
    memset(&dataset, 0, sizeof(dataset));

    dataset.mActiveTimestamp.mSeconds = 1;
    dataset.mComponents.mIsActiveTimestampPresent = true;

    dataset.mChannel = 15;
    dataset.mComponents.mIsChannelPresent = true;

    dataset.mPanId = 0x1234;
    dataset.mComponents.mIsPanIdPresent = true;

    uint8_t ext_panid[OT_EXT_PAN_ID_SIZE] =
        {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0x12,0x34};
    memcpy(dataset.mExtendedPanId.m8, ext_panid, OT_EXT_PAN_ID_SIZE);
    dataset.mComponents.mIsExtendedPanIdPresent = true;

    const char network_name[] = "OpenThread-ESP";
    strncpy(dataset.mNetworkName.m8,
            network_name,
            OT_NETWORK_NAME_MAX_SIZE - 1);
    dataset.mComponents.mIsNetworkNamePresent = true;

    uint8_t network_key[OT_NETWORK_KEY_SIZE] =
        {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
         0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    memcpy(dataset.mNetworkKey.m8,
           network_key,
           OT_NETWORK_KEY_SIZE);
    dataset.mComponents.mIsNetworkKeyPresent = true;

    otMeshLocalPrefix prefix =
        {{0xfd,0x00,0xca,0xfe,0xba,0xbe,0x00,0x00}};
    memcpy(&dataset.mMeshLocalPrefix, &prefix, sizeof(prefix));
    dataset.mComponents.mIsMeshLocalPrefixPresent = true;

    otDatasetConvertToTlvs(&dataset, &dataset_tlvs);

    /* ---------- Start Thread ---------- */
    esp_openthread_lock_acquire(portMAX_DELAY);

    otInstance *instance = esp_openthread_get_instance();
    otDatasetSetActiveTlvs(instance, &dataset_tlvs);
    otIp6SetEnabled(instance, true);
    otThreadSetEnabled(instance, true);

    esp_openthread_lock_release();

    /* ---------- Init thread_comm ---------- */
    esp_openthread_lock_acquire(portMAX_DELAY);
    thread_comm_init(instance, "node_2", "receiver");
    thread_comm_register_rx_cb(on_thread_data);
    esp_openthread_lock_release();

    ESP_LOGI(TAG, "thread_comm initialized");

    /* ---------- App task ---------- */
    xTaskCreate(app_task, "app_task", 4096, NULL, 3, NULL);

    /* ---------- OpenThread mainloop ---------- */
    esp_openthread_launch_mainloop();
}

/* =========================================================
 * app_main
 * ========================================================= */
void app_main(void)
{
    esp_vfs_eventfd_config_t eventfd_cfg = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_cfg));

    xTaskCreate(
        ot_task_worker,
        "ot_node_2",
        10240,
        NULL,
        5,
        NULL);
}
