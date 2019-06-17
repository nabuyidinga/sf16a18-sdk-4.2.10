#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <signal.h>
#include <glob.h>
#include <libubox/blobmsg_json.h>
#include <libubox/avl-cmp.h>
#include <libubus.h>
#include <uci.h>

#include <rpcd/plugin.h>

static const struct rpc_daemon_ops *ops;
static struct blob_buf buf;

enum {
        RPC_P_D_DATA,
        RPC_P_D_COUNT,
        __RPC_P_D_MAX,

        RPC_CHECK_SFI_NUM = 0,
        RPC_CHECK_INIT = 1,
        RPC_CHECK_MAX = 2,

        RPC_UCI_CONFIG = 0,
        RPC_UCI_SECTION = 1,
        RPC_UCI_OPT = 2,
        RPC_UCI_VALUE = 3,
        RPC_UCI_MAX = 4,

        RPC_UCI_SFI_NUM = 0,
        RPC_UCI_SSID = 1,
        RPC_UCI_ENCRYPT = 2,
        RPC_UCI_KEY = 3,
        RPC_UCI_BSSID = 4,
        RPC_UCI_DIS_SCAN_OFF = 5,
        RPC_UCI_NETWORK_INTERFACE = 6,
        RPC_UCI_W_MAX = 7,

        RPC_LAST_FREQ = 0,
        RPC_LAST_BAND = 1,
        RPC_LAST_CHANNEL = 2,
        RPC_LAST_24G_SSID = 3,
        RPC_LAST_24G_ENCRYPT = 4,
        RPC_LAST_24G_KEY = 5,
        RPC_LAST_5G_SSID = 6,
        RPC_LAST_5G_ENCRYPT = 7,
        RPC_LAST_5G_KEY = 8,
        RPC_LAST_MAX = 9,

        RPC_AP_FREQ = 0,
        RPC_AP_24G_SSID = 1,
        RPC_AP_24G_ENCRYPT = 2,
        RPC_AP_24G_KEY = 3,
        RPC_AP_5G_SSID = 4,
        RPC_AP_5G_ENCRYPT = 5,
        RPC_AP_5G_KEY = 6,
        RPC_AP_MAX = 7,
};

