# ESP32 WiFi Provisioning Migration Design

## Summary

This change migrates the `AIToy-s3-dualeye-lcd-0.71` board from WiFi AP provisioning to BLE provisioning without breaking existing WiFi-board behavior.

The common `WifiBoard` base class will gain a provisioning mode parameter:

- `WifiAp`: existing behavior, default for compatibility
- `Ble`: new behavior for boards that should provision over BLE

`WifiBoard::StartNetwork()` will remain the single entry point for startup networking decisions:

1. Check whether WiFi credentials exist.
2. If none exist, enter provisioning mode.
3. If credentials exist, try station connection.
4. If WiFi does not connect within 60 seconds, enter provisioning mode.
5. If WiFi connects, return and continue normal chat startup.

Provisioning mode will dispatch internally to either WiFi AP provisioning or BLE provisioning based on the `WifiBoard` constructor argument.

## Goals

- Keep existing WiFi AP provisioning code available for compatibility.
- Add BLE provisioning support in the common `WifiBoard` flow.
- Make `AIToy-s3-dualeye-lcd-0.71` use BLE provisioning.
- Preserve the existing BLE GATT UUIDs and JSON message format.
- Keep the existing `SsidManager` persistence path.
- Preserve current state/LED behavior for `kDeviceStateWifiConfiguring`.

## Non-Goals

- No mobile app protocol change.
- No change to BLE UUIDs, BLE service structure, or JSON field names.
- No deletion of existing WiFi AP provisioning functions.
- No broad refactor of other boards beyond keeping default behavior intact.

## Current State

`WifiBoard` currently assumes WiFi AP provisioning:

- `force_ap` in NVS forces provisioning mode on next boot.
- Missing WiFi credentials immediately enter AP provisioning.
- WiFi connection failure after station startup falls back to AP provisioning.

The target board file `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc` already contains a board-local BLE provisioning implementation using `BleDataService`, but it is not the authoritative provisioning path. This duplicates logic that is better centralized in `WifiBoard`.

## Design

### 1. Provisioning mode selection in `WifiBoard`

Add a provisioning mode enum to `WifiBoard`:

- `ProvisioningMode::WifiAp`
- `ProvisioningMode::Ble`

Add a `WifiBoard` constructor that accepts this mode, defaulting to `ProvisioningMode::WifiAp`.

This preserves behavior for existing boards that do not pass any parameter.

### 2. Keep a single startup decision point

`WifiBoard::StartNetwork()` will remain responsible for:

- reading the forced provisioning flag
- checking whether stored SSIDs exist
- starting station mode when credentials exist
- waiting for connection completion
- falling back to provisioning when connection fails

The existing AP-specific direct entry will be replaced with a common dispatcher:

- `EnterProvisioningMode()`

That dispatcher will call one of:

- `EnterWifiApProvisioningMode()`
- `EnterBleProvisioningMode()`

### 3. Preserve WiFi AP provisioning code

The existing AP provisioning logic in `WifiBoard` will be retained and renamed or reorganized as WiFi AP-specific behavior.

The old AP path is intentionally kept for compatibility:

- existing boards still default to AP provisioning
- previous AP code is not deleted
- comments will make it clear that AP mode is the compatibility/default path

This satisfies the requirement to keep the previous code and not remove it.

### 4. Add BLE provisioning helpers to `WifiBoard`

BLE provisioning support will be centralized in `WifiBoard` with helpers such as:

- `EnterBleProvisioningMode()`
- `StartBleProvisioning()`
- `StopBleProvisioning()`
- `HandleBleProvisioningData(const uint8_t* data, size_t len)`

`EnterBleProvisioningMode()` will:

- set `Application` state to `kDeviceStateWifiConfiguring`
- start BLE provisioning
- keep the task in provisioning mode until reboot

Unlike the current board-local BLE callback registration approach, BLE will not be tied to idle/listening state transitions. It will only run when the board is in provisioning mode. This is simpler and matches the requested flow.

### 5. BLE protocol behavior

The existing BLE protocol is preserved:

- `{"type":"read"}` returns device MAC address
- `{"type":"write","ssid":"...","pwd":"..."}` stores credentials and reboots

Response behavior remains:

- successful write replies with `{"status":"ok"}`
- device delays briefly, then restarts

Credential storage continues to use:

- `SsidManager::GetInstance().AddSsid(ssid, pwd)`

### 6. Target board changes

The target board `AIToy-s3-dualeye-lcd-0.71` will explicitly opt into BLE provisioning by calling the `WifiBoard` constructor with `ProvisioningMode::Ble`.

