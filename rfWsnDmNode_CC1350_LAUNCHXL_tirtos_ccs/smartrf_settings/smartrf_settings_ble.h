#ifndef _SMARTRF_SETTINGS_BLE_H_
#define _SMARTRF_SETTINGS_BLE_H_


//*********************************************************************************
// These settings have been generated for use with TI-RTOS and cc13xxware
//
// Generated by SmartRF Studio version 2.4.3 (build #20)
// Tested for TI-RTOS version tirtos_simplelink_2_21_xx
// Device: CC1350 Rev. 2.1
//
//*********************************************************************************
#ifdef DEVICE_FAMILY
    #undef DEVICE_FAMILY_PATH
    #define DEVICE_FAMILY_PATH(x) <ti/devices/DEVICE_FAMILY/x>
#else
    #error "You must define DEVICE_FAMILY at the project level as one of cc26x0, cc26x0r2, cc13x0, etc."
#endif

#include DEVICE_FAMILY_PATH(driverlib/rf_mailbox.h)
#include DEVICE_FAMILY_PATH(driverlib/rf_common_cmd.h)
#include DEVICE_FAMILY_PATH(driverlib/rf_ble_cmd.h)

#include <ti/drivers/rf/RF.h>

// TI-RTOS RF Mode Object
extern RF_Mode *RF_pModeBle;


// RF Core API commands
extern rfc_CMD_RADIO_SETUP_t *RF_ble_pCmdRadioSetup;
extern rfc_CMD_FS_t *RF_ble_pCmdFs;
extern rfc_CMD_BLE_ADV_NC_t *RF_ble_pCmdBleAdvNc;
extern rfc_CMD_BLE_GENERIC_RX_t *RF_ble_pCmdBleGenericRx;




#endif // _SMARTRF_SETTINGS_BLE_H_
