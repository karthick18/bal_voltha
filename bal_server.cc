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
#include <iterator>
#include <map>
#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/security/server_credentials.h>
#include "bal.grpc.pb.h"
#include "helper.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

#define BAL_GRPC_PORT_DEFAULT 50051

class BalServiceImpl final : public Bal::Service {
    private:
    const std::string bal_indications_grpc_port = "60001";
    std::map<std::string, BalIndicationsClient*> bal_indications_map;

    std::string GetBalIndicationsHost(const std::string peer_context) {
        auto start = peer_context.find(":");
        if (start == std::string::npos) {
            return nullptr;
        }
        auto end = peer_context.find(":", start+1);
        if(end == std::string::npos) {
            return nullptr;
        }
        std::string peer_host = peer_context.substr(start+1, end - start - 1) + ":" + bal_indications_grpc_port;
        return peer_host;
    }

    BalIndicationsClient *BalIndicationsMapPut(const std::string peer) {
        BalIndicationsClient *bal_ind_clnt = nullptr;
        std::map<std::string, BalIndicationsClient*>::iterator it;
        bal_ind_clnt = BalIndicationsMapGet(peer);
        if(bal_ind_clnt == nullptr) {
            bal_ind_clnt = balIndicationsInit(peer);
            if(bal_ind_clnt != nullptr) {
                bal_indications_map[peer] = bal_ind_clnt;
            }
        }
        return bal_ind_clnt;
    }

    BalIndicationsClient *BalIndicationsMapGet(const std::string peer) {
        std::map<std::string, BalIndicationsClient*>::iterator it;
        it = bal_indications_map.find(peer);
        if(it != bal_indications_map.end()) {
            return it->second;
        }
        return nullptr;
    }

    public:
    Status BalApiInit(ServerContext* context, const BalInit* request, BalErr* response) {
        BalIndicationsClient *bal_ind_clnt;
        const std::string peer = GetBalIndicationsHost(context->peer());
        std::map<std::string, BalIndicationsClient*>::iterator it;
        std::cout << "Server got API init from:" << peer << std::endl;
        bal_ind_clnt = BalIndicationsMapPut(peer);
        if(bal_ind_clnt == nullptr) {
            response->set_err(BAL_ERR_COMM_FAIL);
        }
        else {
            response->set_err(BAL_ERR_OK);
        }
        return Status::OK;
    }

    Status BalApiFinish(ServerContext* context, const BalCfg* request, BalErr* response) {
        BalIndicationsClient *bal_ind_clnt;
        const std::string peer = GetBalIndicationsHost(context->peer());
        std::cout << "Server got API finish from peer:" << peer << std::endl;
        //remove the peer from the indications map
        bal_ind_clnt = BalIndicationsMapGet(peer);
        if(bal_ind_clnt != nullptr) {
            bal_indications_map.erase(peer);
            if(bal_ind_clnt->future_ready) {
                bal_ind_clnt->future.get();
            }
            delete bal_ind_clnt;
        }
        response->set_err(BAL_ERR_OK);
        return Status::OK;
    }

    Status BalCfgSet(ServerContext* context, const BalCfg* request, BalErr* response) {
        BalIndicationsClient *bal_ind_clnt;
        const std::string device_id = request->device_id();
        const std::string peer = GetBalIndicationsHost(context->peer());
        std::cout << "Server got CFG Set for device id " << device_id << std::endl;
        bal_ind_clnt = BalIndicationsMapGet(peer);
        if(bal_ind_clnt == nullptr) {
            std::cerr << "Bal Indications client not found for peer " << peer << std::endl;
            std::cerr << "Bal API Init seems to have been never called for peer " << peer << std::endl;
            response->set_err(BAL_ERR_INVALID_OP);
            return Status::OK;
        }
        balCfgSetCmdToCli(request, response, bal_ind_clnt);
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

void RunServer(const std::string server_address) {
  BalServiceImpl service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
    std::string server_address = "0.0.0.0";
    int port = BAL_GRPC_PORT_DEFAULT;
    if(argc > 1) {
        port = atoi(argv[1]);
    }
    server_address += ":" + std::to_string(port);
    startAgent(argc, argv);
    RunServer(server_address);
    return 0;
}
