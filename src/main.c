//
//  main.m
//  proj_bluetooth_practise
//
//  Created by huke on 12/29/16.
//  Copyright (c) 2016 com.cocoahuke. All rights reserved.
//

#include "common.h"
#include "comm_Terminal.h"
#include "comm_file_transfer.h"

/* Wrapper to get the index and opcode to the response callback */
struct command_data {
    uint16_t id;
    uint16_t op;
    void (*callback) (uint16_t id, uint16_t op, uint8_t status,
                      uint16_t len, const void *param);
};

struct gatt_db_attribute {
    struct gatt_db_service *service;
    uint16_t handle;
    bt_uuid_t uuid;
    uint32_t permissions;
    uint16_t value_len;
    uint8_t *value;
    
    gatt_db_read_t read_func;
    gatt_db_write_t write_func;
    void *user_data;
    
    unsigned int read_id;
    struct queue *pending_reads;
    
    unsigned int write_id;
    struct queue *pending_writes;
};

int COBLUE_DEBUG_OUTPUT=0;
int COBLUE_RUN_WITHOUT_DAEMON=0;
char COBLUE_ADV_DEVICE_NAME[1024];
int COBLUE_ATT_SECURITY=BT_SECURITY_MEDIUM;
int COBLUE_WRITE_FILE_OWNER=1000;  //pi
int COBLUE_ENABLE_MAC_FILTER=1;
int COBLUE_MAXIMUM_FILTER_ENTRY=10;
int COBLUE_ENABLE_VERIFICATION=1;
int COBLUE_VERIFY_TIME_LIMIT=3;

int connsk = 0;
struct bt_att *coBlue_att = NULL;
struct gatt_db *coBlue_gattdb = NULL;
struct bt_gatt_server *coBlue_gattserver = NULL;
struct mgmt *coBlue_mgmt = NULL;

#define macaddr_len 17 //11:22:33:44:55:66
char *filter_arr = NULL;
void **filter_arr_ptr = NULL;

#define uuid_len 36//BFBB3E3C-6488-4CB4-BB98-758829C5F508
char uuid_for_verify[uuid_len+1];
int done_verify = 0;

void simple_pipe_cmd(char *cmd,char *result);
void ble_set_advertise_enable(int enable);

int mgmt_init();
void mgmt_close();
void ble_mgmt_power_on();
void ble_mgmt_set_devicename(char *name);
void ble_mgmt_info();

void simple_pipe_cmd(char *cmd,char *result){
    bzero(result,COBLUE_INTERNAL_STDSIZE);
    char buffer[COBLUE_INTERNAL_STDSIZE];
    FILE* in_pipe = popen(cmd,"r");
    if (!in_pipe){
        exit(1);}
    
    if(fgets(buffer,COBLUE_INTERNAL_STDSIZE,in_pipe)){
        strcat(result,buffer);
    }
    
    pclose(in_pipe);
}

void keep_adversting_if_unconnect(){
    while(1){
        sleep(2);
        if(connsk){
            ble_set_advertise_enable(0);
            break;
        }
        else{
            ble_set_advertise_enable(1);
        }
    }
}

void ble_set_advertise_enable(int enable){
    
    int dev_id,dd;
    
    while(1){
        dev_id = hci_get_route(NULL);
        if(dev_id<0){
            EXC_PERROR("Device is not available");
        }
        else{
            break;
        }
    }
    
    dd = hci_open_dev(dev_id);
    if(dd<0){
        EXC_PERROR("open socket error");
    }
    
    struct hci_request rq;
    le_set_advertise_enable_cp adv_cp;
    uint8_t status;
    
    memset(&adv_cp, 0, sizeof(adv_cp));
    adv_cp.enable = enable;
    
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = OCF_LE_SET_ADVERTISE_ENABLE;
    rq.cparam = &adv_cp;
    rq.clen = LE_SET_ADVERTISE_ENABLE_CP_SIZE;
    rq.rparam = &status;
    rq.rlen = 1;
    
    if (hci_send_req(dd, &rq, 1000) < 0){
        EXC_PRINT("can't set advertise mode,Probably need root\n");
    }
    
    hci_close_dev(dd);
}

