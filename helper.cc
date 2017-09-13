#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/security/server_credentials.h>
#include "helper.h"
#include "stack.h"
#include "bal_indications.h"

#define BCM_DIR "/opt/bcm68620/release.bin.agent"

#define BAL_CLI bal_cli_fds[1]

typedef enum BalCfgSetCmd {
    CFG_UNKNOWN,
    CFG_ADMIN,
    CFG_PON_NNI,
    CFG_ONU,
    CFG_PACKET_OUT,
    CFG_OMCI_REQ,
    CFG_FLOW,
    CFG_SCHEDULER,
} BalCfgSetCmd;

#define CHECK_ADMIN(setreq, hdr, cfg, data) ( (setreq)->has_##hdr() && (setreq)->has_##cfg() && (setreq)->cfg().has_##data() )
#define STACK_ADMIN(stack, setreq, hdr, cfg, data) do {                 \
    std::string _a(admin_state_map[ (int)((setreq)->cfg().data().admin_state()) ]); \
    std::string _o(obj_type_map[ (int)( (setreq)->hdr().obj_type() ) ]); \
    (stack).Push(_a);                                                   \
    (stack).Push(" admin_state=");                                      \
    (stack).Push(_o);                                                   \
    (stack).Push("set object=");                                        \
}while(0)

#define ACTIVATE_PON_NNI(setreq, hdr, interface) ( (setreq)->has_##hdr() && (setreq)->has_##interface() && (setreq)->interface().has_key() && (setreq)->interface().has_data() )
#define STACK_PON_NNI(stack, setreq, hdr, interface) do {               \
    std::string _a(admin_state_map[ (int)((setreq)->interface().data().admin_state()) ]); \
    std::string _o(obj_type_map[ (int)( (setreq)->hdr().obj_type() ) ]); \
    std::string _i(intf_type_map[ (int) ( (setreq)->interface().key().intf_type() ) ]); \
    std::string _t(transceiver_type_map[ (int) ( (setreq)->interface().data().transceiver_type() ) ]); \
    (stack).Push(_t);                                                   \
    (stack).Push(" transceiver_type=");                                 \
    (stack).Push(_a);                                                   \
    (stack).Push(" admin_state=");                                      \
    (stack).Push(_i);                                                   \
    (stack).Push(" intf_type=");                                        \
    (stack).Push(std::to_string( (setreq)->interface().key().intf_id() ) ); \
    (stack).Push(" intf_id=");                                          \
    (stack).Push(_o);                                                   \
    (stack).Push("set object=");                                        \
}while(0)

#define ACTIVATE_ONU(setreq, hdr, terminal) ( (setreq)->has_##hdr() && (setreq)->has_##terminal() && (setreq)->terminal().has_key() && (setreq)->terminal().has_data() && (setreq)->terminal().data().has_serial_number() )
#define STACK_ONU(stack, setreq, hdr, terminal) do {                    \
    std::string _a(admin_state_map[ (int)((setreq)->terminal().data().admin_state()) ]); \
    std::string _o(obj_type_map[ (int)( (setreq)->hdr().obj_type() ) ]); \
    const char  *_vc = (setreq)->terminal().data().serial_number().vendor_id().c_str(); \
    char vendor[10];                                                    \
    snprintf(vendor, sizeof(vendor), "%x%x%x%x", _vc[0], _vc[1], _vc[2], _vc[3]); \
    (stack).Push( (setreq)->terminal().data().registration_id() );      \
    (stack).Push(" registration_id.arr=");                              \
    (stack).Push( (setreq)->terminal().data().serial_number().vendor_specific()); \
    (stack).Push(" serial_number.vendor_specific=");                    \
    std::string _vid(vendor);                                           \
    (stack).Push(_vid);                                                 \
    (stack).Push(" serial_number.vendor_id=");                          \
    (stack).Push(_a);                                                   \
    (stack).Push(" admin_state=");                                      \
    (stack).Push(std::to_string( (setreq)->terminal().key().intf_id() ) ); \
    (stack).Push(" intf_id=");                                          \
    (stack).Push(std::to_string( (setreq)->terminal().key().sub_term_id() ) ); \
    (stack).Push(" sub_term_id=");                                      \
    (stack).Push(_o);                                                   \
    (stack).Push("set object=");                                        \
}while(0)

