#include <iostream>
#include <string>
#include "bal_indications.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

BalErrno BalIndicationsClient::BalAccTermInd(const std::string device_id, bool activation_status) {
    BalIndications request;
    BalErr response;
    ClientContext context;
    request.set_device_id(device_id);
    if(activation_status) {
        request.mutable_access_term_ind()->mutable_data()->set_admin_state(BAL_STATE_UP);
    } else {
        request.mutable_access_term_ind()->mutable_data()->set_admin_state(BAL_STATE_DOWN);
    }
    Status status = stub_->BalAccTermInd(&context, request, &response);
    if(status.ok()) {
        return response.err();
    }
    std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
    return BAL_ERR_COMM_FAIL;
}

BalIndicationsClient *balIndicationsInit(std::string peer_context) {
    auto start = peer_context.find(":");
    if (start == std::string::npos) {
        return nullptr;
    }
    auto end = peer_context.find(":", start+1);
    if(end == std::string::npos) {
        return nullptr;
    }
    std::string peer_host = peer_context.substr(start+1, end - start - 1) + ":60001";
    std::cout << "GRPC Indications Server at " << peer_host << std::endl;
    return new BalIndicationsClient(grpc::CreateChannel(peer_host,
                                                        grpc::InsecureChannelCredentials()));
}
