/*
 * Copyright 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/******************************************************************************
 *
 *  Filename:      bt_vendor_brcm.c
 *
 *  Description:   Broadcom vendor specific library implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <utils/Log.h>
#include <fcntl.h>
#include <termios.h>
#include "bt_vendor_qcom.h"
#include "userial_vendor_qcom.h"
#include "bt_vendor_ar3k.h"
#include "userial_vendor_ar3k.h"
#include "upio.h"
/******************************************************************************
**  Externs
******************************************************************************/
extern int hw_config(int nState);

extern int is_hw_ready();
static const tUSERIAL_CFG userial_init_cfg =
{
    (USERIAL_DATABITS_8 | USERIAL_PARITY_NONE | USERIAL_STOPBITS_1),
    USERIAL_BAUD_115200
};

/******************************************************************************
**  Variables
******************************************************************************/
int pFd[2] = {0,};

bt_hci_transport_device_type bt_hci_transport_device;

bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
uint8_t vnd_local_bd_addr[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

int btSocAth=0;

/******************************************************************************
**  Local type definitions
******************************************************************************/


/******************************************************************************
**  Functions
******************************************************************************/

/*****************************************************************************
**
**   BLUETOOTH VENDOR INTERFACE LIBRARY FUNCTIONS
**
*****************************************************************************/

static int init(const bt_vendor_callbacks_t* p_cb, unsigned char *local_bdaddr)
{
    ALOGI("bt-vendor : init");
    btSocAth = is_bt_soc_ath();

    if (p_cb == NULL) {
        ALOGE("init failed with no user callbacks!");
        return -1;
    }

    if(btSocAth) {
        userial_vendor_init();
        upio_init();
    }

    //vnd_load_conf(VENDOR_LIB_CONF_FILE);

    /* store reference to user callbacks */
    bt_vendor_cbacks = (bt_vendor_callbacks_t *) p_cb;

    /* This is handed over from the stack */
    memcpy(vnd_local_bd_addr, local_bdaddr, 6);

    return 0;
}


/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
    int retval = 0;
    int nCnt = 0;
    int nState = -1;

    ALOGV("bt-vendor : op for %d", opcode);

    switch(opcode)
    {
        case BT_VND_OP_POWER_CTRL:
            {
                if (btSocAth) {
                    int *state = (int *) param;

                    ALOGI("BT_VND_OP_POWER_CTRL State: %d ", *state);

                    if (*state == BT_VND_PWR_OFF) {
                        upio_set_bluetooth_power(UPIO_BT_POWER_OFF);
                    }
                    else if (*state == BT_VND_PWR_ON) {
                        upio_set_bluetooth_power(UPIO_BT_POWER_ON);
                    }
                }
                else {
                    nState = *(int *) param;

                    retval = hw_config(nState);
                    if (nState == BT_VND_PWR_ON
                         && retval == 0
                         && is_hw_ready() == TRUE) {
                        retval = 0;
                    }
                    else {
                        retval = -1;
                    }
                }
            }
            break;

        case BT_VND_OP_FW_CFG:
            {
                // call hciattach to initalize the stack
                if (bt_vendor_cbacks) {
                   ALOGI("Bluetooth Firmware and transport is initialized");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
                }
                else {
                   ALOGE("Error : hci, smd initialization Error");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_FAIL);
                }
            }
            break;

        case BT_VND_OP_SCO_CFG:
            {
                bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_USERIAL_OPEN:
            {
                 if (btSocAth) {
                     int (*fd_array)[] = (int (*)[]) param;
                     int fd, idx;

                     ALOGI("BT_VND_OP_USERIAL_OPEN ");
                     fd = userial_vendor_open((tUSERIAL_CFG *) &userial_init_cfg);
                     if (fd != -1) {
                         for (idx=0; idx < CH_MAX; idx++)
                            (*fd_array)[idx] = fd;

                         retval = 1;
                     }
                 }
                 else {
                     if (bt_hci_init_transport(pFd) != -1) {
                         int (*fd_array)[] = (int (*) []) param;

                         (*fd_array)[CH_CMD] = pFd[0];
                         (*fd_array)[CH_EVT] = pFd[0];
                         (*fd_array)[CH_ACL_OUT] = pFd[1];
                         (*fd_array)[CH_ACL_IN] = pFd[1];
                     }
                     else {
                         retval = -1;
                         break;
                     }
                     retval = 2;
                 }

            }
            break;

        case BT_VND_OP_USERIAL_CLOSE:
            {
                if (btSocAth) {
                    ALOGI("AR3002 ::BT_VND_OP_USERIAL_CLOSE ");
                    userial_vendor_close();
                }
                else {
                    bt_hci_deinit_transport(pFd);
                }
            }
            break;

        case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
            {
                if (btSocAth) {
                    uint32_t *timeout_ms = (uint32_t *) param;
                    *timeout_ms = 1000;
                }
            }
            break;

        case BT_VND_OP_LPM_SET_MODE:
            ALOGI("BT_VND_OP_LPM_SET_MODE");

            if(btSocAth) {
                uint8_t *mode = (uint8_t *) param;

                if (*mode) {
                    lpm_set_ar3k(UPIO_LPM_MODE, UPIO_ASSERT, 0);
                }
                else {
                    lpm_set_ar3k(UPIO_LPM_MODE, UPIO_DEASSERT, 0);
                }

                bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS);
            }
            else {
                bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_LPM_WAKE_SET_STATE:

            if(btSocAth) {
                uint8_t *state = (uint8_t *) param;
                uint8_t wake_assert = (*state == BT_VND_LPM_WAKE_ASSERT) ? \
                                            UPIO_ASSERT : UPIO_DEASSERT;
                lpm_set_ar3k(UPIO_BT_WAKE, wake_assert, 0);
            }
            break;
    }

    return retval;
}

/** Closes the interface */
static void cleanup( void )
{
    ALOGI("cleanup");

    if(btSocAth) {
        upio_cleanup();
    }
    bt_vendor_cbacks = NULL;
}

// Entry point of DLib
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    sizeof(bt_vendor_interface_t),
    init,
    op,
    cleanup
};