static void cmd_rsp(uint8_t status, uint16_t len, const void *param,
                    void *user_data)
{
    struct command_data *data = user_data;
    data->callback(data->op, data->id, status, len, param);
}

static unsigned int send_cmd(struct mgmt *mgmt, uint16_t op, uint16_t id,
                             uint16_t len, const void *param,
                             void (*cb)(uint16_t id, uint16_t op,
                                        uint8_t status, uint16_t len,
                                        const void *param))
{
    struct command_data *data;
    unsigned int send_id;
    
    data = new0(struct command_data, 1);
    if (!data)
        return 0;
    
    data->id = id;
    data->op = op;
    data->callback = cb;
    
    send_id = mgmt_send(mgmt, op, id, len, param, cmd_rsp, data, free);
    if (send_id == 0)
        free(data);
    
    return send_id;
}


static void setting_rsp(uint16_t op, uint16_t id, uint8_t status, uint16_t len,
                        const void *param)
{
    const uint32_t *rp = param;
    
    if (status != 0) {
        printf("mgmt:%s for hci%u failed with status 0x%02x (%s)\n",
               mgmt_opstr(op), id, status, mgmt_errstr(status));
        goto done;
    }
    
    if (len < sizeof(*rp)) {
        printf("mgmt:Too small %s response (%u bytes)\n",
               mgmt_opstr(op), len);
        goto done;
    }
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("mgmt: %s success\n",mgmt_opstr(op));
    
done:
    mainloop_quit();
}
static void cmd_setting(struct mgmt *mgmt, uint16_t index, uint16_t op,
                        int argc, char **argv)
{
    uint8_t val;
    
    val = 1;
    
    if (index == MGMT_INDEX_NONE)
        index = 0;
    if (send_cmd(mgmt, op, index, sizeof(val), &val, setting_rsp) == 0) {
        printf("mgmt:Unable to send %s cmd\n", mgmt_opstr(op));
        mainloop_quit();
    }
}

void ble_mgmt_power_on(){
    if(mgmt_init()) return;
    
    cmd_setting(coBlue_mgmt,MGMT_INDEX_NONE,MGMT_OP_SET_POWERED,NULL,NULL);
    mainloop_run();
    mgmt_close();
}

static void name_rsp(uint8_t status, uint16_t len, const void *param,
                     void *user_data)
{
    if (status != 0)
        error("mgmt:Unable to set local name with status 0x%02x (%s)",
              status, mgmt_errstr(status));
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("mgmt: set name success\n");
    
    mainloop_quit();
}

void ble_mgmt_set_devicename(char *name){
    if(mgmt_init()) return;
    
    if(!name) return;
    struct mgmt_cp_set_local_name cp;
    
    memset(&cp, 0, sizeof(cp));
    strncpy((char *) cp.name, name, HCI_MAX_NAME_LENGTH);
    //strncpy((char *)cp.short_name,short_name,MGMT_MAX_SHORT_NAME_LENGTH);
    
    if (mgmt_send(coBlue_mgmt, MGMT_OP_SET_LOCAL_NAME,0,sizeof(cp),&cp,name_rsp, NULL, NULL) == 0) {
        system("touch /home/pi/fgf");
        mainloop_quit();
    }
    
    mainloop_run();
    
    mgmt_close();
}

int mgmt_init(){
    mainloop_init();
    if(!(coBlue_mgmt=mgmt_new_default())){
        printf("failed to open mgmt socket\n");
        return -1;
    }
    
    return 0;
}

void mgmt_close(){
    mainloop_quit();
    mgmt_cancel_all(coBlue_mgmt);
    mgmt_unregister_all(coBlue_mgmt);
    mgmt_unref(coBlue_mgmt);
    coBlue_mgmt = NULL;
}

void verify_timer(){
    sleep(COBLUE_VERIFY_TIME_LIMIT);
    if(!done_verify){
        if(COBLUE_DEBUG_OUTPUT)
            printf("couldn't pass verify in %dsec,reboot coblue\n",COBLUE_VERIFY_TIME_LIMIT);
        prog_quit(0);
    }
}

