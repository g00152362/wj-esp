#include "ble_data_service.h"

// #ifdef CONFIG_USE_BLE_DATA_SERVICE

#include <cstring>
#include "esp_log.h"
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char* TAG = "BleDataService";

// Custom Service UUID: 12345678-1234-5678-1234-56789ABCDEF0
static const ble_uuid128_t BLE_DATA_SERVICE_UUID =
    BLE_UUID128_INIT(0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

// Write Characteristic UUID: 12345678-1234-5678-1234-56789ABCDEF1
static const ble_uuid128_t BLE_DATA_WRITE_CHAR_UUID =
    BLE_UUID128_INIT(0xF1, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

// Notify Characteristic UUID: 12345678-1234-5678-1234-56789ABCDEF2
static const ble_uuid128_t BLE_DATA_NOTIFY_CHAR_UUID =
    BLE_UUID128_INIT(0xF2, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

// Forward declarations for GATT callbacks
static int WriteCharAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt* ctxt, void* arg);
static int NotifyCharAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt* ctxt, void* arg);

// GATT service definition
static uint16_t s_notify_attr_handle;

static const struct ble_gatt_chr_def gatt_chars[] = {
    {
        // Write Characteristic - Client writes data to device
        .uuid = &BLE_DATA_WRITE_CHAR_UUID.u,
        .access_cb = WriteCharAccessCb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {
        // Notify Characteristic - Device sends data to client
        .uuid = &BLE_DATA_NOTIFY_CHAR_UUID.u,
        .access_cb = NotifyCharAccessCb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &s_notify_attr_handle,
    },
    {
        // Terminator
        .uuid = nullptr,
    },
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_DATA_SERVICE_UUID.u,
        .includes = nullptr,
        .characteristics = gatt_chars,
    },
    {
        // Terminator
        .type = 0,
    },
};

BleDataService& BleDataService::GetInstance() {
    static BleDataService instance;
    return instance;
}

BleDataService::BleDataService()
    : inited_(false),
      advertising_(false),
      conn_handle_(0xFFFF),
      notify_attr_handle_(0),
      notify_enabled_(false) {
}

BleDataService::~BleDataService() {
    if (inited_) {
        Deinit();
    }
}

esp_err_t BleDataService::Init() {
    if (inited_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing BLE Data Service");

    esp_err_t ret;

    // Initialize Bluetooth controller
#if CONFIG_BT_CONTROLLER_ENABLED
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init BT controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable BT controller: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return ret;
    }
#endif

    // Initialize NimBLE stack
    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE: %s", esp_err_to_name(ret));
#if CONFIG_BT_CONTROLLER_ENABLED
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
#endif
        return ret;
    }

    // Configure NimBLE host
    ble_hs_cfg.reset_cb = OnReset;
    ble_hs_cfg.sync_cb = OnSync;
    ble_hs_cfg.gatts_register_cb = nullptr;
    ble_hs_cfg.store_status_cb = nullptr;

    // Security settings - No bonding for simple data transfer
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_sc = 0;

    // Initialize GATT services
    ret = InitGattServices();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init GATT services: %s", esp_err_to_name(ret));
        esp_nimble_deinit();
#if CONFIG_BT_CONTROLLER_ENABLED
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
#endif
        return ret;
    }

    // Set device name
#ifdef CONFIG_BLE_DATA_SERVICE_DEVICE_NAME
    ble_svc_gap_device_name_set(CONFIG_BLE_DATA_SERVICE_DEVICE_NAME);
#else
    ble_svc_gap_device_name_set("Xiaozhi-BLE");
#endif

    // Mark as initialized before starting host task (OnSync may be called immediately)
    inited_ = true;

    // Start NimBLE host task
    ret = esp_nimble_enable((void*)HostTask);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable NimBLE: %s", esp_err_to_name(ret));
        inited_ = false;
        esp_nimble_deinit();
#if CONFIG_BT_CONTROLLER_ENABLED
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
#endif
        return ret;
    }

    ESP_LOGI(TAG, "BLE Data Service initialized");
    return ESP_OK;
}

esp_err_t BleDataService::Deinit() {
    if (!inited_) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing BLE Data Service");

    esp_err_t ret = nimble_port_stop();
    if (ret == ESP_OK) {
        esp_nimble_deinit();
    }

#if CONFIG_BT_CONTROLLER_ENABLED
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
#endif

    inited_ = false;
    advertising_ = false;
    conn_handle_ = 0xFFFF;
    notify_enabled_ = false;

    return ret;
}

esp_err_t BleDataService::InitGattServices() {
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add svcs failed: %d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t BleDataService::StartAdvertising() {
    if (!inited_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_handle_ != 0xFFFF) {
        ESP_LOGW(TAG, "Already connected, not advertising");
        return ESP_OK;
    }

    StartAdvertisingInternal();
    return ESP_OK;
}

void BleDataService::StartAdvertisingInternal() {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    ESP_LOGI(TAG, "Starting advertising...");

    memset(&fields, 0, sizeof(fields));

    // Discoverable and BLE-only
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Include device name
    const char* name = ble_svc_gap_device_name();
    fields.name = (uint8_t*)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting adv fields: %d", rc);
        return;
    }

    // Set advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                           &adv_params, GapEventHandler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertising: %d", rc);
        return;
    }

    advertising_ = true;
    ESP_LOGI(TAG, "Advertising started, device name: %s", name);
}