#define CHECK_PACKET_OUT(setreq, packet) ( (setreq)->has_##packet() && (setreq)->packet().has_data() && (setreq)->packet().has_key() && (setreq)->packet().key().has_packet_send_dest() && (setreq)->packet().key().packet_send_dest().has_sub_term() )
#define STACK_PACKET_OUT(stack, setreq, packet) do {                    \
    std::string _f(flow_type_map[ (int)( (setreq)->packet().data().flow_type() ) ]); \
    (stack).Push( (setreq)->packet().data().pkt() );                    \
    (stack).Push(" pkt=");                                              \
    (stack).Push(std::to_string( (setreq)->packet().data().intf_id()) ); \
    (stack).Push(" intf_id=");                                          \
    (stack).Push(_f);                                                   \
    (stack).Push(" flow_type=");                                        \
    (stack).Push(std::to_string( (setreq)->packet().key().packet_send_dest().sub_term().intf_id())); \
    (stack).Push(" packet_send_dest.int_id=");                          \
    (stack).Push(std::to_string((setreq)->packet().key().packet_send_dest().sub_term().sub_term_uni())); \
    (stack).Push(" packet_send_dest.sub_term_uni=");                    \
    (stack).Push(std::to_string((setreq)->packet().key().packet_send_dest().sub_term().sub_term_id())); \
    (stack).Push(" packet_send_dest.sub_term_id=");                     \
    (stack).Push("set object=packet reserved=0 packet_send_dest.type=sub_term"); \
}while(0)

#define CHECK_OMCI_REQ(setreq, packet) ( (setreq)->has_##packet() && (setreq)->packet().has_data() && (setreq)->packet().has_key() && (setreq)->packet().key().has_packet_send_dest() && (setreq)->packet().key().packet_send_dest().has_itu_omci_channel() )

#define STACK_OMCI_REQ(stack, setreq, packet) do {                      \
    (stack).Push("bal/");                                               \
    (stack).Push("..\n");                                               \
    (stack).Push(" admin_state=up\n");                                  \
    (stack).Push(std::to_string( (setreq)->packet().key().packet_send_dest().itu_omci_channel().sub_term_id())); \
    (stack).Push(" sub_term_id=");                                      \
    (stack).Push(std::to_string( (setreq)->packet().key().packet_send_dest().itu_omci_channel().intf_id()) ); \
    (stack).Push(" intf_id=");                                          \
    (stack).Push("set object=sub_term");                                \
    (stack).Push("omci/\n");                                            \
    (stack).Push("..\n");                                               \
}while(0)

