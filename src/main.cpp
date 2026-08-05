// src/main.cpp
// FlowHub — AI Chat 功能集成入口

#include "common/TaskPool.hpp"
#include "fw/EoEnv.hpp"
#include "generated/TaskType.hpp"

#include "CPlane/BusinessMgr.hpp"
#include "CPlane/SessionMgr.hpp"
#include "DPlane/business/AiChatBus.hpp"
#include "DPlane/business/Router.hpp"
#include "DPlane/service/AiApiAdapter.hpp"
#include "DPlane/service/ServiceGateway.hpp"
#include "DPlane/service/ServiceMgr.hpp"
#include "DPlane/session/SessionData.hpp"
#include "userAccess/AccessGateway.hpp"
#include "userAccess/CliAdapter.hpp"
#include "utils/SysLog.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

using TaskPool = common::TaskPool;
using TaskType = common::TaskType;

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

    utils::gSysLog() = utils::createSysLog();

    // ---- API 配置 ----
    const char *apiKey = std::getenv("FLOWHUB_API_KEY");
    const char *apiUrl = std::getenv("FLOWHUB_API_URL");
    const char *model = std::getenv("FLOWHUB_MODEL");

    // 默认值：模型固定 DeepSeek v4 flash；API key 不硬编码，由 CLI 每次 /new 输入
    if (apiKey == nullptr)
    {
        apiKey = "";
    }
    if (apiUrl == nullptr)
    {
        apiUrl = "https://api.deepseek.com";
    }
    if (model == nullptr)
    {
        model = "deepseek-v4-flash";
    }

    LG_INFO("FlowHub v0.2.0 — AI Chat  API: %s  Model: %s", apiUrl, model);

    // ===== 1. 创建所有 EO（按分层顺序：Access → Session → Business → Service）=====

    // --- Access 层 ---
    // CliAdapter 先创建，AccessGateway 构造时接收其地址并向其发 TempConfig
    userAccess::CliAdapter cliAdapter(env.system());
    auto accessGateway = env.createEo<userAccess::AccessGateway>(cliAdapter.myAddress());

    // --- Session 层 ---
    auto sessionData = env.createEo<DPlane::session::SessionData>(accessGateway);
    auto sessionMgr = env.createEo<CPlane::SessionMgr>(pool, accessGateway);

    // --- Business 层 ---
    // C面
    auto businessMgr = env.createEo<CPlane::BusinessMgr>(sessionMgr);
    // D面
    auto router = env.createEo<DPlane::business::Router>(businessMgr, sessionData);
    auto aiChatBus = env.createEo<DPlane::business::AiChatBus<TaskType::AiAgora>>(
        pool, sessionData, businessMgr, router,
        model ? std::string(model) : std::string("default"));

    // --- Service 层 ---
    // C面
    auto serviceMgr = env.createEo<DPlane::service::ServiceMgr>(businessMgr);
    // D面
    auto serviceGateway = env.createEo<DPlane::service::ServiceGateway>(serviceMgr, aiChatBus);
    auto aiApiAdapter = env.createEo<DPlane::service::AiApiAdapter>(
        apiUrl, apiKey, model ? model : "default", router, serviceMgr, serviceGateway);
    cliAdapter.setAiApiAdapterAddr(aiApiAdapter);

    // ===== 2. 启动 CLI 前端 =====
    printBanner();
    cliAdapter.showPrompt();
    cliAdapter.run();

    LG_INFO("[main] goodbye.");
    return 0;
}
