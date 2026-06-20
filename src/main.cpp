// src/main.cpp
// FlowHub — AI Chat 功能集成入口

#include "common/TaskPool.hpp"
#include "fw/EoEnv.hpp"
#include "generated/TaskType.hpp"

#include "access/CliAdapter.hpp"
#include "DPlane/business/AiChatBus.hpp"
#include "DPlane/business/Router.hpp"
#include "DPlane/service/AiApiAdapter.hpp"
#include "DPlane/service/ServiceGateway.hpp"
#include "DPlane/session/SessionData.hpp"
#include "DPlane/session/SessionMgr.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace common;
using namespace common::message;

static void printBanner()
{
    std::cout << "\n"
              << "╔═══════════════════════════════╗\n"
              << "║   FlowHub AI Chat (CLI)      ║\n"
              << "║   输入消息开始对话             ║\n"
              << "║   /exit  退出                 ║\n"
              << "╚═══════════════════════════════╝\n\n";
}

int main()
{
    fw::EoEnv env;
    TaskPool pool;

    // ---- API 配置 ----
    const char *apiKey = std::getenv("FLOWHUB_API_KEY");
    const char *apiUrl = std::getenv("FLOWHUB_API_URL");
    const char *model = std::getenv("FLOWHUB_MODEL");

    // 未设环境变量时使用硬编码默认值
    if (apiKey == nullptr)
        apiKey = "sk-ck49fnhqcb9uo91pqjvra4o53or7hahiyrhps2ztsedcl0mi";
    if (apiUrl == nullptr)
        apiUrl = "https://api.xiaomimimo.com";
    if (model == nullptr)
        model = "mimo-v2.5";

    std::cout << "FlowHub v0.2.0 — AI Chat\n"
              << "  API: " << apiUrl << "\n"
              << "  Model: " << (model ? model : "(default)") << "\n";

    // ===== 1. 创建所有 EO（按依赖顺序，地址通过构造函数注入）=====

    // 服务层 — kMayBlock=true，createEo 自动以 detached 创建
    auto aiApiAdapter =
        env.createEo<DPlane::service::AiApiAdapter>(apiUrl, apiKey, model ? model : "default");

    // 服务层 — ServiceGateway（统一入口，ADR-0012）
    auto serviceGateway = env.createEo<DPlane::service::ServiceGateway>(aiApiAdapter);

    // 会话层 — SessionMgr 管理 GTID 分配/回收
    auto sessionMgr = env.createEo<DPlane::session::SessionMgr>(pool);

    // 业务层 — Router
    auto router = env.createEo<DPlane::business::Router>();

    // 会话层 — SessionData（需要 Router 地址）
    auto sessionData = env.createEo<DPlane::session::SessionData>(router);

    // 业务层 — AiChatBus（发 AiChatServiceReq 到 Gateway，不发 Adapter 直连）
    auto aiChatBus = env.createEo<DPlane::business::AiChatBus<TaskType::AiChat>>(
        pool, serviceGateway, sessionData, model ? std::string(model) : std::string("default"));

    // 接入层 — CliAdapter
    auto cliAdapter = env.createEo<access::CliAdapter>(sessionMgr, sessionData);

    // ===== 2. 注入地址依赖 =====
    // Router: aiChatBusAddr
    fw::anonSendTo(router, ModifyReq{aiChatBus});
    // AiChatBus: routerAddr（用于 AiChatServiceReq.sourceAddress，ADR-0014）
    fw::anonSendTo(aiChatBus, ModifyReq{router});

    // ===== 3. 启动时建立会话 =====
    std::cout << "[main] creating session...\n";
    fw::anonSendTo(cliAdapter, SessionSetupReq{TaskType::AiChat});
    // 等 SessionMgr 处理完
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // ===== 4. 主循环 =====
    printBanner();
    std::cout << "> " << std::flush;

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
        {
            std::cout << "> " << std::flush;
            continue;
        }

        if (line == "/exit")
        {
            fw::anonSendTo(cliAdapter, SessionCloseReq{{{0}}});
            break;
        }

        // 发送用户输入到 CliAdapter
        fw::anonSendTo(cliAdapter, AiChatReq{{{0}, {}}, std::move(line)});

        std::cout << "> " << std::flush;
    }

    std::cout << "\n[main] goodbye.\n";
    return 0;
}
