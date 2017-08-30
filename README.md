# bal_voltha
BAL edge core voltha grpc server.
Needs the C++ grpc_cpp_plugin installed for protobuf.
Build from source at:
http://github.com/grpc/grpc
and install the protobuf protoc plugin for C++ using the INSTALL.md.

This is the BAL Voltha GRPC server side.

It was written with the Voltha asfvolt16_olt adapter grpc client code as the reference :)

It also includes a bal grpc indications client to talk back to voltha.

It needs the Radisys Broadcom(bcm) agent for the OLT installed on the target accton device.
(volt_2.2.0.0+accton1.0-1_amd64.deb)

This GRPC voltha server translates the GRPC calls to BAL cli commands.

The server runs the BCM agent as the child process on start.

Translates the grpc calls (cfg set) now into bal cli commands and pipes it to the agent.

As of now, it supports and translates bal cfgSet requests to bal cfg set cli:

a) activate_olt

b) activate_onu (untested but should work)

c) activate_pon (untested but should work)

So activate_olt request for asfvolt16_olt works.

The indications client as of now sends access term indicator on activation complete to voltha.

The approach is pretty simple w.r.t directly utilizing BAL cli over translating incoming GRPC bal request to BAL messages to send another RPC (hare-brained approach).

One could just start the agent inside the server and talk to the bcm over BAL cli interface.
