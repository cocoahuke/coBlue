//
//  COMM_Terminal.c
//  bluetooth_practise
//
//  Created by huke on 1/14/17.
//  Copyright (c) 2017 com.cocoahuke. All rights reserved.
//

#include "comm_Terminal.h"

/*
 Because the Apple device MTU limit, In my macbook can only accept 23 bytes each time
 */

#define STDSIZE 20

#define TERMINAL_IDLE 0x1
#define TERMINAL_NEED_RESPOND 0x2
#define TERMINAL_STILL_IN_EXEC 0x4
#define TERMINAL_EXEC_DONE 0x8
#define TERMINAL_GOT_RETURN 0x10

int terminal_need_respond = 0;

int terminal_status = TERMINAL_IDLE;
char pub_buf[STDSIZE];

void comm_Terminal_xREAD(struct gatt_db_attribute *attrib,
                                unsigned int id, uint16_t offset,
                                uint8_t opcode, struct bt_att *att,
                                void *user_data){

    connect_verify(NULL,0);

    char remote_buf[STDSIZE];
    bzero(remote_buf,STDSIZE);

    if(terminal_status&TERMINAL_NEED_RESPOND){
        if(terminal_status&TERMINAL_EXEC_DONE){
            terminal_status&=~TERMINAL_NEED_RESPOND;
        }
        *(uint32_t*)remote_buf = terminal_status;
        DEBUG_OUTPUT("read return1\n");
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,strlen(remote_buf));
        return;
    }

    if(terminal_status&(TERMINAL_EXEC_DONE|TERMINAL_GOT_RETURN)){
        while(!*(uint32_t*)pub_buf){
            if(terminal_status==TERMINAL_EXEC_DONE){
                terminal_status = TERMINAL_IDLE;
                break;
            }
        }
        memcpy(remote_buf,pub_buf,STDSIZE);
        bzero(pub_buf,STDSIZE);
        //DEBUG_OUTPUT("read return2:(0x%x) %s\n",*(uint32_t*)remote_buf,remote_buf);
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,strlen(remote_buf));
        return;
    }

    *(uint32_t*)remote_buf = terminal_status;
    DEBUG_OUTPUT("read return3\n");
    gatt_db_attribute_read_result(attrib,id,0,remote_buf,strlen(remote_buf));
    return;
}

void terminal_write(char *cmd){

    terminal_status = TERMINAL_NEED_RESPOND;
    bzero(pub_buf,STDSIZE);
    terminal_status|=TERMINAL_STILL_IN_EXEC;

    DEBUG_OUTPUT("exec cmd: [%s]\n",(char*)cmd);

    FILE* in_pipe = popen(cmd,"r");
    if(!in_pipe){
        DEBUG_OUTPUT("本地执行出错\n");prog_quit(0);
    }

    terminal_status&=~TERMINAL_STILL_IN_EXEC;

    while(fgets(pub_buf,STDSIZE,in_pipe)){
        terminal_status|=TERMINAL_EXEC_DONE|TERMINAL_GOT_RETURN;
        while(*(uint32_t*)pub_buf){
        }
    }

    if(terminal_status&TERMINAL_GOT_RETURN){
        terminal_status = TERMINAL_EXEC_DONE;
    }
    else{
        terminal_status |= TERMINAL_EXEC_DONE;
    }

    pclose(in_pipe);
    free(cmd);
}

#define WRITE_RETURN gatt_db_attribute_write_result(attrib,id,0)

void comm_Terminal_xWRITE(struct gatt_db_attribute *attrib,
                                 unsigned int id, uint16_t offset,
                                 const uint8_t *value, size_t len,
                                 uint8_t opcode, struct bt_att *att,
                                 void *user_data)
{
    if(connect_verify(value,len)){
        WRITE_RETURN;return;
    }

    pthread_t wt = 0;
    void *cmd_buf = malloc(len+1);
    bzero(cmd_buf,len+1);
    memcpy(cmd_buf,value,len);
    pthread_create(&wt,NULL,(void *)terminal_write,cmd_buf);
    WRITE_RETURN;
}