int ble_att_listen_accept(bdaddr_t *servaddr){
    
    int sk;
    struct sockaddr_l2 servinfo,clitinfo;
    socklen_t infolen;
    struct bt_security btsec;
    char clitaddr[18];
    
    sk = socket(PF_BLUETOOTH,SOCK_SEQPACKET,BTPROTO_L2CAP);
    if(sk<0)
        EXC_PERROR("failed to create socket when create service\n");
    
    // set up service address
    memset(&servinfo, 0, sizeof(servinfo));
    servinfo.l2_family = AF_BLUETOOTH;
    servinfo.l2_cid = htobs(4); //ATT_CID
    servinfo.l2_bdaddr_type = BDADDR_LE_PUBLIC;
    bacpy(&servinfo.l2_bdaddr,servaddr);
    
    if(bind(sk,(struct sockaddr *)&servinfo,sizeof(servinfo))<0){
        close(sk);
        EXC_PERROR("failed to bind socket when create service\n");
    }
    
    //set the security level
    memset(&btsec, 0, sizeof(btsec));
    btsec.level = BT_SECURITY_LOW;
    
    if (setsockopt(sk,SOL_BLUETOOTH,BT_SECURITY,&btsec,
                   sizeof(btsec)) != 0) {
        close(sk);
        EXC_PRINT("failed to set L2CAP security level\n");
    }
    
    if (listen(sk, 10) < 0) {
        close(sk);
        EXC_PERROR("listening on socket failed\n");
    }
    
    printf("waiting for connections...\n");
    
    memset(&clitinfo, 0, sizeof(clitinfo));
    infolen = sizeof(clitinfo);
    int remotesk = accept(sk,(struct sockaddr *)&clitinfo,&infolen);
    
    char name[1024];
    
    if (remotesk < 0) {
        close(sk);
        EXC_PERROR("listening on socket failed\n");
    }
    
    ba2str(&clitinfo.l2_bdaddr,clitaddr);
    
    if(COBLUE_ENABLE_MAC_FILTER){
        for(int cur_amo=0;filter_arr_ptr[cur_amo];cur_amo++){
            if(!strcmp(clitaddr,filter_arr_ptr[cur_amo])){
                done_verify = 1;
                if(COBLUE_DEBUG_OUTPUT)
                    printf("connect from %s\n",clitaddr);
                
                close(sk);
                return remotesk;
            }
        }
    }
    
    if (COBLUE_ENABLE_VERIFICATION){
        done_verify = 0;
        if(COBLUE_DEBUG_OUTPUT)
            printf("unverify connect from %s\n",clitaddr);
        pthread_t v_timer;
        pthread_create(&v_timer,NULL,verify_timer,NULL);
        close(sk);
        return remotesk;
    }
    printf("acc4\n");
    if(COBLUE_DEBUG_OUTPUT)
        printf("Rejected connect from %s\n",clitaddr);
    
    close(remotesk);
    close(sk);
    return -1;
}

int connect_verify(char *veriyuuid,size_t len){
    if(COBLUE_ENABLE_VERIFICATION&&!done_verify){
        if(!veriyuuid||!len){
            prog_quit(0);
        }
        if(len!=uuid_len){
            prog_quit(0);
        }
        if(strncmp(uuid_for_verify,veriyuuid,len)){
            prog_quit(0);
        }
        if(COBLUE_DEBUG_OUTPUT)
            printf("Verify Pass!\n");
        done_verify = 1;
        return 1;
    }
    return 0;
}

void notifi_disconnect(int err, void *user_data){
    printf("device disconnect\n");
    prog_quit(0);
}

