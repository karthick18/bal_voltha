/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/security/server_credentials.h>
#include "bal.grpc.pb.h"
#include "helper.h"
#include "bal_indications.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

class BalServiceImpl final : public Bal::Service {
    private:
    BalIndicationsClient *bal_ind_clnt;

    public:
    Status BalApiInit(ServerContext* context, const BalInit* request, BalErr* response) {
        std::cout << "Server got API init from:" << context->peer() << std::endl;
        bal_ind_clnt = balIndicationsInit(context->peer());
        if(bal_ind_clnt == nullptr) {
            response->set_err(BAL_ERR_COMM_FAIL);
        }
        else
        {
            response->set_err(BAL_ERR_OK);
        }
        return Status::OK;
    }

    Status BalApiFinish(ServerContext* context, const BalCfg* request, BalErr* response) {
        std::cout << "Server got API finish" << std::endl;
        response->set_err(BAL_ERR_OK);
        return Status::OK;
    }

    Status BalCfgSet(ServerContext* context, const BalCfg* request, BalErr* response) {
        std::cout << "Server got CFG Set for device id " << request->device_id() << std::endl;
        balCfgSetCmdToCli(request, response);
        if(bal_ind_clnt != nullptr) {
            if(response->err() == BAL_ERR_OK) {
                bal_ind_clnt->BalAccTermInd(request->device_id(), true);
            }
            else
            {
                bal_ind_clnt->BalAccTermInd(request->device_id(), false);
            }
        }
        return Status::OK;
    }

    Status BalCfgClear(ServerContext* context, const BalKey* request, BalErr* response) {
        std::cout << "Server got CFG Clear" << std::endl;
        response->set_err(BAL_ERR_OK);
        return Status::OK;
    }

    Status BalCfgGet(ServerContext* context, const BalKey* request, BalCfg* response) {
        std::cout << "Server got CFG Get" << std::endl;
        return Status::OK;
    }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  BalServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
    startAgent(argc, argv);
    RunServer();
    return 0;
}
