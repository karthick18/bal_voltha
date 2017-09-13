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
    CFG_UNKNOWN = 0,
    CFG_ADMIN = 1,
    CFG_PON_NNI = 2,
    CFG_ONU = 3,
    CFG_PACKET_OUT = 4,
    CFG_OMCI_REQ = 5,
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
static int bal_cli_fds[2];

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
