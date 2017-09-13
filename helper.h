#ifndef _HELPER_H_
#define _HELPER_H_

#include "bal.grpc.pb.h"
#include "bal_indications.h"

extern void balCfgSetCmdToCli(const BalCfg *cfg, BalErr *response, BalIndicationsClient *bal_ind_clnt);
extern int startAgent(int argc, char **argv);

#endif