void Setup_comm_service(){
    
    /*
     Service: CusService
     
     characteristic: Terminal (rw)
     Execute the shell command with pipe, specific implementation in comm_Terminal.c
     Can not support interactive commands, each time execution is running in new subprocess
     
     characteristic: File Transfer(rw)
     Send or receive files via GATT characteristic, specific implementation in comm_file_transfer.c
     
     More Custom ...
     */
    
    /*
     handle 1: uuid:0x2800 (Primary Service Declaration) value:0x1111
     handle 2: uuid:0x2803 (Characteristic Declaration)  value:0x2222
     handle 3: uuid:0x2222 (coBlue Terminal)
     handle 4: uuid:0x2803 (Characteristic Declaration)  value:0x3333
     handle 5: uuid:0x3333 (coBlue File Transfer)
     */
    
    bt_uuid_t uuid;
    struct gatt_db_attribute *service,*att1,*att2;
    
    bt_uuid16_create(&uuid,0x1111);
    service = gatt_db_add_service(coBlue_gattdb,&uuid,true,5);
    
    bt_uuid16_create(&uuid,COBLUE_TERMINAL_UUID);
    att1 = gatt_db_service_add_characteristic(service,&uuid,BT_ATT_PERM_READ|BT_ATT_PERM_WRITE,BT_GATT_CHRC_PROP_READ|BT_GATT_CHRC_PROP_WRITE,comm_Terminal_xREAD,comm_Terminal_xWRITE,NULL);
    
    bt_uuid16_create(&uuid,COBLUE_FILE_TRANSFER_UUID);
    att2 = gatt_db_service_add_characteristic(service,&uuid,BT_ATT_PERM_READ|BT_ATT_PERM_WRITE,BT_GATT_CHRC_PROP_READ|BT_GATT_CHRC_PROP_WRITE,comm_file_transfer_xREAD,comm_file_transfer_xWRITE,NULL);
    
    gatt_db_service_set_active(service, true);
}

static void ble_destroy_server()
{
    bt_gatt_server_unref(coBlue_gattserver);
    gatt_db_unref(coBlue_gattdb);
    bt_att_unref(coBlue_att);
    close(connsk);
}

void prog_quit(int sig){
    printf("\n");
    ble_set_advertise_enable(0);
    ble_destroy_server();
    if(sig)
        printf("quit from signal:%d\n",sig);
    printf("---%s Exit---\n",COBLUE_PROG_NAME);
    exit(1);
}

#define get_pid_of_daemon get_pid_of_coBlued
pid_t get_pid_of_daemon(){
    char tmpstr[1024];
    char result[COBLUE_INTERNAL_STDSIZE];
    const char *progname = COBLUE_PROG_NAME;
    sprintf(tmpstr,"ps -c -e |awk '$NF==\"%s\"{print $1}'|xargs echo",progname);
    simple_pipe_cmd(tmpstr,result);
    if(strlen(result)){
        char *p = strtok(result," ");
        while(p)
        {
            uint32_t pid = (uint32_t)atol(p);
            if(pid!=getpid()){
                return pid;
            }
            p=strtok(NULL," ");
        }
        
    }
    return -1;
}

int asdaemon_need_firsttime_setup(){
    char tmpstr[1024];
    char result[COBLUE_INTERNAL_STDSIZE];
    const char *progname = COBLUE_PROG_NAME;
    sprintf(tmpstr,"ps -c -e |awk '$NF==\"%s\"{print $1}'|xargs echo",progname);
    simple_pipe_cmd(tmpstr,result);
    if(strlen(result)){
        char *p = strtok(result," ");
        while(p)
        {
            uint32_t pid = (uint32_t)atol(p);
            if(pid!=getpid()){
                return 0;
            }
            p=strtok(NULL," ");
        }
        
    }
    return -1;
}

void asdaemon_getmainprogpath(char *buf){
    char tmpstr[1024];
    char result[COBLUE_INTERNAL_STDSIZE];
    const char *progname = COBLUE_PROG_NAME;
    sprintf(tmpstr,"ps -c -e |awk '$NF==\"%s\"{print $1}'|xargs echo",progname);
    simple_pipe_cmd(tmpstr,result);
    if(strlen(result)){
        char *p = strtok(result," ");
        while(p)
        {
            uint32_t pid = (uint32_t)atol(p);
            if(pid>0&&pid!=getpid()){
                //sprintf(tmpstr,"ps -e %d|grep %d|awk '{print $NF}'",pid,pid);
                sprintf(tmpstr,"ps -p %d -o command | tail -n 1",pid);
                simple_pipe_cmd(tmpstr,result);
                strcpy(buf,result);
            }
            p=strtok(NULL," ");
        }
    }
}

