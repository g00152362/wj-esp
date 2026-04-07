#pragma once

#include <functional>
#include <cstdint>
#include <cstddef>
#include "esp_err.h"

#ifdef CONFIG_USE_BLE_DATA_SERVICE

/**
 * @brief BLE Data Service class for custom GATT communication
 *
 * This class implements a BLE GATT Server with custom service and characteristics
 * for data communication with mobile apps. It supports data read/write and notify operations.
 */
class BleDataService {
public:
    // Callback types
    using OnReceiveCallback = std::function<void(const uint8_t* data, size_t len)>;
    using OnConnectCallback = std::function<void(uint16_t conn_handle)>;
    using OnDisconnectCallback = std::function<void(uint16_t conn_handle, int reason)>;

    /**
     * @brief Get the singleton instance of the BleDataService class.
     */
    static BleDataService& GetInstance();

    /**
     * @brief Initializes the BLE stack and GATT services.
     * @return ESP_OK on success, otherwise an error code.
     */
    esp_err_t Init();

    /**
     * @brief Deinitializes BLE and releases resources.
     * @return ESP_OK on success, otherwise an error code.
     */
    esp_err_t Deinit();

    /**
     * @brief Start BLE advertising.
     * @return ESP_OK on success, otherwise an error code.
     */
    esp_err_t StartAdvertising();

    /**
     * @brief Stop BLE advertising.
     * @return ESP_OK on success, otherwise an error code.
     */
    esp_err_t StopAdvertising();

    /**
     * @brief Send data to connected client via notify characteristic.
     * @param data Pointer to data buffer.
     * @param len Length of data.
     * @return ESP_OK on success, otherwise an error code.
     */
    esp_err_t Notify(const uint8_t* data, size_t len);

    /**
     * @brief Check if a client is connected.
     * @return true if connected, false otherwise.
     */
    bool IsConnected() const;

    /**
     * @brief Get current connection handle.
     * @return Connection handle, 0xFFFF if not connected.
     */
    uint16_t GetConnHandle() const;

    /**
     * @brief Set callback for data reception.
     * @param cb Callback function.
     */
    void SetOnReceive(OnReceiveCallback cb);

    /**
     * @brief Set callback for client connection.
     * @param cb Callback function.
     */
    void SetOnConnect(OnConnectCallback cb);

    /**
     * @brief Set callback for client disconnection.
     * @param cb Callback function.
     */
    void SetOnDisconnect(OnDisconnectCallback cb);

    /**
     * @brief Handle received data (called by GATT callback).
     * @param data Pointer to received data.
     * @param len Length of received data.
     */
    void HandleReceivedData(const uint8_t* data, size_t len);

    // Delete copy constructor and assignment operator for singleton
    BleDataService(const BleDataService&) = delete;
    BleDataService& operator=(const BleDataService&) = delete;

private:
    BleDataService();
    ~BleDataService();

    // Internal state
    bool inited_ = false;
    bool advertising_ = false;
    uint16_t conn_handle_ = 0xFFFF;
    uint16_t notify_attr_handle_ = 0;
    bool notify_enabled_ = false;

    // Callbacks
    OnReceiveCallback on_receive_cb_;
    OnConnectCallback on_connect_cb_;
    OnDisconnectCallback on_disconnect_cb_;

    // NimBLE callbacks (static trampolines)
    static void OnReset(int reason);
    static void OnSync();
    static void HostTask(void* param);
    static int GapEventHandler(struct ble_gap_event* event, void* arg);
    static int GattAccessHandler(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt* ctxt, void* arg);

    // Internal methods
    esp_err_t InitGattServices();
    void StartAdvertisingInternal();

    // Friend functions for static callbacks
    friend int GattWriteCallback(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt* ctxt, void* arg);
    friend int GattReadCallback(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt* ctxt, void* arg);
};

#endif // CONFIG_USE_BLE_DATA_SERVICE
