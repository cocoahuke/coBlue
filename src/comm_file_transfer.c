//
//  comm_file_transfer.c
//  bluetooth_practise
//
//  Created by huke on 1/14/17.
//  Copyright (c) 2017 com.cocoahuke. All rights reserved.
//

#include "comm_file_transfer.h"

#define SEND_SIZE 20
#define RECEIVE_SIZE 512

#define FILE_TRANSFER_IDLE 0x1
#define FILE_TRANSFER_NEED_RESPOND 0x2
#define FILE_TRANSFER_DECIDING_RW 0x4
#define FILE_TRANSFER_DECIDING_PATH 0x8
#define FILE_TRANSFER_READFILE_SIZE 0x10
#define FILE_TRANSFER_READING_FILE 0x20
#define FILE_TRANSFER_WRITEFILE_PATH_NAME 0x40
#define FILE_TRANSFER_WRITEFILE_SIZE 0x80
#define FILE_TRANSFER_WRITING_FILE 0x100
#define FILE_TRANSFER_ERROR_MACRO_START FILE_TRANSFER_ERROR_OP_NOT_EXIST
#define FILE_TRANSFER_ERROR_OP_NOT_EXIST 0x200
#define FILE_TRANSFER_ERROR_FILEPATH_TOO_LONG 0x400
#define FILE_TRANSFER_ERROR_LOCALFILE_NOT_EXIST 0x800
#define FILE_TRANSFER_ERROR_LOCALFILE_ALREADY_EXIST 0x1000
#define FILE_TRANSFER_ERROR_LOCALFILE_ITS_DIRECTORY 0x2000
#define FILE_TRANSFER_ERROR_LOCALFILE_ITS_EMPTY_FILE 0x4000
#define FILE_TRANSFER_ERROR_LOCALFILE_FILE_TOO_LARGE 0x8000
#define FILE_TRANSFER_ERROR_LOCALFILE_FOPEN_FAILED 0x10000
#define FILE_TRANSFER_ERROR_MACRO_END FILE_TRANSFER_ERROR_LOCALFILE_FOPEN_FAILED

int isReadOP = 0;

char *file_path[RECEIVE_SIZE];FILE *file_fp=0;char file_buf[SEND_SIZE];size_t file_size=0;

int file_transfer_status = FILE_TRANSFER_IDLE;

void clear_and_restore_idle(){
    bzero(file_path,RECEIVE_SIZE);
    if(file_fp){fclose(file_fp);file_fp=0;}
    bzero(file_buf,SEND_SIZE);
    file_size = 0;
    file_transfer_status = FILE_TRANSFER_IDLE;
}