void asdaemon_killothermainprogs()
{
    char tmpstr[1024];
    char result[COBLUE_INTERNAL_STDSIZE];
    const char *progname = COBLUE_PROG_NAME;
    sprintf(tmpstr,"ps -c -e |awk '$NF==\"%s\"{print $1}'|xargs echo",progname);
    simple_pipe_cmd(tmpstr,result);
    if(strlen(result)){
        char *p = strtok(result," ");
        while(p)
        {
            uint32_t pid = (uint32_t)atol(p);
            if(pid>0&&pid!=getpid()){
                kill(pid,SIGINT);
            }
            p=strtok(NULL," ");
        }
    }
}

void asdaemon_killallmainprogs(int sig)
{
    char tmpstr[1024];
    char result[COBLUE_INTERNAL_STDSIZE];
    const char *progname = COBLUE_PROG_NAME;
    sprintf(tmpstr,"ps -c -e |awk '$NF==\"%s\"{print $1}'|xargs echo",progname);
    simple_pipe_cmd(tmpstr,result);
    if(strlen(result)){
        char *p = strtok(result," ");
        while(p)
        {
            uint32_t pid = (uint32_t)atol(p);
            if(pid>0&&pid!=getpid()){
                kill(pid,SIGINT);
            }
            p=strtok(NULL," ");
        }
    }
    exit(1);
}

void asdaemon_run_once(){
    /*Here is the code only get execute when daemon first time setup*/
    ble_mgmt_power_on();
    ble_mgmt_set_devicename(COBLUE_ADV_DEVICE_NAME);
    
    pid_t coBluedpid = get_pid_of_coBlued();
    if(coBluedpid<getpid()){
        kill(coBluedpid,SIGKILL);
    }
    
    sleep(5);
}

void asdaemon_run_when_coBlue_reboot(){
    /*Here is the code get execute when daemon reboot coBlue*/
    ble_mgmt_set_devicename(COBLUE_ADV_DEVICE_NAME);
}

void asdaemon_run(){
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("coblued subprocess running\n");
    
    signal(SIGINT,asdaemon_killallmainprogs);
    signal(SIGTERM,asdaemon_killallmainprogs);
    signal(SIGKILL,asdaemon_killallmainprogs);
    
    char tmpstr[1024];
    char result[COBLUE_INTERNAL_STDSIZE];
    const char *progname = COBLUE_PROG_NAME;
    char execpath[COBLUE_INTERNAL_STDSIZE];
    asdaemon_getmainprogpath(execpath);
    sprintf(tmpstr,"ps -c -e |awk '$NF==\"%s\"{print $1}'|xargs echo",progname);
    
    asdaemon_run_once();
    while(1){
        sleep(COBLUE_REBOOT_INTERVAL_SEC);
        simple_pipe_cmd(tmpstr,result);
        
        int need_reboot_mainprog = 1;
        if(strlen(result)){
            char *p = strtok(result," ");
            while(p)
            {
                uint32_t pid = (uint32_t)atol(p);
                if(pid>0&&pid!=getpid()){
                    need_reboot_mainprog = 0;
                }
                p=strtok(NULL," ");
            }
            if(need_reboot_mainprog){
                /* Here is what child proc do for dad proc*/
                /* Default is reboot dad proc,But you can change/add op when dad proc is quit and*/
                asdaemon_killothermainprogs();
                asdaemon_run_when_coBlue_reboot();
                sprintf(tmpstr,"exec %s",execpath);
                system(tmpstr);
            }
        }
    }
}

void create_config_file_if_not_exist(char *user_path){
    struct stat fstat;
    char tmpstr[strlen(user_path)+128];
    sprintf(tmpstr,"%s/.coBlued_config",user_path);
    
    if (stat(tmpstr,&fstat) < 0 )
    {
        sprintf(tmpstr,"mkdir %s/.coBlued_config",user_path);
        system(tmpstr);
        return;
    }
    if(!fstat.st_mode&__S_IFDIR){
        printf("%s/.coBlued_config must be a directory",user_path);exit(1);
    }
    
}