#define CHECK_FLOW(setreq, hdr, flow) ( (setreq)->has_##hdr() && (setreq)->has_##flow() && (setreq)->flow().has_key() && (setreq)->flow().has_data() )
#define STACK_FLOW(stack, setreq, hdr, flow) do {                       \
    std::string _f(flow_type_map[ (int)( (setreq)->flow().key().flow_type() ) ]); \
    std::string _a(admin_state_map[ (int) ( (setreq)->flow().data().admin_state() ) ]); \
    std::string _o(obj_type_map[ (int) ( (setreq)->hdr().obj_type() ) ]); \
    std::string _pkt_tag_type( pkt_tag_map[ mask_to_shift( (uint32) ( (setreq)->flow().data().classifier().pkt_tag_type() ) ) ]); \
    if( (setreq)->flow().data().has_action() ) {                        \
        std::string _action_cmd_mask( action_mask_map [ mask_to_shift( (uint32)( (setreq)->flow().data().action().cmds_bitmask())) ] ); \
        if( (setreq)->flow().data().action().presence_mask() & BAL_ACTION_ID_O_VID ) { \
            (stack).Push(std::to_string( (setreq)->flow().data().action().o_vid() ) ); \
            (stack).Push(" action.o_vid=");                             \
        }                                                               \
        (stack).Push(_action_cmd_mask);                                 \
        (stack).Push(" action.cmds_bitmask=");                          \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_PKT_TAG_TYPE ) { \
        (stack).Push(_pkt_tag_type);                                    \
        (stack).Push(" classifier.pkt_tag_type=");                      \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_I_VID ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().classifier().i_vid())); \
        (stack).Push(" classifier.i_vid=");                             \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_SRC_IP ) { \
        (stack).Push( to_ip((setreq)->flow().data().classifier().src_ip()) ); \
        (stack).Push(" classifier.src_ip=");                            \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_DST_IP ) { \
        (stack).Push( to_ip((setreq)->flow().data().classifier().dst_ip()) ); \
        (stack).Push(" classifier.dst_ip=");                            \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_DST_PORT ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().classifier().dst_port())); \
        (stack).Push(" classifier.dst_port=");                          \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_SRC_PORT ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().classifier().src_port() )); \
        (stack).Push(" classifier.src_port=");                          \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_O_PBITS ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().classifier().o_pbits()) ); \
        (stack).Push(" classifier.o_pbits=");                           \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_O_VID ) { \
    (stack).Push(std::to_string( (setreq)->flow().data().classifier().o_vid() ) ); \
    (stack).Push(" classifier.o_vid=");                                 \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_IP_PROTO ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().classifier().ip_proto())); \
        (stack).Push(" classifier.ip_proto=");                          \
    }                                                                   \
    if( (setreq)->flow().data().classifier().presence_mask() & BAL_CLASSIFIER_ID_ETHER_TYPE ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().classifier().ether_type() )); \
        (stack).Push(" classifier.ether_type=");                        \
    }                                                                   \
    (stack).Push(std::to_string( (setreq)->flow().data().svc_port_id())); \
    (stack).Push(" svc_port_id=");                                      \
    (stack).Push(std::to_string( (setreq)->flow().data().sub_term_id())); \
    (stack).Push(" sub_term_id=");                                      \
    (stack).Push(std::to_string( (setreq)->flow().data().access_int_id())); \
    (stack).Push(" access_int_id=");                                    \
    (stack).Push(_a);                                                   \
    (stack).Push(" admin_state=");                                      \
    if ( (setreq)->flow().key().flow_type() == BAL_FLOW_TYPE_UPSTREAM ) { \
        (stack).Push(std::to_string( (setreq)->flow().data().dba_tm_sched_id() ) ); \
        (stack).Push(" queue.tm_sched_id=");                            \
    }                                                                   \
    (stack).Push(_f);                                                   \
    (stack).Push(" flow_type=");                                        \
    (stack).Push(std::to_string( (setreq)->flow().key().flow_id() ));   \
    (stack).Push(" flow_id=");                                          \
    (stack).Push(_o);                                                   \
    (stack).Push("set object=");                                        \
}while(0)

#define CHECK_SCHEDULER(setreq, hdr, sched) ( (setreq)->has_##hdr() && (setreq)->has_##sched() && (setreq)->sched().has_key() )
#define STACK_SCHEDULER(stack, setreq, hdr, sched) do {                 \
    std::string _dir( sched_dir_map[ (int) (setreq)->sched().key().dir() ] ); \
    std::string _o( obj_type_map [ (int) (setreq)->hdr().obj_type() ] ); \
    std::string _owner_type( owner_type_map [ (int) (setreq)->sched().data().owner().type() ] ); \
    std::string _sched_type( sched_type_map [ (int) (setreq)->sched().data().sched_type() ] ); \
    (stack).Push(_sched_type);                                          \
    (stack).Push(" sched_type=");                                       \
    if( (setreq)->sched().data().owner().type() == BAL_TM_SCHED_OWNER_TYPE_AGG_PORT ) { \
        if( (setreq)->sched().data().owner().agg_port().presence_mask() & BAL_TM_SCHED_OWNER_AGG_PORT_ID_SUB_TERM_ID ) { \
            (stack).Push(std::to_string( (setreq)->sched().data().owner().agg_port().sub_term_id())); \
            (stack).Push(" owner.sub_term_id=");                        \
        }                                                               \
        if( (setreq)->sched().data().owner().agg_port().presence_mask() & BAL_TM_SCHED_OWNER_AGG_PORT_ID_AGG_PORT_ID ) { \
            (stack).Push(std::to_string( (setreq)->sched().data().owner().agg_port().agg_port_id() )); \
            (stack).Push(" owner.agg_port_id=");                        \
        }                                                               \
        if( (setreq)->sched().data().owner().agg_port().presence_mask() & BAL_TM_SCHED_OWNER_AGG_PORT_ID_INTF_ID ) { \
            (stack).Push(std::to_string( (setreq)->sched().data().owner().agg_port().intf_id())); \
            (stack).Push(" owner.intf_id=");                            \
        }                                                               \
    }                                                                   \
    (stack).Push(_owner_type);                                          \
    (stack).Push( "owner.type=");                                       \
    (stack).Push(std::to_string( (setreq)->sched().data().num_priorities() ) ); \
    (stack).Push( "num_priorities=");                                   \
    (stack).Push(std::to_string( (setreq)->sched().key().id() ) );      \
    (stack).Push(" id=");                                               \
    (stack).Push(_dir);                                                 \
    (stack).Push( "dir=");                                              \
    (stack).Push(_o);                                                   \
    (stack).Push("set object=");                                        \
}while(0)


