#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
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

#define BCM_DIR "/opt/bcm68620/release.bin.agent"

#define BAL_CLI bal_cli_fds[1]

static const char *obj_type_map[] = { "access_terminal", "flow", "group", "interface", "packet",
                                      "subscriber_terminal", "tm_queue", "tm_sched" };

static const char *admin_state_map[] = { "invalid", "up", "down", "testing" };

static const char *intf_type_map[] = { "nni", "pon", "epon_1g", "epon_10g" };

static const char *transceiver_type_map[] = {"gpon_sps_43_48", "gpon_sps_sog_4321",
                                                 "gpon_lte_3680_m", "gpon_source_photonics",
                                                 "gpon_lte_3680_p", "xgpon_lth_7222_pc",
                                                 "xgpon_lth_7226_pc", "xgpon_lth_5302_pc",
                                                 "xgpon_lth_7226_a_pc_plus"};

static int bal_cli_fds[2];

void balCfgSetCmdToCli(const BalCfg *cfg, BalErr *response) {
    char cli_cmd[200] = {0};
    BalErrno err = BAL_ERR_PARM;
    response->set_err(err);
    if(cfg->has_hdr() && cfg->has_cfg() && cfg->cfg().has_data()) {
        if((int)cfg->hdr().obj_type() >= sizeof(obj_type_map)/sizeof(obj_type_map[0])) {
            std::cerr << "Unknown object type " << cfg->hdr().obj_type() << std::endl;
            return;
        }
        if((int)cfg->cfg().data().admin_state() >= sizeof(admin_state_map)/sizeof(admin_state_map[0])) {
            std::cerr << "Unknown admin state transition " << cfg->cfg().data().admin_state() << std::endl;
            return;
        }
        snprintf(cli_cmd, sizeof(cli_cmd), "set object=%s admin_state=%s\n",
                 obj_type_map[(int)cfg->hdr().obj_type()],
                 admin_state_map[(int)cfg->cfg().data().admin_state()]);
    } else if(cfg->has_hdr() && cfg->has_interface()) {
        //activate pon/nni interface
        if((int)cfg->hdr().obj_type() >= sizeof(obj_type_map)/sizeof(obj_type_map[0])) {
            std::cerr << "Unknown object type " << cfg->hdr().obj_type() << std::endl;
            return;
        }
        if(!cfg->interface().has_key() || !cfg->interface().has_data()) {
            std::cerr << "No interface key or data specified" << std::endl;
            return;
        }
        snprintf(cli_cmd, sizeof(cli_cmd), "set object=%s intf_id=%d intf_type=%s admin_state=%s transceiver_type=%s\n",
                 obj_type_map[(int)cfg->hdr().obj_type()],
                 cfg->interface().key().intf_id(),
                 intf_type_map[(int)cfg->interface().key().intf_type()],
                 admin_state_map[(int)cfg->interface().data().admin_state()],
                 transceiver_type_map[(int)cfg->interface().data().transceiver_type()]
                 );
    } else if(cfg->has_hdr() && cfg->has_terminal()) {
        //activate onu
        if((int)cfg->hdr().obj_type() >= sizeof(obj_type_map)/sizeof(obj_type_map[0])) {
            std::cerr << "Unknown object type " << cfg->hdr().obj_type() << std::endl;
            return;
        }
        if(!cfg->terminal().has_key() || !cfg->terminal().has_data()) {
            std::cerr << "No terminal key/data specified " << std::endl;
            return;
        }
        if(!cfg->terminal().data().has_serial_number()) {
            std::cerr << "No serial number in request" << std::endl;
            return;
        }
        snprintf(cli_cmd, sizeof(cli_cmd),
                 "set object=%s sub_term_id=%d admin_state=%s serial_number.vendor_id=%s serial_number.vendor_specific=%s registration_id.arr=%s\n",
                 obj_type_map[(int)cfg->hdr().obj_type()],
                 cfg->terminal().key().sub_term_id(),
                 admin_state_map[(int)cfg->terminal().data().admin_state()],
                 cfg->terminal().data().serial_number().vendor_id().c_str(),
                 cfg->terminal().data().serial_number().vendor_specific().c_str(),
                 cfg->terminal().data().registration_id().c_str());
    }
    response->set_err(BAL_ERR_OK);
    if(cli_cmd[0]) {
        std::cout << "CLI command translated: " << cli_cmd << std::endl;
        write(BAL_CLI, cli_cmd, strlen(cli_cmd));
    }
}

static void enter_bal(void) {
    write(BAL_CLI, "bal/\n", 5);
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
