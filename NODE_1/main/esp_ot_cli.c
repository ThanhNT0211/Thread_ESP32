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

#define TAG "NODE_1"



/* =====================================================
 * RX CALLBACK (DATA ONLY)
 * ===================================================== */

static void on_thread_data(const char *from,
                           const char *payload,
                           const otIp6Address *src)
{
    char ip[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(src, ip, sizeof(ip));

    ESP_LOGI(TAG,
             "RX DATA from %s (%s): %s",
             from, ip, payload);
}

/* =====================================================
 * APP TASK
 * ===================================================== */

static void app_task(void *arg)
{
    ESP_LOGI(TAG, "Start DISCOVER phase");

    while (1) {
        esp_openthread_lock_acquire(portMAX_DELAY);
   
        thread_comm_print_nodes();
        esp_openthread_lock_release();

        ESP_LOGI(TAG, "DISCOVER sent");

        vTaskDelay(pdMS_TO_TICKS(3000));

        /* gửi data kiểm tra */
        esp_openthread_lock_acquire(portMAX_DELAY);
        thread_comm_send("node_2", "ping");
        esp_openthread_lock_release();

        /* không có warn thì báo oke */
        ESP_LOGI(TAG, "Check node_2 registration");
        vTaskDelay(pdMS_TO_TICKS(2000));

        break;
    }

    ESP_LOGI(TAG, "Enter DATA send loop");

    /* ===== SEND DATA LOOP ===== */
    while (1) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        thread_comm_send("node_2", "temp:25.5, hum:60");
        thread_comm_print_nodes();
        esp_openthread_lock_release();

        ESP_LOGI(TAG, "DATA sent to node_2");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* =====================================================
 * NETIF
 * ===================================================== */

static esp_netif_t *init_openthread_netif(
        const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif);

    ESP_ERROR_CHECK(
        esp_netif_attach(
            netif,
            esp_openthread_netif_glue_init(config))
    );

    return netif;
}

/* =====================================================
 * OPENTHREAD TASK
 * ===================================================== */

static void ot_task_worker(void *arg)
{
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config  = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_openthread_init(&config));

    esp_netif_t *netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(netif);

    /* ===== Dataset (các node giống nhau) ===== */
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
    memcpy(dataset.mExtendedPanId.m8,
           ext_panid,
           OT_EXT_PAN_ID_SIZE);
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

    /* ===== Start Thread ===== */
    esp_openthread_lock_acquire(portMAX_DELAY);

    otInstance *instance = esp_openthread_get_instance();
    otDatasetSetActiveTlvs(instance, &dataset_tlvs);
    otIp6SetEnabled(instance, true);
    otThreadSetEnabled(instance, true);

    esp_openthread_lock_release();

    vTaskDelay(pdMS_TO_TICKS(20000));

    ESP_LOGI(TAG, "Thread ready");

    /* ===== Init thread_comm ===== */
    esp_openthread_lock_acquire(portMAX_DELAY);
    thread_comm_init(instance, "node_1", "receiver"); // khởi tạo thread_comm với vai trò receiver

    vTaskDelay(pdMS_TO_TICKS(3000));

    for (int i = 0; i < 3; i++) {
       thread_comm_announce();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    thread_comm_register_rx_cb(on_thread_data);
    thread_comm_print_nodes();
    esp_openthread_lock_release();

    xTaskCreate(app_task, "app_task", 4096, NULL, 3, NULL);

    /* ===== OpenThread mainloop ===== */
    esp_openthread_launch_mainloop();
}

/* =====================================================
 * APP MAIN
 * ===================================================== */

void app_main(void)
{
    esp_vfs_eventfd_config_t eventfd = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd));

    xTaskCreate(
        ot_task_worker,
        "ot_node_1",
        10240,
        NULL,
        5,
        NULL
    );
}
