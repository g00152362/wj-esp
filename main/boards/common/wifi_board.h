#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"

enum class ProvisioningMode {
    WifiAp,
    Ble,
};

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    ProvisioningMode provisioning_mode_ = ProvisioningMode::WifiAp;
    void EnterProvisioningMode();
    void EnterWifiApProvisioningMode();
    virtual std::string GetBoardJson() override;

public:
    explicit WifiBoard(ProvisioningMode provisioning_mode = ProvisioningMode::WifiAp);
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual NetworkInterface* GetNetwork() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;
};

#endif // WIFI_BOARD_H