The board-local duplicated BLE provisioning implementation will no longer be the active provisioning path. The board file should stop managing BLE provisioning lifecycle itself so there is only one authoritative implementation.

The board keeps its existing hardware/display/button logic.

## Detailed Flow

### Flow A: no stored WiFi credentials

1. Boot device.
2. `WifiBoard::StartNetwork()` checks `SsidManager`.
3. No SSIDs found.
4. `EnterProvisioningMode()` is called.
5. For `AIToy`, mode dispatches to BLE provisioning.
6. Device enters `kDeviceStateWifiConfiguring`.
7. LED continues existing provisioning blink behavior.
8. BLE accepts provisioning data.
9. Credentials are saved.
10. Device reboots.

### Flow B: stored credentials and WiFi available

1. Boot device.
2. Stored SSIDs exist.
3. Station mode starts and scans.
4. A known AP is found and connected.
5. `StartNetwork()` returns.
6. Application continues normal startup and chat flow.

### Flow C: stored credentials but WiFi unavailable

1. Boot device.
2. Stored SSIDs exist.
3. Station mode starts.
4. No usable AP is connected within 60 seconds.
5. Station mode is stopped.
6. `EnterProvisioningMode()` is called.
7. For `AIToy`, BLE provisioning starts.

### Flow D: forced provisioning after reset

1. `ResetWifiConfiguration()` stores `force_ap = 1`.
2. Device reboots.
3. `WifiBoard` sees forced provisioning on boot.
4. Forced provisioning flag is cleared.
5. `EnterProvisioningMode()` is called.
6. Actual provisioning transport depends on the board mode:
   `WifiAp` boards enter AP provisioning, `Ble` boards enter BLE provisioning.

## Error Handling

### BLE init failure

If BLE provisioning startup fails:

- remain in provisioning state
- log the error
- show a provisioning failure or waiting message on display if practical
- do not silently continue into normal chat startup
- do not automatically switch to AP mode for BLE-configured boards

This keeps behavior deterministic.

### Invalid BLE payloads

The following inputs are ignored without saving credentials:

- invalid JSON
- missing `type`
- unknown `type`
- missing `ssid` or `pwd` for write requests
- SSID/password exceeding expected size limits

For these cases:

- log the error
- do not write NVS
- do not reboot

### Successful provisioning write

For a valid `write` request:

- save credentials through `SsidManager`
- send success response
- delay about 1 second
- restart device

## Compatibility Notes

- Existing boards remain on WiFi AP provisioning by default because the new constructor parameter defaults to `WifiAp`.
- Existing WiFi AP provisioning code remains in the tree and remains callable.
- Existing button behavior that triggers `ResetWifiConfiguration()` stays valid.
- Existing `force_ap` key name is preserved to avoid broad compatibility changes. Semantically it becomes "force provisioning on next boot," while transport is selected by provisioning mode.

## Files Expected To Change

- `main/boards/common/wifi_board.h`
- `main/boards/common/wifi_board.cc`
- `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc`

Potentially, only if required by build/linkage cleanup:

- `main/boards/common/ble_data_service.h`
- `main/boards/common/ble_data_service.cpp`

## Verification Plan

### Build verification

- Build the target `AIToy-s3-dualeye-lcd-0.71` board successfully.
- Build at least one unchanged default `WifiBoard`-based board successfully to confirm AP mode compatibility.

### Runtime verification

1. No stored WiFi credentials:
   boot enters BLE provisioning and LED blinks in provisioning state.
2. Stored credentials with reachable AP:
   boot connects normally and enters chat flow.
3. Stored credentials with unreachable AP:
   after timeout, device enters BLE provisioning.
4. BLE protocol compatibility:
   existing phone-side `read` and `write` messages still work unchanged.
5. Successful write:
   SSID/password are saved, success reply is sent, device restarts, and next boot connects using stored credentials.

## Implementation Notes

- Keep comments short and explicit where the old AP path is preserved for compatibility.
- Avoid two active BLE provisioning implementations. Common behavior belongs in `WifiBoard`.
- Prefer minimal behavioral change outside the provisioning transport selection.

## Acceptance Criteria

- `AIToy-s3-dualeye-lcd-0.71` provisions over BLE instead of WiFi AP.
- Existing WiFi AP provisioning functions are still present.
- Default `WifiBoard` behavior remains WiFi AP if no mode parameter is passed.
- Startup and fallback behavior match the requested five-step flow.
- Existing BLE app protocol works without modification.
