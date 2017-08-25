#ifndef _HELPER_H_
#define _HELPER_H_

#include "bal.grpc.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

    extern void balCfgSetCmdToCli(const BalCfg *cfg, BalErr *response);
    extern int startAgent(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
