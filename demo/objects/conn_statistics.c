/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../objects.h"
#include "../demo_utils.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

typedef enum {
    CS_SMS_TX_COUNTER       = 0,
    CS_SMS_RX_COUNTER       = 1,
    CS_TX_KB                = 2,
    CS_RX_KB                = 3,
    CS_MAX_MSG_SIZE         = 4,
    CS_AVG_MSG_SIZE         = 5,
    CS_START                = 6,
    CS_STOP                 = 7,
    CS_COLLECTION_PERIOD    = 8,
} conn_stats_res_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    uint64_t last_tx_bytes;
    uint64_t last_rx_bytes;
    bool is_collecting;
    uint32_t collection_period;
} conn_stats_repr_t;

static conn_stats_repr_t *get_cs(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return container_of(obj_ptr, conn_stats_repr_t, def);
}

static int read_uint64_from_file(uint64_t *number, const char *filename) {
    int result = 0;
    FILE *rx_bytes_file = fopen(filename, "r");

    if (!rx_bytes_file) {
        return -1;
    }
    if (fscanf(rx_bytes_file, "%" SCNu64, number) != 1) {
        result = -1;
    }
    fclose(rx_bytes_file);

    return result;
}

static int ifname_for_first_socket(anjay_t *anjay,
                                   avs_net_socket_interface_name_t *if_name) {
    avs_net_abstract_socket_t *const *first = anjay_get_sockets(anjay);
    if (!first) {
        return -1;
    }
    return avs_net_socket_interface_name(*first, if_name);
}

static const char* RX_STATS = "rx_bytes";
static const char* TX_STATS = "tx_bytes";

static int stats_getter(avs_net_socket_interface_name_t *if_name,
                        uint64_t *bytes,
                        const char* stats) {
    char file_name[128];
    sprintf(file_name, "/sys/class/net/%s/statistics/%s", *if_name, stats);
    return read_uint64_from_file(bytes, file_name);
}

static uint64_t first_socket_stats(anjay_t *anjay, const char* stats) {
    avs_net_socket_interface_name_t if_name;
    memset(&if_name, 0, sizeof(if_name));
    if (ifname_for_first_socket(anjay, &if_name)) {
        return 0;
    }
    uint64_t value = 0;
    stats_getter(&if_name, &value, stats);
    return value;
}

static uint64_t get_rx_stats(anjay_t *anjay, conn_stats_repr_t *repr) {
    if (repr->is_collecting) {
        return first_socket_stats(anjay, RX_STATS) - repr->last_rx_bytes;
    } else {
        return repr->last_rx_bytes;
    }
}

static uint64_t get_tx_stats(anjay_t *anjay, conn_stats_repr_t *repr) {
    if (repr->is_collecting) {
        return first_socket_stats(anjay, TX_STATS) - repr->last_tx_bytes;
    } else {
        return repr->last_tx_bytes;
    }
}

static int cs_instance_reset(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid) {
    (void) anjay;
    (void) iid;

    conn_stats_repr_t *repr = get_cs(obj_ptr);

    repr->last_tx_bytes = 0;
    repr->last_rx_bytes = 0;
    repr->is_collecting = false;
    repr->collection_period = 0;
    return 0;
}

static int cs_resource_execute(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_iid_t rid,
                               anjay_execute_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) ctx;
    conn_stats_repr_t *repr = get_cs(obj_ptr);
    switch ((conn_stats_res_t) rid) {
    case CS_SMS_TX_COUNTER:
    case CS_SMS_RX_COUNTER:
    case CS_TX_KB:
    case CS_RX_KB:
    case CS_MAX_MSG_SIZE:
    case CS_AVG_MSG_SIZE:
    case CS_COLLECTION_PERIOD:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    case CS_START:
        // TODO: actually use Collection Period resource
        repr->last_tx_bytes = first_socket_stats(anjay, TX_STATS);
        repr->last_rx_bytes = first_socket_stats(anjay, RX_STATS);
        repr->is_collecting = true;
        break;
    case CS_STOP:
        if (!repr->is_collecting) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        repr->last_tx_bytes =
            first_socket_stats(anjay, TX_STATS) - repr->last_tx_bytes;
        repr->last_rx_bytes =
            first_socket_stats(anjay, RX_STATS) - repr->last_rx_bytes;
        repr->is_collecting = false;
        break;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
    return 0;
}

static int cs_resource_read(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_output_ctx_t *ctx) {
    (void) iid;
    conn_stats_repr_t *repr = get_cs(obj_ptr);
    switch (rid) {
    case CS_SMS_TX_COUNTER:
    case CS_SMS_RX_COUNTER:
    case CS_MAX_MSG_SIZE:
    case CS_AVG_MSG_SIZE:
        return anjay_ret_i32(ctx, 0);
    case CS_TX_KB:
        return anjay_ret_i64(ctx, (int64_t) (get_tx_stats(anjay, repr) / 1024));
    case CS_RX_KB:
        return anjay_ret_i64(ctx, (int64_t) (get_rx_stats(anjay, repr) / 1024));
    case CS_COLLECTION_PERIOD:
        return anjay_ret_i64(ctx, (int64_t) repr->collection_period);
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
    return 0;
}

static int cs_resource_write(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    conn_stats_repr_t *repr = get_cs(obj_ptr);
    switch (rid) {
    case CS_COLLECTION_PERIOD: {
            int32_t val;
            int result;
            if ((result = anjay_get_i32(ctx, &val))) {
                return result;
            } else if (val < 0) {
                return ANJAY_ERR_BAD_REQUEST;
            }

            repr->collection_period = (uint32_t) val;
            return 0;
        }
    case CS_MAX_MSG_SIZE:
    case CS_AVG_MSG_SIZE:
    case CS_SMS_TX_COUNTER:
    case CS_SMS_RX_COUNTER:
    case CS_TX_KB:
    case CS_RX_KB:
    case CS_START:
    case CS_STOP:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    return 0;
}

static const anjay_dm_object_def_t CONN_STATISTICS = {
    .oid = DEMO_OID_CONN_STATISTICS,
    .supported_rids = ANJAY_DM_SUPPORTED_RIDS(
            CS_SMS_TX_COUNTER,
            CS_SMS_RX_COUNTER,
            CS_TX_KB,
            CS_RX_KB,
            CS_MAX_MSG_SIZE,
            CS_AVG_MSG_SIZE,
            CS_START,
            CS_STOP,
            CS_COLLECTION_PERIOD),
    .handlers = {
        .instance_it = anjay_dm_instance_it_SINGLE,
        .instance_present = anjay_dm_instance_present_SINGLE,
        .instance_reset = cs_instance_reset,
        .resource_present = anjay_dm_resource_present_TRUE,
        .resource_execute = cs_resource_execute,
        .resource_read = cs_resource_read,
        .resource_write = cs_resource_write,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **cs_object_create(void) {
    conn_stats_repr_t *repr = (conn_stats_repr_t *)
            avs_calloc(1, sizeof(conn_stats_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &CONN_STATISTICS;
    return &repr->def;
}

void cs_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_cs(def));
    }
}