void usage(){
    char space = ' ';
    printf("coBlue -- Use terminal and file transfer services thought Bluetooth Low Enegy, wrote by cocoahuke\n\n");
    printf("Options:\n");
    printf("%-2c-debug\t\tEnable output of debug\n",space);
    printf("%-2c-nodaemon\t\tDirectly running without fork subprocess, which means does not automatically restart after disconnecting\n\n",space);
    printf("%-2c-secur [arg]\tSpecifies the security level on the ATT protocol, default is medium. im not sure just this changes could make high-level security work, other options are:\n",space);
    printf("\t\t\tlow%-5c(No encryption and no authentication)\n",space);
    printf("\t\t\tmedium%-2c(Encryption and no authentication)\n",space);
    printf("\t\t\thigh%-4c(Encryption and authentication)\n",space);
    printf("\t\t\tfips%-4c(Authenticated Secure Connections)\n\n",space);
    printf("%-2c-wowner [arg]\tSpecifies the owner of the file that create by writting op\n",space);
    printf("%-2c-dmacfltr\t\tDisable Mac Address Filtering, default is enable. Enable will allow the device Mac address record from ~/.coblued_config/coblue_filter make connection without verify\n",space);
    printf("\t\t\tcoblue_filter file content format example:\n");
    printf("\t\t\t[AB:CC:12:34:9F:31][XX:XX:XX:XX:XX:XX]\n");
    printf("\t\t\tDisable means that all access devices need to be verified, same if file does not exist\n\n");
    printf("%-2c-filterw\t\tModify the Mac Address Filtering configuration using the nano tool\n",space);
    printf("%-2c-fltrmax [arg]\tThe maximum number of filter addresses, default is ten\n\n",space);
    printf("%-2c-dverify\t\tDisable the authentication, default is enable. when Enable all access devices need to send a string of uuid within a limited time and match with specified uuid, match, disconnect right away if doesn't match. specified uuid save as\n",space);
    printf("\t\t\tspecified uuid saves in a file for location ~/.coblued_config/coBlued_verify\n");
    printf("\t\t\tcoblue_filter file content format example:\n");
    printf("\t\t\t561382C9-4D0C-4E1F-A8E2-50AAACFA31D1\n");
    printf("\t\t\tDisable means no verify for all access device\n\n");
    printf("%-2c-verifyw\t\tModify the authentication configuration using the nano tool\n",space);
    printf("%-2c-verifyti [arg]\tSpecified the countdown(second) for complete verify before disconnecting, default is three seconds\n\n",space);
    printf("%-2c-name [arg]\tSpecifies the device name when advertising, default is orange. Only work with daemon\n\n",space);
    printf("%-2c-configw\t\tadd the boot parameters that every time using with boot. if you want to change the advertise name, this way\n",space);
}