void comm_file_transfer_xREAD(struct gatt_db_attribute *attrib,
                              unsigned int id, uint16_t offset,
                              uint8_t opcode, struct bt_att *att,
                              void *user_data){
    connect_verify(NULL,0);

    char remote_buf[SEND_SIZE];
    bzero(remote_buf,SEND_SIZE);

    int i = FILE_TRANSFER_ERROR_MACRO_START;
    for(;i<=FILE_TRANSFER_ERROR_MACRO_END;i=i*2){
        if(file_transfer_status&i){
            DEBUG_OUTPUT("file_transfer read error status:0x%x\n",file_transfer_status);

            *(uint32_t*)remote_buf = file_transfer_status;
            clear_and_restore_idle();
            gatt_db_attribute_read_result(attrib,id,0,remote_buf,sizeof(uint32_t));
            return;
        }
    }

    if(!file_transfer_status&FILE_TRANSFER_NEED_RESPOND){
        *(uint32_t*)remote_buf = file_transfer_status;
        DEBUG_OUTPUT("file_transfer read need respond:0x%x\n",*(uint32_t*)remote_buf);
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,sizeof(uint32_t));
        return;
    }

    if(file_transfer_status&FILE_TRANSFER_DECIDING_RW){
        /*Step 2*/
        DEBUG_OUTPUT("file_transfer Step 2\n");

        file_transfer_status&=~FILE_TRANSFER_NEED_RESPOND;
        *(uint32_t*)remote_buf = file_transfer_status;
        DEBUG_OUTPUT("file_transfer deciding rw:0x%x\n",*(uint32_t*)remote_buf);
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,sizeof(uint32_t));
        return;
    }

    if(file_transfer_status&FILE_TRANSFER_DECIDING_PATH){
        /*Step 4*/
        DEBUG_OUTPUT("file_transfer Step 4\n");

        file_transfer_status&=~FILE_TRANSFER_NEED_RESPOND;
        *(uint32_t*)remote_buf = file_transfer_status;
        if(isReadOP){
            file_transfer_status&=~FILE_TRANSFER_DECIDING_PATH;
            file_transfer_status|=FILE_TRANSFER_NEED_RESPOND;
            file_transfer_status|=FILE_TRANSFER_READFILE_SIZE;
        }
        else{
            file_transfer_status&=~FILE_TRANSFER_DECIDING_PATH;
            //file_transfer_status|=FILE_TRANSFER_WRITEFILE_SIZE; //for Ask file size before writing
            file_transfer_status|=FILE_TRANSFER_WRITING_FILE;
        }
        DEBUG_OUTPUT("file_transfer deciding path:0x%x\n",*(uint32_t*)remote_buf);
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,sizeof(uint32_t));
        return;
    }

    if(file_transfer_status&FILE_TRANSFER_READFILE_SIZE){
        /*Step 5(read)*/
        DEBUG_OUTPUT("file_transfer Step 5(read)\n");

        file_transfer_status&=~FILE_TRANSFER_NEED_RESPOND;
        *(uint32_t*)remote_buf = (uint32_t)file_size;
        file_transfer_status&=~FILE_TRANSFER_READFILE_SIZE;
        file_transfer_status|=FILE_TRANSFER_READING_FILE;
        DEBUG_OUTPUT("file_transfer read filesize:0x%x\n",*(uint32_t*)remote_buf);
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,sizeof(uint32_t));
        return;
    }

    if(file_transfer_status&FILE_TRANSFER_READING_FILE){
        file_transfer_status&=~FILE_TRANSFER_NEED_RESPOND;
        /*Step 6(read)*/
        //*(uint32_t*)remote_buf = (uint32_t)file_size;
        DEBUG_OUTPUT("file_transfer Step 6(read)\n");

        size_t readsize = 0;

        /*if(file_size<=SEND_SIZE){
         readsize = fread(remote_buf,file_size,1,file_fp);
         }
         else{
         readsize = fread(remote_buf,SEND_SIZE,1,file_fp);
         }*/
        readsize = file_size<=SEND_SIZE?fread(remote_buf,1,file_size,file_fp):fread(remote_buf,1,SEND_SIZE,file_fp);
        file_size-=readsize;

        if(!file_size){
            DEBUG_OUTPUT("coblue terminal read op complete\n");
            clear_and_restore_idle();
            DEBUG_OUTPUT("file_transfer read:0x%x\n",*(uint32_t*)remote_buf);
            gatt_db_attribute_read_result(attrib,id,0,remote_buf,0);
            return;
        }

        DEBUG_OUTPUT("file_transfer read(%d):0x%x\n",readsize,*(uint32_t*)remote_buf);
        gatt_db_attribute_read_result(attrib,id,0,remote_buf,readsize);
        return;
    }

    *(uint32_t*)remote_buf = file_transfer_status;
    gatt_db_attribute_read_result(attrib,id,0,remote_buf,sizeof(uint32_t));
}

#define WRITE_RETURN gatt_db_attribute_write_result(attrib,id,0)
//occur "unlikely error" of GATT in Central if miss this line

