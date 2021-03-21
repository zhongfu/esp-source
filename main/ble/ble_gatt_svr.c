// Copyright 2015-2020 The Apache Software Foundation
// Modifications Copyright 2017-2020 Espressif Systems (Shanghai) CO., LTD.
//
// Portions of this software were developed at Runtime Inc, copyright 2015.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"

//#include "bleprph.h"
#include "../sling/sling_setup.h"

static const char* TAG = "ble_gatt";

/**
 * The vendor specific security test service consists of two characteristics:
 *     o random-number-generator: generates a random 32-bit number each time
 *       it is read.
 *     o static-value: a single-byte characteristic that can always be read.
 */

// 5005acad-ad27-42eb-88d2-12dccec5ccb0
static const ble_uuid128_t gatt_svc_conf_uuid =
    BLE_UUID128_INIT(0xb0, 0xcc, 0xc5, 0xce, 0xdc, 0x12, 0xd2, 0x88, 0xeb, 0x42, 0x27, 0xad, 0xad, 0xac, 0x05, 0x50);

// 5005acad-2d93-498c-920d-1ba2965a3e8a
static const ble_uuid128_t gatt_chr_conf_secret_uuid =
    BLE_UUID128_INIT(0x8a, 0x3e, 0x5a, 0x96, 0xa2, 0x1b, 0x0d, 0x92, 0x8c, 0x49, 0x93, 0x2d, 0xad, 0xac, 0x05, 0x50);

static int gatt_chr_conf_secret_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] =
{
    {
        /*** Service: Security test. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svc_conf_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /*** Characteristic: Random number generator. */
                .uuid = &gatt_chr_conf_secret_uuid.u,
                .access_cb = gatt_chr_conf_secret_cb,
                .flags = BLE_GATT_CHR_F_READ
            },
            // {
            //     /*** Characteristic: Static value. */
            //     .uuid = &gatt_svr_chr_sec_test_static_uuid.u,
            //     .access_cb = gatt_svr_chr_access_sec_test,
            //     .flags = BLE_GATT_CHR_F_READ |
            //     BLE_GATT_CHR_F_WRITE
            // },
            {
                0, /* No more characteristics in this service. */
            }
        },
    },
    {
        0, /* No more services. */
    },
};

// static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
//                    void *dst, uint16_t *len) {
//     uint16_t om_len;
//     int rc;

//     om_len = OS_MBUF_PKTLEN(om);
//     if (om_len < min_len || om_len > max_len) {
//         return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
//     }

//     rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
//     if (rc != 0) {
//         return BLE_ATT_ERR_UNLIKELY;
//     }

//     return 0;
// }

static int gatt_chr_conf_secret_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg) {
    //const ble_uuid_t *uuid;
    int rc;

    //uuid = ctxt->chr->uuid;

    /* Determine which characteristic is being accessed by examining its
     * 128-bit UUID.
     */

    // if (ble_uuid_cmp(uuid, &gatt_svr_chr_sec_test_rand_uuid.u) == 0) {
    //     assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);

    //     /* Respond with a 32-bit random number. */
    //     rand_num = rand();
    //     rc = os_mbuf_append(ctxt->om, &rand_num, sizeof rand_num);
    //     return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    // }

    // if (ble_uuid_cmp(uuid, &gatt_svr_chr_sec_test_static_uuid.u) == 0) {
    //     switch (ctxt->op) {
    //     case BLE_GATT_ACCESS_OP_READ_CHR:
    //         rc = os_mbuf_append(ctxt->om, &gatt_svr_sec_test_static_val,
    //                             sizeof gatt_svr_sec_test_static_val);
    //         return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    //     case BLE_GATT_ACCESS_OP_WRITE_CHR:
    //         rc = gatt_svr_chr_write(ctxt->om,
    //                                 sizeof gatt_svr_sec_test_static_val,
    //                                 sizeof gatt_svr_sec_test_static_val,
    //                                 &gatt_svr_sec_test_static_val, NULL);
    //         return rc;

    //     default:
    //         assert(0);
    //         return BLE_ATT_ERR_UNLIKELY;
    //     }
    // }

    // we only have a single read characteristic now
    assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);

    char secret[37] = {0};
    sling_get_secret(secret);
    rc = os_mbuf_append(ctxt->om, secret, 37);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    /* Unknown characteristic; the nimble stack should not have called this
     * function.
     */
    // assert(0);
    // return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "registering characteristic %s with "
                    "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int gatt_svr_init(void) {
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_ans_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