void spec_args(int argc,const char *argv[]){
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-debug")){
            COBLUE_DEBUG_OUTPUT = 1;
        }
        if(!strcmp(argv[i],"-nodaemon")){
            COBLUE_RUN_WITHOUT_DAEMON = 1;
        }
        if(!strcmp(argv[i],"-secur")){
            char *c = (i=i+1)>=argc?"":(char*)argv[i];
            if(!strcmp(c,"low"))
                COBLUE_ATT_SECURITY = BT_SECURITY_LOW;
            else if(!strcmp(c,"medium"))
                COBLUE_ATT_SECURITY = BT_SECURITY_MEDIUM;
            else if(!strcmp(c,"high"))
                COBLUE_ATT_SECURITY = BT_SECURITY_HIGH;
            else if(!strcmp(c,"fips"))
                COBLUE_ATT_SECURITY = BT_SECURITY_FIPS;
            else
                COBLUE_ATT_SECURITY = BT_SECURITY_MEDIUM;
        }
        if(!strcmp(argv[i],"-wowner")){
            char *c = (i=i+1)>=argc?NULL:(char*)argv[i];
            if(c){
                struct passwd *spe_uid = getpwnam(c);
                if(spe_uid->pw_uid)
                    COBLUE_WRITE_FILE_OWNER = spe_uid->pw_uid;
            }
        }
        if(!strcmp(argv[i],"-dmacfltr")){
            COBLUE_ENABLE_MAC_FILTER=0;
        }
        if(!strcmp(argv[i],"-filterw")){
            struct passwd *rootusr = getpwuid(getuid());
            if(!rootusr){
                printf("getpwuid failed\n");exit(1);
            }
            char tmpchar[strlen(rootusr->pw_dir)+128];
            sprintf(tmpchar,"%s/.coBlued_config/coBlued_filter",rootusr->pw_dir);
            char *shargv[]={"/usr/bin/nano",tmpchar,NULL};
            execv("/usr/bin/nano",shargv);
        }
        if(!strcmp(argv[i],"-fltrmax")){
            char *c = (i=i+1)>=argc?NULL:(char*)argv[i];
            if(c)
                COBLUE_MAXIMUM_FILTER_ENTRY = (uint32_t)strtoull(c,0,10);
        }
        if(!strcmp(argv[i],"-dverify")){
            COBLUE_ENABLE_VERIFICATION = 0;
        }
        if(!strcmp(argv[i],"-verifyw")){
            struct passwd *rootusr = getpwuid(getuid());
            if(!rootusr){
                printf("getpwuid failed\n");exit(1);
            }
            char tmpchar[strlen(rootusr->pw_dir)+128];
            sprintf(tmpchar,"%s/.coBlued_config/coBlued_verify",rootusr->pw_dir);
            char *shargv[]={"/usr/bin/nano",tmpchar,NULL};
            execv("/usr/bin/nano",shargv);
        }
        if(!strcmp(argv[i],"-verifyti")){
            char *c = (i=i+1)>=argc?NULL:(char*)argv[i];
            if(c)
                COBLUE_VERIFY_TIME_LIMIT = (uint32_t)strtoull(c,0,10);
        }
        if(!strcmp(argv[i],"-name")){
            char *c = (i=i+1)>=argc?NULL:(char*)argv[i];
            if(c)
                strcpy(COBLUE_ADV_DEVICE_NAME,c);
        }
        if(!strcmp(argv[i],"-configw")){
            char *shargv[]={"/usr/bin/nano","/etc/init.d/coBlued",NULL};
            execv("/usr/bin/nano",shargv);
        }
    }
}

/*
 Sometimes the Bluetooth advertisement will be a problem, unconnectable and unknown name. however, both the hcitool and btmgmt checks it is connectable.
 Simple solution is reboot bluetooth service by systemctl,then turn on power and it is back to normal
 */
