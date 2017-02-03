//
//  comm.h
//  bluetooth_practise
//
//  Created by huke on 1/14/17.
//  Copyright (c) 2017 com.cocoahuke. All rights reserved.
//

#ifndef __coblue_comm_h__
#define __coblue_comm_h__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/timeb.h>
#include <pwd.h>

#include "ble/lib/bluetooth.h"
#include "ble/lib/hci.h"
#include "ble/lib/hci_lib.h"
#include "ble/lib/l2cap.h"
#include "ble/lib/uuid.h"
#include "ble/lib/mgmt.h"

#include "ble/util/mainloop.h"
#include "ble/util/util.h"
#include "ble/util/mgmt.h"
#include "ble/att/att.h"
#include "ble/util/queue.h"
#include "ble/util/timeout.h"
#include "ble/gatt/gatt-db.h"
#include "ble/gatt/gatt-server.h"

static int COBLUE_DEBUG_OUTPUT=0;

#define COBLUE_INTERNAL_STDSIZE 1024
#define COBLUE_REBOOT_INTERVAL_SEC 1

static int COBLUE_RUN_WITHOUT_DAEMON=0;

#define COBLUE_PROG_NAME "coBlued" //coBlued
static char COBLUE_ADV_DEVICE_NAME[1024];
#define COBLUE_BLE_GATT_MTU 0

static int COBLUE_ATT_SECURITY=BT_SECURITY_MEDIUM;

#define COBLUE_SERVICE_UUID 0x1111
#define COBLUE_TERMINAL_UUID 0x2222
#define COBLUE_FILE_TRANSFER_UUID 0x3333

#define COBLUE_WRITE_FILE_PERMISSION S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH
static int COBLUE_WRITE_FILE_OWNER=1000;  //pi

static int COBLUE_ENABLE_MAC_FILTER=1;
static int COBLUE_MAXIMUM_FILTER_ENTRY=10;

static int COBLUE_ENABLE_VERIFICATION=1;
static int COBLUE_VERIFY_TIME_LIMIT=3;
int connect_verify(char *veriyuuid,size_t len);

#define EXC_PERROR(log) do{printf("%s:%d\n",__FUNCTION__,__LINE__);perror(log);prog_quit(0);}while(0)
#define EXC_PRINT(log,...) do{printf("%s:%d\n",__FUNCTION__,__LINE__);printf(log);prog_quit(0);}while(0)
#define DEBUG_OUTPUT(log,...) if(COBLUE_DEBUG_OUTPUT){printf(log);}
void prog_quit(int sig);
#endif