#define STACK_OMCI_SAMPLE(stack) do {           \
    (stack).Push("bal/");                       \
    (stack).Push("..\n");                       \
    (stack).Push(" admin_state=up\n");          \
    (stack).Push(std::to_string(0));            \
    (stack).Push(" sub_term_id=");              \
    (stack).Push(std::to_string(0));            \
    (stack).Push(" intf_id=");                  \
    (stack).Push("set object=sub_term");        \
    (stack).Push("omci/\n");                    \
    (stack).Push("..\n");                       \
}while(0)

static const char *obj_type_map[] = { "access_terminal", "flow", "group", "interface", "packet",
                                      "subscriber_terminal", "tm_queue", "tm_sched" };

static const char *admin_state_map[] = { "invalid", "up", "down", "testing" };

static const char *intf_type_map[] = { "nni", "pon", "epon_1g", "epon_10g" };

static const char *transceiver_type_map[] = {"gpon_sps_43_48", "gpon_sps_sog_4321",
                                                 "gpon_lte_3680_m", "gpon_source_photonics",
                                                 "gpon_lte_3680_p", "xgpon_lth_7222_pc",
                                                 "xgpon_lth_7226_pc", "xgpon_lth_5302_pc",
                                                 "xgpon_lth_7226_a_pc_plus"};

static const char *flow_type_map[] = {"upstream", "downstream", "broadcast", "multicast"};

static const char *pkt_tag_map[] = {"none", "untagged", "single_tag", "double_tag"};

static const char *action_mask_map[] =  {"none", "add_outer_tag", "remove_outer_tag", "xlate_outer_tag", "xlate_two_tags",\
                                         "discard_ds_bcast", "discard_ds_unknown", "add_two_tags", "remove_two_tags", "remark_pbits",\
                                         "copy_pbits", "reverse_copy_pbits", "dscp_to_pbits", "trap_to_host"};

static const char *owner_type_map[] = {"undefined", "interface", "sub_term", "agg_port", "uni", "virtual"};
static const char *sched_type_map[] = {"none", "wfq", "sp", "sp_wfq"};
static const char *sched_dir_map[] = {"invalid", "us", "ds"};

static int bal_cli_fds[2];

typedef unsigned int uint32;

static std::string to_ip(uint32 val) {
#define _V(i) ( ( (val) >> ( (i) * 8 ) ) & 0xff )

    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", _V(3), _V(2), _V(1), _V(0));
    return std::string(buf);

#undef _V
}

static __inline__ int mask_to_shift(unsigned int mask) {
    int shift = 0;
    if(mask == 0) {
        return 0;
    }
    for(shift = 0; (1 << shift) <= mask; shift++);
    return shift;
}

static BalErrno BalAccTermInd(BalIndicationsClient *bal_ind_clnt,
                            const std::string device_id,
                            bool activation_status) {
    return bal_ind_clnt->BalAccTermInd(device_id, activation_status);
}

static void sendBalAccTermInd(BalIndicationsClient *bal_ind_clnt, const std::string device_id, bool status) {
    if(bal_ind_clnt->future_ready) {
        //block on the last set to finish before continuing
        bal_ind_clnt->future_ready = false;
        bal_ind_clnt->future.get();
    }
    bal_ind_clnt->future = std::async(std::launch::async, BalAccTermInd, bal_ind_clnt, device_id, status);
    bal_ind_clnt->future_ready = true;
}

