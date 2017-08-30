#ifndef _BAL_INDICATIONS_H_
#define _BAL_INDICATIONS_H_

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include "bal.grpc.pb.h"

class BalIndicationsClient {
    private:
    std::unique_ptr<BalInd::Stub> stub_;

    public:
    BalIndicationsClient(std::shared_ptr<grpc::Channel>channel)
        : stub_(BalInd::NewStub(channel)) {}
    BalErrno BalAccTermInd(const std::string device_id, bool activation_status);

};

extern BalIndicationsClient *balIndicationsInit(std::string peer_context);

#endif