void comm_file_transfer_xWRITE(struct gatt_db_attribute *attrib,
                               unsigned int id, uint16_t offset,
                               const uint8_t *value, size_t len,
                               uint8_t opcode, struct bt_att *att,
                               void *user_data){

    if(connect_verify(value,len)){
        WRITE_RETURN;return;
    }

    char *cmd = (char*)value;

    if(file_transfer_status&FILE_TRANSFER_NEED_RESPOND){
        DEBUG_OUTPUT("Please read respond first\n");WRITE_RETURN;return;
    }

    if(file_transfer_status==FILE_TRANSFER_IDLE){
        /*Step 1*/
        file_transfer_status=FILE_TRANSFER_NEED_RESPOND;
        file_transfer_status|=FILE_TRANSFER_DECIDING_RW;

        if(!strncmp(cmd,"read",4)){
            isReadOP = 1;
        }
        else if(!strncmp(cmd,"write",5)){
            isReadOP = 0;
        }
        else{
            file_transfer_status|=FILE_TRANSFER_ERROR_OP_NOT_EXIST;
        }
        WRITE_RETURN;
        return;
    }

    if(file_transfer_status&FILE_TRANSFER_DECIDING_RW){
        /*Step 3*/
        DEBUG_OUTPUT("file_transfer Step 3\n");

        file_transfer_status&=~FILE_TRANSFER_DECIDING_RW;
        file_transfer_status|=FILE_TRANSFER_NEED_RESPOND;
        file_transfer_status|=FILE_TRANSFER_DECIDING_PATH;

        if(len>RECEIVE_SIZE){
            file_transfer_status|=FILE_TRANSFER_ERROR_FILEPATH_TOO_LONG;
            WRITE_RETURN;
            return;
        }

        bzero(file_path,RECEIVE_SIZE);
        memcpy(file_path,cmd,len);

        DEBUG_OUTPUT("file_transfer file_path(len:%d):%s\n",strlen(file_path),file_path);

        if(isReadOP){
            struct stat fstat;
            if (stat(file_path,&fstat) < 0 )
            {
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_NOT_EXIST;
                WRITE_RETURN;
                return;
            }
            if(fstat.st_mode&__S_IFDIR){
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_ITS_DIRECTORY;
                WRITE_RETURN;
                return;
            }
            file_size = fstat.st_size;
            if(!file_size){
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_ITS_EMPTY_FILE;
                WRITE_RETURN;
                return;
            }
            if(file_size>UINT32_MAX){
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_FILE_TOO_LARGE;
                WRITE_RETURN;
                return;
            }
            file_fp = fopen(file_path,"r");
            if(!file_fp){
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_FOPEN_FAILED;
                WRITE_RETURN;
                return;
            }
        }
        else{
            if(!access(file_path,F_OK)){
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_ALREADY_EXIST;
                WRITE_RETURN;
                return;
            }

            file_fp = fopen(file_path,"a+");
            if(!file_fp){
                file_transfer_status|=FILE_TRANSFER_ERROR_LOCALFILE_FOPEN_FAILED;
                WRITE_RETURN;
                return;
            }

            chown(file_path,COBLUE_WRITE_FILE_OWNER,COBLUE_WRITE_FILE_OWNER);
            chmod(file_path,COBLUE_WRITE_FILE_PERMISSION);
        }

        WRITE_RETURN;
        return;
    }

    //Ask size of file before writing to the local file,but actually its not matter
    /*if(file_transfer_status&FILE_TRANSFER_WRITEFILE_SIZE){
     //Step 5(write) Deprecated
     if(COBLUE_DEBUG_OUTPUT)
     DEBUG_OUTPUT("file_transfer Step 5(write)\n");
     file_size = cmd?*(uint32_t*)cmd:0;
     if(COBLUE_DEBUG_OUTPUT)
     DEBUG_OUTPUT("file_transfer write filesize:0x%x\n",file_size);
     file_transfer_status&=~FILE_TRANSFER_WRITEFILE_SIZE;
     file_transfer_status|=FILE_TRANSFER_WRITING_FILE;
     WRITE_RETURN;
     return;
     }*/

    if(file_transfer_status&FILE_TRANSFER_WRITING_FILE){
        /*Step 5(write)*/

        DEBUG_OUTPUT("file_transfer Step 6(write)\n");

        size_t wrotesize = len;

        if(!len){
            DEBUG_OUTPUT("coblue terminal write op complete\n");

            clear_and_restore_idle();
            WRITE_RETURN;
            return;
        }

        fwrite(value,1,wrotesize,file_fp);

        DEBUG_OUTPUT("file_transfer write(%d):0x%x\n",wrotesize,*(uint32_t*)value);

        WRITE_RETURN;
        return;
    }

    WRITE_RETURN;
    return;
}
