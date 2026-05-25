// src/main.cpp
// FlowHub - 基于 CAF 的消息驱动嵌入式编排平台

#include "caf/all.hpp"

#include "CPlane/BusinessMgr.hpp"
#include "DPlane/business/Router.hpp"

#include <iostream>

using namespace common::message;

int main()
{
    caf::init_global_meta_objects<caf::id_block::flowhub>();
    caf::core::init_global_meta_objects();

    caf::actor_system_config cfg;
    caf::actor_system sys{cfg};

    std::cout << "FlowHub v0.1.0 — Actor 通信验证\n";

    auto businessMgr = sys.spawn<CPlane::BusinessMgr>();
    auto router = sys.spawn<DPlane::business::Router>();

    caf::anon_mail(ModifyReq{businessMgr}).send(router);
    caf::anon_mail(InternalPing{"Hello from main"}).send(router);

    sys.await_all_actors_done();

    std::cout << "通信验证通过。\n";
    return 0;
}