int main(int argc, const char * argv[]){
    
    if(!strcmp(argv[argc-1],"-h")){
        usage();exit(1);
    }
    
    if(getuid()!=0){
        printf("Require root to execute this program\n");exit(1);
    }
    
    strcpy(COBLUE_ADV_DEVICE_NAME,"orange");
    if(argc>1)
        spec_args(argc,argv);
    
    struct passwd *rootusr = NULL;
    
    rootusr = getpwuid(getuid());
    if(!rootusr){
        printf("getpwuid failed\n");exit(1);
    }
    
    create_config_file_if_not_exist(rootusr->pw_dir);
    
    while(1){
        char result[COBLUE_INTERNAL_STDSIZE];
        simple_pipe_cmd("ps -e | grep bluetoothd",result);
        if(strlen(result)>5){
            break;
        }
    }
    
    if(!COBLUE_RUN_WITHOUT_DAEMON){
        if(asdaemon_need_firsttime_setup()){
            if(!fork()){
                asdaemon_run();
            }
            else{
                while(1){};
            }
        }
    }
    
    signal(SIGINT,prog_quit);
    signal(SIGKILL,prog_quit);
    signal(SIGTERM,prog_quit);
    
    int max_filter_amount = COBLUE_MAXIMUM_FILTER_ENTRY; //maximum number of filter array
    size_t filter_buf_len = (macaddr_len+2)*10+1;
    char filter_buf[filter_buf_len];
    char tmpchar[strlen(rootusr->pw_dir)+128];
    
    filter_arr = malloc(COBLUE_MAXIMUM_FILTER_ENTRY*(macaddr_len+1));
    filter_arr_ptr = malloc((COBLUE_MAXIMUM_FILTER_ENTRY+1)*sizeof(void*));
    
    bzero(filter_buf,filter_buf_len);
    bzero(filter_arr,max_filter_amount*(macaddr_len+1));
    bzero(filter_arr_ptr,(max_filter_amount+1)*sizeof(void*));
    
    sprintf(tmpchar,"%s/.coBlued_config/coBlued_filter",rootusr->pw_dir);
    FILE *filter_fp = fopen(tmpchar,"ro");
    if(filter_fp){
        
        fseek(filter_fp,0,SEEK_END);
        if((ftell(filter_fp))>filter_buf_len){
            printf("filter file is too large,must need smaller than 1KB\n");exit(1);
        }
        fseek(filter_fp,0,SEEK_SET);
        bzero(filter_buf,filter_buf_len);
        fread(filter_buf,1,filter_buf_len,filter_fp);
        for(int i=0;filter_buf[i];i++){
            if(isxdigit(filter_buf[i])){
                filter_buf[i] = toupper(filter_buf[i]);
            }
            if(!isprint(filter_buf[i])){
                filter_buf[i] = '\0';
            }
        }
        fclose(filter_fp);
        
        for(char*p=strtok(filter_buf,"[");p;){
            if((strlen(p)-1)==macaddr_len){
                int cur_amo = 0;
                while(filter_arr_ptr[cur_amo]){cur_amo++;};
                char *pos_in_arr = filter_arr+(cur_amo*(macaddr_len+1));
                sscanf(p,"%[^]]",pos_in_arr);
                filter_arr_ptr[cur_amo] = pos_in_arr;
            }
            p=strtok(NULL,"[");
        }
        
        if(COBLUE_DEBUG_OUTPUT){
            for(int cur_amo=0;filter_arr_ptr[cur_amo];cur_amo++){
                printf("allow conn UUID: %s\n",filter_arr_ptr[cur_amo]);
            }
        }
        
    }
    
    sprintf(tmpchar,"%s/.coBlued_config/coBlued_verify",rootusr->pw_dir);
    bzero(uuid_for_verify,uuid_len+1);
    FILE *verify_fp = fopen(tmpchar,"ro");
    if(verify_fp){
        if(fread(uuid_for_verify,1,uuid_len,verify_fp)!=uuid_len){
            bzero(uuid_for_verify,uuid_len+1);
        }else{
            printf("UUID for veridy: %s\n",uuid_for_verify);
        }
        fclose(verify_fp);
    }
    
    
    
    int rt;
    sigset_t mask;
    
    mainloop_init();
    
    printf("---%s Start---\n\n",COBLUE_PROG_NAME);
    
    pthread_t cc;
    pthread_create(&cc,NULL,keep_adversting_if_unconnect,NULL);
    
    connsk = ble_att_listen_accept(BDADDR_ANY);
    if (connsk < 0) {
        connsk = 0;
        prog_quit(0);
    }
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("Debug: start initialization ATT layer...\n");
    
    if(!(coBlue_att=bt_att_new(connsk,false))) EXC_PRINT("");
    if(!bt_att_set_close_on_unref(coBlue_att,true)) EXC_PRINT("");
    if(!bt_att_set_security(coBlue_att,COBLUE_ATT_SECURITY)) EXC_PRINT("");
    if(!bt_att_register_disconnect(coBlue_att,notifi_disconnect,NULL,NULL)) EXC_PRINT("");
    if(!(coBlue_gattdb=gatt_db_new())) EXC_PRINT("");
    if(!(coBlue_gattserver=bt_gatt_server_new(coBlue_gattdb,coBlue_att,COBLUE_BLE_GATT_MTU))) EXC_PRINT("");
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("Debug: end initialization ATT layer\n");
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("Debug: start setup communication service...\n");
    
    Setup_comm_service();
    
    if(COBLUE_DEBUG_OUTPUT)
        printf("Debug: end setup communication service\n");
    
    mainloop_run();
end:
    prog_quit(0);
    //child proc will reboot
    return 0;
}
