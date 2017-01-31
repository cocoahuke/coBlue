//
//  comm_file_transfer.h
//  bluetooth_practise
//
//  Created by huke on 1/14/17.
//  Copyright (c) 2017 com.cocoahuke. All rights reserved.
//

#include "common.h"

void comm_file_transfer_xREAD(struct gatt_db_attribute *attrib,
                         unsigned int id, uint16_t offset,
                         uint8_t opcode, struct bt_att *att,
                         void *user_data);
void comm_file_transfer_xWRITE(struct gatt_db_attribute *attrib,
                          unsigned int id, uint16_t offset,
                          const uint8_t *value, size_t len,
                          uint8_t opcode, struct bt_att *att,
                          void *user_data);