esp_err_t BleDataService::StopAdvertising() {
    if (!advertising_) {
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Error stopping advertising: %d", rc);
        return ESP_FAIL;
    }

    advertising_ = false;
    ESP_LOGI(TAG, "Advertising stopped");
    return ESP_OK;
}

esp_err_t BleDataService::Notify(const uint8_t* data, size_t len) {
    if (!inited_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_handle_ == 0xFFFF) {
        ESP_LOGW(TAG, "Not connected, cannot notify");
        return ESP_ERR_INVALID_STATE;
    }

    if (!notify_enabled_) {
        ESP_LOGW(TAG, "Notifications not enabled by client");
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (om == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle_, s_notify_attr_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Notify failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Notified %zu bytes", len);
    return ESP_OK;
}

bool BleDataService::IsConnected() const {
    return conn_handle_ != 0xFFFF;
}

uint16_t BleDataService::GetConnHandle() const {
    return conn_handle_;
}

void BleDataService::SetOnReceive(OnReceiveCallback cb) {
    on_receive_cb_ = std::move(cb);
}

void BleDataService::SetOnConnect(OnConnectCallback cb) {
    on_connect_cb_ = std::move(cb);
}

void BleDataService::SetOnDisconnect(OnDisconnectCallback cb) {
    on_disconnect_cb_ = std::move(cb);
}

void BleDataService::HandleReceivedData(const uint8_t* data, size_t len) {
    if (on_receive_cb_) {
        on_receive_cb_(data, len);
    }
}

// Static callback implementations
void BleDataService::OnReset(int reason) {
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

void BleDataService::OnSync() {
    ESP_LOGI(TAG, "NimBLE synced");

    // Generate a random address if needed
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Device address not available, rc=%d", rc);
        return;
    }

    // Print device address
    uint8_t addr[6];
    uint8_t addr_type;
    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc == 0) {
        rc = ble_hs_id_copy_addr(addr_type, addr, nullptr);
        if (rc == 0) {
            ESP_LOGI(TAG, "Device address: %02x:%02x:%02x:%02x:%02x:%02x (type=%d)",
                     addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], addr_type);
        }
    }

    // Start advertising after sync
    auto& instance = GetInstance();
    if (instance.inited_) {
        instance.StartAdvertisingInternal();
    } else {
        ESP_LOGW(TAG, "Not starting advertising - not initialized");
    }
}

void BleDataService::HostTask(void* param) {
    ESP_LOGI(TAG, "BLE Host Task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int BleDataService::GapEventHandler(struct ble_gap_event* event, void* arg) {
    auto& instance = GetInstance();

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "Connection %s, status=%d",
                     event->connect.status == 0 ? "established" : "failed",
                     event->connect.status);
            if (event->connect.status == 0) {
                instance.conn_handle_ = event->connect.conn_handle;
                instance.advertising_ = false;
                if (instance.on_connect_cb_) {
                    instance.on_connect_cb_(event->connect.conn_handle);
                }
            } else {
                // Connection failed, restart advertising
                instance.StartAdvertisingInternal();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
            instance.conn_handle_ = 0xFFFF;
            instance.notify_enabled_ = false;
            if (instance.on_disconnect_cb_) {
                instance.on_disconnect_cb_(event->disconnect.conn.conn_handle,
                                           event->disconnect.reason);
            }
            // Restart advertising
            instance.StartAdvertisingInternal();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe event: cur_notify=%d, attr_handle=%d",
                     event->subscribe.cur_notify, event->subscribe.attr_handle);
            if (event->subscribe.attr_handle == s_notify_attr_handle) {
                instance.notify_enabled_ = event->subscribe.cur_notify;
                ESP_LOGI(TAG, "Notifications %s",
                         instance.notify_enabled_ ? "enabled" : "disabled");
            }
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU update: conn_handle=%d, mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;

        default:
            break;
    }

    return 0;
}

// GATT characteristic access callbacks
static int WriteCharAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt* ctxt, void* arg) {
          ESP_LOGW(TAG, "WriteCharAccessCb called with op=%d", ctxt->op);                      
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    // Get data from mbuf
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) {
        return 0;
    }
   ESP_LOGW(TAG, "WriteCharAccessCb called11111");      
    uint8_t* buf = (uint8_t*)malloc(len);
    if (buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate buffer for write");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
    if (rc != 0) {
        free(buf);
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGD(TAG, "Received %d bytes", copied);

    BleDataService::GetInstance().HandleReceivedData(buf, copied);

    free(buf);
    return 0;
}

static int NotifyCharAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt* ctxt, void* arg) {
    // This is called when client reads the notify characteristic
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Return empty data
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// #endif // CONFIG_USE_BLE_DATA_SERVICE
