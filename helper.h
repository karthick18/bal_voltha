#ifndef _HELPER_H_
#define _HELPER_H_

#include "bal.grpc.pb.h"

extern void balCfgSetCmdToCli(const BalCfg *cfg, BalErr *response);
extern int startAgent(int argc, char **argv);

#endif