static const struct blobmsg_policy rpc_ping_data_policy[__RPC_P_D_MAX] = {
        [RPC_P_D_DATA]   = { .name = "data",  .type = BLOBMSG_TYPE_STRING },
        [RPC_P_D_COUNT]   = { .name = "count",  .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy check_wds_data[RPC_CHECK_MAX] = {
        [RPC_CHECK_SFI_NUM]   = { .name = "sfi_num",  .type = BLOBMSG_TYPE_STRING },
        [RPC_CHECK_INIT]   = { .name = "init",  .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy set_uci_data[RPC_UCI_MAX] = {
        [RPC_UCI_CONFIG]   = { .name = "config",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_SECTION]   = { .name = "section",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_OPT]   = { .name = "option",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_VALUE]   = { .name = "value",  .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy set_wireless_data[RPC_UCI_W_MAX] = {
        [RPC_UCI_SFI_NUM]   = { .name = "sfi_num",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_SSID]   = { .name = "ssid",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_ENCRYPT]   = { .name = "encryption",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_KEY]   = { .name = "key",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_BSSID]   = { .name = "bssid",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_DIS_SCAN_OFF]   = { .name = "dis_scan_off",  .type = BLOBMSG_TYPE_STRING },
        [RPC_UCI_NETWORK_INTERFACE]   = { .name = "network_interface",  .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy set_wireless_disable_data[1] = {
        [RPC_UCI_SFI_NUM]   = { .name = "sfi_num",  .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy set_config_last_data[RPC_LAST_MAX] = {
        [RPC_LAST_FREQ]   = { .name = "freq",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_BAND]   = { .name = "band",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_CHANNEL]   = { .name = "channel",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_24G_SSID]   = { .name = "ssid_24g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_24G_ENCRYPT]   = { .name = "encryption_24g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_24G_KEY]   = { .name = "key_24g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_5G_SSID ]   = { .name = "ssid_5g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_5G_ENCRYPT ]   = { .name = "encryption_5g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_LAST_5G_KEY ]   = { .name = "key_5g",  .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy ap_mode_set_data[RPC_AP_MAX] = {
        [RPC_AP_FREQ]   = { .name = "freq",  .type = BLOBMSG_TYPE_STRING },
        [RPC_AP_24G_SSID]   = { .name = "ssid_24g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_AP_24G_ENCRYPT]   = { .name = "encryption_24g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_AP_24G_KEY]   = { .name = "key_24g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_AP_5G_SSID ]   = { .name = "ssid_5g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_AP_5G_ENCRYPT ]   = { .name = "encryption_5g",  .type = BLOBMSG_TYPE_STRING },
        [RPC_AP_5G_KEY ]   = { .name = "key_5g",  .type = BLOBMSG_TYPE_STRING },
};

static int
rpc_luci2_network_ping(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
        char *arg;
        char *count = "5";

        struct blob_attr *tb_ping[__RPC_P_D_MAX];
        blobmsg_parse(rpc_ping_data_policy, __RPC_P_D_MAX, tb_ping,
                      blob_data(msg), blob_len(msg));

        if (!tb_ping[RPC_P_D_DATA] && !tb_ping[RPC_P_D_COUNT])
                return UBUS_STATUS_INVALID_ARGUMENT;

        arg = blobmsg_get_string(tb_ping[RPC_P_D_DATA]);
        count = blobmsg_get_string(tb_ping[RPC_P_D_COUNT]);

        const char *cmds[7] = { "ping", "-c", count, "-W", "1", arg, NULL };

        return ops->exec(cmds, NULL, NULL, NULL, NULL, NULL, ctx, req);
}

static int
set_uci(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
        char *config;
        char *section;
        char *option;
        char *value;

        struct blob_attr *tb_ping[RPC_UCI_MAX];
        blobmsg_parse(set_uci_data, RPC_UCI_MAX, tb_ping,
                      blob_data(msg), blob_len(msg));

        if (!tb_ping[RPC_UCI_CONFIG] && !tb_ping[RPC_UCI_SECTION] && !tb_ping[RPC_UCI_OPT] && !tb_ping[RPC_UCI_VALUE])
                return UBUS_STATUS_INVALID_ARGUMENT;

        config = blobmsg_get_string(tb_ping[RPC_UCI_CONFIG]);
        section = blobmsg_get_string(tb_ping[RPC_UCI_SECTION]);
        option = blobmsg_get_string(tb_ping[RPC_UCI_OPT]);
        value = blobmsg_get_string(tb_ping[RPC_UCI_VALUE]);

       printf("=============");
        struct uci_context *set_ctx = uci_alloc_context();
        struct uci_package *p = NULL;
        int ret = -1;
        uci_set_confdir(set_ctx, "/etc/config");

        if(uci_load(set_ctx, config, &p) == UCI_OK)
        {
            struct uci_section *router = uci_lookup_section(set_ctx, p, section);
            //lookup values
            if(router != NULL){
                struct uci_ptr ptr = { .p = p, .s = router};
                ptr.o      = NULL;
                ptr.option = option;
                ptr.value  = value;
                uci_set(set_ctx, &ptr);
                uci_save(set_ctx,p);
                uci_commit(set_ctx,&p,false);
                ret = 0;
            }

            uci_unload(set_ctx,p);
        }
        uci_free_context(set_ctx);

        return ret;
}
//set disabled = 0
static int
set_wireless_disable(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
    char *sfi_num;

    struct uci_context *get_ctx = uci_alloc_context();
    struct uci_context *set_ctx = uci_alloc_context();
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    struct uci_section *s;
    struct uci_ptr ptr;
    const char *value = NULL;

    struct blob_attr *tb_ping[1];
    blobmsg_parse(set_wireless_disable_data, 1, tb_ping,
                  blob_data(msg), blob_len(msg));

    if (!tb_ping[RPC_UCI_SFI_NUM])
        return UBUS_STATUS_INVALID_ARGUMENT;

    sfi_num= blobmsg_get_string(tb_ping[RPC_UCI_SFI_NUM]);

    memset(&ptr , 0, sizeof(struct uci_ptr));
    uci_set_confdir(get_ctx, "/etc/config");
    uci_set_confdir(set_ctx, "/etc/config");
    if(UCI_OK == uci_load(get_ctx, "wireless", &pkg)){
        uci_foreach_element(&pkg->sections, e)
        {
            s = uci_to_section(e);
            if(!strcmp(s->type, "wifi-iface"))
            {
                if (NULL != (value = uci_lookup_option_string(get_ctx, s, "ifname")))
                {
                    if (!strcmp(value, sfi_num)){
                        ptr.package = "wireless";
                        ptr.section = s->e.name;

                        ptr.option = "key";
                        ptr.value = "errorkey3131212";
                        uci_set(set_ctx, &ptr);

                        ptr.option = "ssid";
                        ptr.value = "SiWiFi-test";
                        uci_set(set_ctx, &ptr);

                        ptr.option = "disabled";
                        ptr.value = "0";
                        uci_set(set_ctx, &ptr);

                        uci_commit(set_ctx, &ptr.p, false);
                        uci_unload(set_ctx, ptr.p);
                    }
                }
            }
        }
        uci_unload(get_ctx, pkg);
    }

    uci_free_context(get_ctx);
    uci_free_context(set_ctx);
    system("uci set network.stabridge.disabled='1'");
    system("uci commit");
    return 0;
}

static int
set_wireless(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
        char *sfi_num;
        char *ssid;
        char *encryption;
        char *key;
        char *bssid;
        char *dis_scan_off;
        char *network_interface;

        struct uci_context *get_ctx = uci_alloc_context();
        struct uci_context *set_ctx = uci_alloc_context();
        struct uci_package *pkg = NULL;
        struct uci_element *e;
        struct uci_section *s;
        struct uci_ptr ptr;
        const char *value = NULL;

        struct blob_attr *tb_ping[RPC_UCI_W_MAX];
        blobmsg_parse(set_wireless_data, RPC_UCI_W_MAX, tb_ping,
                      blob_data(msg), blob_len(msg));

        if (!tb_ping[RPC_UCI_SFI_NUM])
                return UBUS_STATUS_INVALID_ARGUMENT;

        sfi_num= blobmsg_get_string(tb_ping[RPC_UCI_SFI_NUM]);
        ssid = blobmsg_get_string(tb_ping[RPC_UCI_SSID]);
        encryption = blobmsg_get_string(tb_ping[RPC_UCI_ENCRYPT]);
        key = blobmsg_get_string(tb_ping[RPC_UCI_KEY]);
        bssid = blobmsg_get_string(tb_ping[RPC_UCI_BSSID]);
        dis_scan_off = blobmsg_get_string(tb_ping[RPC_UCI_DIS_SCAN_OFF]);
        network_interface = blobmsg_get_string(tb_ping[RPC_UCI_NETWORK_INTERFACE]);

        memset(&ptr , 0, sizeof(struct uci_ptr));
        uci_set_confdir(get_ctx, "/etc/config");
        uci_set_confdir(set_ctx, "/etc/config");
        if(UCI_OK == uci_load(get_ctx, "wireless", &pkg)){
            uci_foreach_element(&pkg->sections, e)
            {
                s = uci_to_section(e);
                if(!strcmp(s->type, "wifi-iface"))
                {
                    if (NULL != (value = uci_lookup_option_string(get_ctx, s, "ifname")))
                    {
                        if (!strcmp(value, sfi_num)){
                            ptr.package = "wireless";
                            ptr.section = s->e.name;
                            if(ssid != NULL)
                            {
                                ptr.option = "ssid";
                                ptr.value = ssid;
                                uci_set(set_ctx, &ptr);
                            }

                            if(encryption != NULL)
                            {
                                ptr.option = "encryption";
                                ptr.value = encryption;
                                uci_set(set_ctx, &ptr);
                            }

                            if(key != NULL)
                            {
                                ptr.option = "key";
                                ptr.value = key;
                                uci_set(set_ctx, &ptr);
                            }

                            if(bssid != NULL)
                            {
                                ptr.option = "bssid";
                                ptr.value = bssid;
                                uci_set(set_ctx, &ptr);
                            }else{
                                ptr.option = "bssid";
                                ptr.value = "";
                                uci_set(set_ctx, &ptr);
                            }

                            if(dis_scan_off != NULL)
                            {
                                ptr.option = "disabled";
                                ptr.value = dis_scan_off;
                                uci_set(set_ctx, &ptr);
                            }

                            if(network_interface != NULL)
                            {
                                ptr.option = "network";
                                ptr.value = network_interface;
                                uci_set(set_ctx, &ptr);
                            }

                            uci_commit(set_ctx, &ptr.p, false);
                            uci_unload(set_ctx, ptr.p);
                        }
                    }
                }
            }
            uci_unload(get_ctx, pkg);
        }

        uci_free_context(get_ctx);
        uci_free_context(set_ctx);

        return 0;
}

//for set ap wifi info
static int set_wireless_local(char* sfi_num, char* ssid, char* encryption, char* key){
    struct uci_context *get_ctx = uci_alloc_context();
    struct uci_context *set_ctx = uci_alloc_context();
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    struct uci_section *s;
    struct uci_ptr ptr;
    const char *value = NULL;

    memset(&ptr , 0, sizeof(struct uci_ptr));
    uci_set_confdir(get_ctx, "/etc/config");
    uci_set_confdir(set_ctx, "/etc/config");
    if(UCI_OK == uci_load(get_ctx, "wireless", &pkg)){
        uci_foreach_element(&pkg->sections, e)
        {
            s = uci_to_section(e);
            if(!strcmp(s->type, "wifi-iface"))
            {
                if (NULL != (value = uci_lookup_option_string(get_ctx, s, "ifname")))
                {
                    if (!strcmp(value, sfi_num)){
                        ptr.package = "wireless";
                        ptr.section = s->e.name;
                        if(ssid != NULL)
                        {
                            ptr.option = "ssid";
                            ptr.value = ssid;
                            uci_set(set_ctx, &ptr);
                        }

                        if(encryption != NULL)
                        {
                            ptr.option = "encryption";
                            ptr.value = encryption;
                            uci_set(set_ctx, &ptr);
                        }

                        if(key != NULL)
                        {
                            ptr.option = "key";
                            ptr.value = key;
                            uci_set(set_ctx, &ptr);
                        }

                        uci_commit(set_ctx, &ptr.p, false);
                        uci_unload(set_ctx, ptr.p);
                    }
                }
            }
        }
        uci_unload(get_ctx, pkg);
    }

    uci_free_context(get_ctx);
    uci_free_context(set_ctx);

    return 0;
}

static int
check_wds(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
        char *sfi_num;
        char *init;
        char passwd_buf[2] = "0";
        char cmd[80] = "ifconfig ";
        char cmd_buf[20] = "";
        char path[30] = "/tmp/wds_reason_code";
        int fd, len, i;
        FILE *pp;

        struct blob_attr *tb_ping[RPC_CHECK_MAX];
        blobmsg_parse(check_wds_data,RPC_CHECK_MAX, tb_ping,
                      blob_data(msg), blob_len(msg));

        if (!tb_ping[RPC_CHECK_SFI_NUM])
            return UBUS_STATUS_INVALID_ARGUMENT;

        sfi_num = blobmsg_get_string(tb_ping[RPC_CHECK_SFI_NUM]);
        init = blobmsg_get_string(tb_ping[RPC_CHECK_INIT]);

//check password initial
        if ((*init) == '1'){
            fd = open(path,O_WRONLY|O_CREAT|O_TRUNC);
            if (fd < 0){
                return UBUS_STATUS_INVALID_ARGUMENT;
            }
            len = write(fd, passwd_buf, 1);
            if (len < 0){
                return UBUS_STATUS_NO_DATA;
            }
            close(fd);
            return 0;
        }
        //check password
        fd = open(path,O_RDONLY);
        if (fd < 0){
            return UBUS_STATUS_INVALID_ARGUMENT;
        }
        len = read(fd, passwd_buf, 1);
        if (len < 0){
            return UBUS_STATUS_NO_DATA;
        }
        if(strcmp(passwd_buf, "1") == 0){
            return -1;
        }
        if(strcmp(passwd_buf, "2") == 0){
            return -2;
        }
        close(fd);

//check wds success
        strcat(cmd,sfi_num);
        strcat(cmd,"|grep inet|awk -F\" \" \'{print $2}\'|awk -F\":\" \'{print $2}\'");
        if( (pp = popen(cmd, "r")) == NULL )
        {
            printf("popen() error!/n");
            return -3;
        }
        fgets(cmd_buf, sizeof cmd_buf, pp);
        pclose(pp);
        for(i=0 ;i < 20; i++)
            if(cmd_buf[i] == '\n')
            {
                cmd_buf[i] = '\0';
                break;
            }

        blob_buf_init(&buf, 0);
        blobmsg_add_string(&buf, "sfi_ip", cmd_buf);

        ubus_send_reply(ctx, req, buf.head);
        return 0;
}

static int
rpc_luci2_print_lepton(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
        char conf[10] = "lepton";
        blob_buf_init(&buf, 0);
        blobmsg_add_string(&buf, "name", conf);

        ubus_send_reply(ctx, req, buf.head);
        return 0;
}

static int
reset_wds(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
        struct uci_context *get_ctx = uci_alloc_context();
        struct uci_context *set_ctx = uci_alloc_context();
        struct uci_package *pkg = NULL;
        struct uci_element *e;
        struct uci_section *s;
        struct uci_ptr ptr;
        const char *value = NULL;

        memset(&ptr , 0, sizeof(struct uci_ptr));
        uci_set_confdir(get_ctx, "/etc/config");
        uci_set_confdir(set_ctx, "/etc/config");
        if(UCI_OK == uci_load(get_ctx, "wireless", &pkg)){
            uci_foreach_element(&pkg->sections, e)
            {
                s = uci_to_section(e);
                if(!strcmp(s->type, "wifi-iface"))
                {
                    if (NULL != (value = uci_lookup_option_string(get_ctx, s, "ifname")))
                    {
                        if (!strcmp(value, "sfi0") || !strcmp(value, "sfi1")){
                            ptr.package = "wireless";
                            ptr.section = s->e.name;

                            ptr.option = "ssid";
                            ptr.value = "SiWiFi-test";
                            uci_set(set_ctx, &ptr);

                            ptr.option = "encryption";
                            ptr.value = "psk2+ccmp";
                            uci_set(set_ctx, &ptr);

                            ptr.option = "key";
                            ptr.value = "errorkey3131212";
                            uci_set(set_ctx, &ptr);

                            ptr.option = "bssid";
                            ptr.value = "A8:5A:F3:00:30:58";
                            uci_set(set_ctx, &ptr);


                            ptr.option = "disabled";
                            ptr.value = "0";
                            uci_set(set_ctx, &ptr);
                        }
                    }
                }
            }
            uci_commit(set_ctx, &ptr.p, false);
            uci_unload(set_ctx, ptr.p);
            uci_unload(get_ctx, pkg);
        }

        uci_free_context(get_ctx);
        uci_free_context(set_ctx);

        return 0;
}

static int
wifi_reload(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
        system("wifi reload");
        return 0;
}

static int
dnsmasq_restart(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
        system("/etc/init.d/dnsmasq restart");
        return 0;
}

static int kick_sta_local(void);

static int set_netisolate(char *sfi_num){
    struct uci_context *get_ctx = uci_alloc_context();
    struct uci_context *set_ctx = uci_alloc_context();
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    struct uci_section *s;
    struct uci_ptr ptr;
    const char *value = NULL;

    memset(&ptr , 0, sizeof(struct uci_ptr));
    uci_set_confdir(get_ctx, "/etc/config");
    uci_set_confdir(set_ctx, "/etc/config");
    if(UCI_OK == uci_load(get_ctx, "wireless", &pkg)){
        uci_foreach_element(&pkg->sections, e)
        {
            s = uci_to_section(e);
            if(!strcmp(s->type, "wifi-iface"))
            {
                if (NULL != (value = uci_lookup_option_string(get_ctx, s, "ifname")))
                {
                    if (!strcmp(value, sfi_num)){
                        ptr.package = "wireless";
                        ptr.section = s->e.name;
                        ptr.option = "netisolate";
                        ptr.value = "0";
                        uci_set(set_ctx, &ptr);

                        uci_commit(set_ctx, &ptr.p, false);
                        uci_unload(set_ctx, ptr.p);
                    }
                }
            }
        }
        uci_unload(get_ctx, pkg);
    }

    uci_free_context(get_ctx);
    uci_free_context(set_ctx);

    return 0;
}

static int set_wireless_disable_local(char* sfi_num);

static int
set_config_last(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
    char *freq;
    char *band;
    char *channel;
    char *ssid_24g;
    char *encryption_24g;
    char *key_24g;
    char *ssid_5g;
    char *encryption_5g;
    char *key_5g;
    char cmd[50];

    struct blob_attr *tb_ping[RPC_LAST_MAX];
    blobmsg_parse(set_config_last_data, RPC_LAST_MAX, tb_ping,
                  blob_data(msg), blob_len(msg));

    if (!tb_ping[RPC_LAST_FREQ]||!tb_ping[RPC_LAST_BAND]||!tb_ping[RPC_LAST_CHANNEL])
        return UBUS_STATUS_INVALID_ARGUMENT;
    if (!tb_ping[RPC_LAST_24G_SSID]||!tb_ping[RPC_LAST_24G_ENCRYPT]||!tb_ping[RPC_LAST_24G_KEY])
        return UBUS_STATUS_INVALID_ARGUMENT;
    if (!tb_ping[RPC_LAST_5G_SSID]||!tb_ping[RPC_LAST_5G_ENCRYPT]||!tb_ping[RPC_LAST_5G_KEY])
        return UBUS_STATUS_INVALID_ARGUMENT;

    freq = blobmsg_get_string(tb_ping[RPC_LAST_FREQ]);
    band = blobmsg_get_string(tb_ping[RPC_LAST_BAND]);
    channel = blobmsg_get_string(tb_ping[RPC_LAST_CHANNEL]);
    ssid_24g = blobmsg_get_string(tb_ping[RPC_LAST_24G_SSID]);
    encryption_24g = blobmsg_get_string(tb_ping[RPC_LAST_24G_ENCRYPT]);
    key_24g = blobmsg_get_string(tb_ping[RPC_LAST_24G_KEY]);
    ssid_5g = blobmsg_get_string(tb_ping[RPC_LAST_5G_SSID]);
    encryption_5g = blobmsg_get_string(tb_ping[RPC_LAST_5G_ENCRYPT]);
    key_5g = blobmsg_get_string(tb_ping[RPC_LAST_5G_KEY]);

    set_wireless_local("wlan0" ,ssid_24g ,encryption_24g ,key_24g);
    set_wireless_local("wlan1" ,ssid_5g,encryption_5g,key_5g);
    //set freq
    memset(cmd,0,sizeof cmd);
    sprintf(cmd,"uci set basic_setting.freq.enable='");
    strcat(cmd,freq);
    strcat(cmd,"'");
    system(cmd);
    system("uci commit");
    //set stabridge
    memset(cmd,0,sizeof cmd);
    sprintf(cmd,"uci set network.stabridge.network='");
    if(!strcmp(band,"24g")){
        strcat(cmd, "lan wwan'");
        set_wireless_disable_local("sfi1");
    }else if(!strcmp(band,"5g")){
        strcat(cmd, "lan wwwan'");
        set_wireless_disable_local("sfi0");
    }else{
        blob_buf_init(&buf, 0);
        blobmsg_add_string(&buf, "status", "params error");
        ubus_send_reply(ctx, req, buf.head);
        return -1;
    }
    system(cmd);
    system("uci set network.stabridge.disabled='0'");
    system("uci commit");
    //dnsmasq stop
    system("uci set basic_setting.dnsmasq.down='1'");
    system("uci commit");
    system("/etc/init.d/dnsmasq restart");
    //bridge
    system("killall relayd");
    system("/etc/init.d/relayd restart");
    // this delay_kick will do:
        // sleep 2
        // uci set channel
        // wifi reload
        // kick all station for get ip .
    memset(cmd,0,sizeof cmd);
    sprintf(cmd,"delay_kick repeater ");
    strcat(cmd,channel);
    strcat(cmd,"&");
    system(cmd);

    return 0;
}

static int kick_sta_local(void)
{
    system("ubus call hostapd.wlan0 deauth");
    system("ubus call hostapd.wlan1 deauth");
    return 0;
}

//this interface for script, web not use
static int
net_restart(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
    char cmd[40] = "uci get network.stabridge.disabled";
    char cmd_buf[2];
    FILE *pp;
    memset(cmd_buf,0,sizeof cmd_buf);
    if( (pp = popen(cmd, "r")) == NULL )
    {
        printf("popen() error!/n");
        return -3;
    }
    fgets(cmd_buf, sizeof cmd_buf, pp);
    pclose(pp);
    if(cmd_buf[0] == '0'){
        system("killall relayd");
        system("/etc/init.d/relayd restart");
        system("wifi reload");
        kick_sta_local();
        system("ifconfig eth0 down; sleep 2;ifconfig eth0 up");
    }
    //log
    /*
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "bssid", cmd_buf);
    ubus_send_reply(ctx, req, buf.head);
    */
    return 0;
}

static int
kick_sta(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
    return kick_sta_local();
}

static int
get_firmware_version(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
    char cmd[30] = "cat /etc/openwrt_version";
    char cmd_buf[80];
    FILE *pp;
    memset(cmd_buf,0,sizeof cmd_buf);
    if( (pp = popen(cmd, "r")) == NULL )
    {
        printf("popen() error!/n");
        return -3;
    }
    fgets(cmd_buf, sizeof cmd_buf, pp);
    pclose(pp);
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "firmware_version", cmd_buf);
    ubus_send_reply(ctx, req, buf.head);
    return 0;
}
//set disabled = 1
static int set_wireless_disable_local(char* sfi_num){
    struct uci_context *get_ctx = uci_alloc_context();
    struct uci_context *set_ctx = uci_alloc_context();
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    struct uci_section *s;
    struct uci_ptr ptr;
    const char *value = NULL;

    memset(&ptr , 0, sizeof(struct uci_ptr));
    uci_set_confdir(get_ctx, "/etc/config");
    uci_set_confdir(set_ctx, "/etc/config");
    if(UCI_OK == uci_load(get_ctx, "wireless", &pkg)){
        uci_foreach_element(&pkg->sections, e)
        {
            s = uci_to_section(e);
            if(!strcmp(s->type, "wifi-iface"))
            {
                if (NULL != (value = uci_lookup_option_string(get_ctx, s, "ifname")))
                {
                    if (!strcmp(value, sfi_num)){
                        ptr.package = "wireless";
                        ptr.section = s->e.name;

                        ptr.option = "key";
                        ptr.value = "errorkey3131212";
                        uci_set(set_ctx, &ptr);

                        ptr.option = "ssid";
                        ptr.value = "SiWiFi-test";
                        uci_set(set_ctx, &ptr);

                        ptr.option = "disabled";
                        ptr.value = "1";
                        uci_set(set_ctx, &ptr);

                        uci_commit(set_ctx, &ptr.p, false);
                        uci_unload(set_ctx, ptr.p);
                    }
                }
            }
        }
        uci_unload(get_ctx, pkg);
    }

    uci_free_context(get_ctx);
    uci_free_context(set_ctx);
    system("uci set network.stabridge.disabled='1'");
    system("uci commit");
    return 0;
}

static int
ap_mode_set(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
    char *freq;
    char *ssid_24g;
    char *encryption_24g;
    char *key_24g;
    char *ssid_5g;
    char *encryption_5g;
    char *key_5g;
    char cmd[70];
    char cmd_buf[2];
    FILE *pp = NULL;

    struct blob_attr *tb_ping[RPC_AP_MAX];
    blobmsg_parse(ap_mode_set_data, RPC_AP_MAX, tb_ping,
                  blob_data(msg), blob_len(msg));

    if (!tb_ping[RPC_AP_FREQ])
        return UBUS_STATUS_INVALID_ARGUMENT;
    if (!tb_ping[RPC_AP_24G_SSID]||!tb_ping[RPC_AP_24G_ENCRYPT]||!tb_ping[RPC_AP_24G_KEY])
        return UBUS_STATUS_INVALID_ARGUMENT;
    if (!tb_ping[RPC_AP_5G_SSID]||!tb_ping[RPC_AP_5G_ENCRYPT]||!tb_ping[RPC_AP_5G_KEY])
        return UBUS_STATUS_INVALID_ARGUMENT;

    freq = blobmsg_get_string(tb_ping[RPC_AP_FREQ]);
    ssid_24g = blobmsg_get_string(tb_ping[RPC_AP_24G_SSID]);
    encryption_24g = blobmsg_get_string(tb_ping[RPC_AP_24G_ENCRYPT]);
    key_24g = blobmsg_get_string(tb_ping[RPC_AP_24G_KEY]);
    ssid_5g = blobmsg_get_string(tb_ping[RPC_AP_5G_SSID]);
    encryption_5g = blobmsg_get_string(tb_ping[RPC_AP_5G_ENCRYPT]);
    key_5g = blobmsg_get_string(tb_ping[RPC_AP_5G_KEY]);

    //set freq
    memset(cmd,0,sizeof cmd);
    sprintf(cmd,"uci set basic_setting.freq.enable='");
    strcat(cmd,freq);
    strcat(cmd,"'");
    system(cmd);
    system("uci commit");
    //close wds connect
    set_wireless_disable_local("sfi0");
    set_wireless_disable_local("sfi1");
    system("wifi reload");

    set_wireless_local("wlan0" ,ssid_24g ,encryption_24g ,key_24g);
    set_wireless_local("wlan1" ,ssid_5g,encryption_5g,key_5g);

    set_netisolate("wlan0");
    set_netisolate("wlan1");
    system("/etc/init.d/dnsmasq stop");
    system("killall relayd");
    system("/etc/init.d/relayd restart");
    //led
    memset(cmd,0,sizeof cmd);
    sprintf(cmd,"cat /sys/devices/10000000.palmbus/10000000.ethernet/net/eth0/carrier");
    if( (pp = popen(cmd, "r")) == NULL )
    {
        printf("popen() error!/n");
        return -3;
    }
    memset(cmd_buf,0,sizeof cmd_buf);
    fgets(cmd_buf, sizeof cmd_buf, pp);
    if(cmd_buf[0] == '0'){
        system("echo none > /sys/class/leds/wifi-status/trigger");
    }else{
        system("echo default-on > /sys/class/leds/wifi-status/trigger");
    }

    // this delay_kick will do:
        // sleep 2
        // wifi reload
        // kick all station for get ip .
    system("delay_kick ap&");
    return 0;
}

static int
get_sta_num(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{

    char cmd[80] = "hostapd_cli -i wlan0 status|grep \"num_sta\\[0\\]\"|awk -F '=' '{print $2}'";
    char cmd_buf[5];
    FILE *pp;
    int i;
    //get wlan0 sta num
    if( (pp = popen(cmd, "r")) == NULL )
    {
        printf("popen() error!/n");
        return -3;
    }
    memset(cmd_buf,0,sizeof cmd_buf);
    fgets(cmd_buf, sizeof cmd_buf, pp);
    for(i=0 ; i<5 ;i++){
        if(cmd_buf[i] == '\n'){
            cmd_buf[i] = '\0';
        }
    }
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "wlan0_sta_num", cmd_buf);
    ubus_send_reply(ctx, req, buf.head);
    pclose(pp);

    //get wlan1 sta num
    memset(cmd,0,sizeof cmd);
    strcat(cmd,"hostapd_cli -i wlan1 status|grep \"num_sta\\[0\\]\"|awk -F '=' '{print $2}'");
    if( (pp = popen(cmd, "r")) == NULL )
    {
        printf("popen() error!/n");
        return -3;
    }
    memset(cmd_buf,0,sizeof cmd_buf);
    fgets(cmd_buf, sizeof cmd_buf, pp);
    for(i=0 ; i<5 ;i++){
        if(cmd_buf[i] == '\n'){
            cmd_buf[i] = '\0';
        }
    }
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "wlan1_sta_num", cmd_buf);
    ubus_send_reply(ctx, req, buf.head);

    pclose(pp);
    return 0;
}

static int
rpc_luci2_api_init(const struct rpc_daemon_ops *o, struct ubus_context *ctx)
{
        int rv = 0;

        static const struct ubus_method luci2_network_methods[] = {
                UBUS_METHOD_NOARG("lepton",          rpc_luci2_print_lepton), //no params operation
                UBUS_METHOD_NOARG("wifi_reload",          wifi_reload), //no params operation
                UBUS_METHOD_NOARG("dnsmasq_restart",          dnsmasq_restart), //no params operation
                UBUS_METHOD_NOARG("net_restart",          net_restart), //no params operation
                UBUS_METHOD_NOARG("kick_sta",          kick_sta), //no params operation
                UBUS_METHOD_NOARG("get_firmware_version",          get_firmware_version), //no params operation
                UBUS_METHOD_NOARG("get_sta_num",          get_sta_num), //no params operation
                UBUS_METHOD_NOARG("reset_wds",      reset_wds), //no params operation
                UBUS_METHOD("ap_mode_set",           ap_mode_set,
                                                     ap_mode_set_data), //params operation
                UBUS_METHOD("ping1",                 rpc_luci2_network_ping, //params operation
                                                     rpc_ping_data_policy),
                UBUS_METHOD("check_wds",      check_wds, //params operation
                                                     check_wds_data),
                UBUS_METHOD("set_wireless",      set_wireless, //params operation
                                                     set_wireless_data),
                UBUS_METHOD("set_wireless_disable",      set_wireless_disable, //params operation
                                                     set_wireless_disable_data),
                UBUS_METHOD("set_uci",      set_uci, //params operation
                                                     set_uci_data),
                UBUS_METHOD("set_config_last",      set_config_last,//params operation
                                                    set_config_last_data)
        };

        static struct ubus_object_type luci2_network_type =
                UBUS_OBJECT_TYPE("luci-rpc-luci2-lepton", luci2_network_methods);

        static struct ubus_object network_obj = {
                .name = "lepton.network", //interface name
                .type = &luci2_network_type,
                .methods = luci2_network_methods,
                .n_methods = ARRAY_SIZE(luci2_network_methods),
        };

        ops = o;

        rv |= ubus_add_object(ctx, &network_obj);

        return rv;
}

struct rpc_plugin rpc_plugin = {
        .init = rpc_luci2_api_init
};