void balCfgSetCmdToCli(const BalCfg *cfg, BalErr *response, BalIndicationsClient *bal_ind_clnt) {
    char cli_cmd[1024] = {0};
    stack::Stack <std::string> stk;
    const std::string device_id = cfg->device_id();
    BalErrno err = BAL_ERR_OK;
    BalCfgSetCmd cmd = CFG_UNKNOWN;
    response->set_err(err);
    if(CHECK_ADMIN(cfg, hdr, cfg, data)) {
        cmd = CFG_ADMIN;
        STACK_ADMIN(stk, cfg, hdr, cfg, data);
    }
    else if(ACTIVATE_PON_NNI(cfg, hdr, interface)) {
        cmd = CFG_PON_NNI;
        STACK_PON_NNI(stk, cfg, hdr, interface);
    }
    else if(ACTIVATE_ONU(cfg, hdr, terminal)) {
        cmd = CFG_ONU;
        STACK_ONU(stk, cfg, hdr, terminal);
    }
    else if(CHECK_PACKET_OUT(cfg, packet)) {
        cmd = CFG_PACKET_OUT;
        STACK_PACKET_OUT(stk, cfg, packet);
    }
    else if(CHECK_OMCI_REQ(cfg, packet)) {
        cmd = CFG_OMCI_REQ;
        STACK_OMCI_REQ(stk, cfg, packet);
    }
    else if(CHECK_FLOW(cfg, hdr, flow)) {
        cmd = CFG_FLOW;
        STACK_FLOW(stk, cfg, hdr, flow);
    }
    else if(CHECK_SCHEDULER(cfg, hdr, tm_sched_cfg)) {
        cmd = CFG_SCHEDULER;
        STACK_SCHEDULER(stk, cfg, hdr, tm_sched_cfg);
    }
    else {
        err = BAL_ERR_PARM;
    }
    response->set_err(err);
    if(err == BAL_ERR_OK) {
        std::ostringstream str_stream;
        str_stream << stk;
        snprintf(cli_cmd, sizeof(cli_cmd), "%s\n", str_stream.str().c_str());
        std::cout << "CLI command translated: " << cli_cmd << std::endl;
        write(BAL_CLI, cli_cmd, strlen(cli_cmd));
    }

    //send indications as appropriate
    switch(cmd) {
    case CFG_ADMIN:
        {
            bool status = err == BAL_ERR_OK ? true : false;
            sendBalAccTermInd(bal_ind_clnt, device_id, status);
        }
        break;
    default:
        //other indications not sent for now
        break;
    }
}

static void enter_bal(void) {
    write(BAL_CLI, "bal/\n", 5);
}

__attribute__((unused)) static void test_omci(void) {
    stack::Stack <std::string> stk;
    std::ostringstream str_stream;
    char cli_cmd[100];
    STACK_OMCI_SAMPLE(stk);
    str_stream << stk;
    snprintf(cli_cmd, sizeof(cli_cmd), "%s\n", str_stream.str().c_str());
    write(BAL_CLI, cli_cmd, strlen(cli_cmd));
}

__attribute__((unused)) static int cmdloop(int child_pid, int wfd) {
    char buf[500];
    while(fgets(buf, sizeof(buf), stdin) != NULL) {
        if(!strncmp(buf, "quit", 4)) {
            break;
        }
        write(wfd, buf, strlen(buf));
    }
    kill(child_pid, SIGKILL);
    printf("Exiting agent\n");
    return 0;
}

int startAgent(int argc, char **argv) {
    int pid;
    if(pipe(bal_cli_fds) < 0) {
        perror("pipe:");
        exit(EXIT_FAILURE);
    }
    signal(SIGCHLD, SIG_IGN);
    chdir(BCM_DIR);
    switch((pid = fork())) {
    case 0:
        {
            char *prog[] = { (char*)"./start_sdn_agent.sh", (char*)"-nl", NULL };
            close(bal_cli_fds[1]);
            dup2(bal_cli_fds[0], STDIN_FILENO);
            execvp(prog[0], prog);
        }
    case -1:
        perror("fork:");
        exit(EXIT_FAILURE);
    default:
        break;
    }
    close(bal_cli_fds[0]);
    close(STDIN_FILENO);
    printf("Waiting for the bcm agent to start ...\n");
    sleep(15);
    printf("Entering Bal CLI\n");
    enter_bal();
    return 0;
}
