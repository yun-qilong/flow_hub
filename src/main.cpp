// src/main.cpp
// FlowHub — AI Chat 功能集成入口

#include "common/TaskPool.hpp"
#include "fw/EoEnv.hpp"
#include "generated/TaskType.hpp"

#include "access/AccessGateway.hpp"
#include "access/CliAdapter.hpp"
#include "CPlane/BusinessMgr.hpp"
#include "DPlane/business/AiChatBus.hpp"
#include "DPlane/business/Router.hpp"
#include "DPlane/service/AiApiAdapter.hpp"
#include "DPlane/service/ServiceGateway.hpp"
#include "DPlane/service/ServiceMgr.hpp"
#include "DPlane/session/SessionData.hpp"
#include "DPlane/session/SessionMgr.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

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

    // ===== 1. 创建所有 EO（按分层顺序：Access → Session → Business → Service）=====

    // --- Access 层 ---
    // CliAdapter 先创建，AccessGateway 构造时接收其地址并向其发 TempConfig
    access::CliAdapter cliAdapter(env.system());
    auto accessGateway = env.createEo<access::AccessGateway>(cliAdapter.myAddress());

    // --- Session 层 ---
    auto sessionData = env.createEo<DPlane::session::SessionData>(accessGateway);
    auto sessionMgr = env.createEo<DPlane::session::SessionMgr>(pool, accessGateway, sessionData);

    // --- Business 层 ---
    // C面
    auto businessMgr = env.createEo<CPlane::BusinessMgr>(sessionMgr);
    // D面
    auto router = env.createEo<DPlane::business::Router>(businessMgr, sessionData);
    auto aiChatBus = env.createEo<DPlane::business::AiChatBus<TaskType::AiChat>>(
        pool, sessionData, businessMgr, router,
        model ? std::string(model) : std::string("default"));

    // --- Service 层 ---
    // C面
    auto serviceMgr = env.createEo<DPlane::service::ServiceMgr>(businessMgr);
    // D面
    auto serviceGateway = env.createEo<DPlane::service::ServiceGateway>(serviceMgr, aiChatBus);
    auto aiApiAdapter = env.createEo<DPlane::service::AiApiAdapter>(
        apiUrl, apiKey, model ? model : "default", router, serviceMgr, serviceGateway);

    // ===== 2. 启动 CLI 前端 =====
    printBanner();
    cliAdapter.showPrompt();
    cliAdapter.run();

    std::cout << "\n[main] goodbye.\n";
    return 0;
}
