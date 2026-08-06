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
#include "DPlane/session/AiAgora.hpp"
#include "DPlane/session/SessionDispatcher.hpp"
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

namespace
{

struct ApiConfig
{
    std::string apiUrl;
    std::string apiKey;
    std::string model;
};

ApiConfig loadApiConfig()
{
    auto *key = std::getenv("FLOWHUB_API_KEY");
    auto *url = std::getenv("FLOWHUB_API_URL");
    auto *model = std::getenv("FLOWHUB_MODEL");

    ApiConfig cfg;
    cfg.apiKey = key != nullptr ? key : "";
    cfg.apiUrl = url != nullptr ? url : "https://api.deepseek.com";
    cfg.model = model != nullptr ? model : "deepseek-v4-flash";
    LG_INFO("FlowHub v0.2.0 — AI Chat  API: %s  Model: %s", cfg.apiUrl.c_str(), cfg.model.c_str());
    return cfg;
}

void buildSystem(fw::EoEnv &env, TaskPool &pool, const ApiConfig &cfg,
                 userAccess::CliAdapter &cliAdapter)
{
    auto accessGateway = env.createEo<userAccess::AccessGateway>(cliAdapter.myAddress());

    auto sessionDispatcher = env.createEo<DPlane::session::SessionDispatcher>(accessGateway);
    auto sessionMgr = env.createEo<CPlane::SessionMgr>(pool, accessGateway);
    env.createEo<DPlane::session::AiAgora>();

    auto businessMgr = env.createEo<CPlane::BusinessMgr>(sessionMgr);
    auto router = env.createEo<DPlane::business::Router>(businessMgr, sessionDispatcher);
    auto aiChatBus = env.createEo<DPlane::business::AiChatBus<TaskType::AiChat>>(
        pool, sessionDispatcher, businessMgr, router);

    auto serviceMgr = env.createEo<DPlane::service::ServiceMgr>(businessMgr);
    auto serviceGateway = env.createEo<DPlane::service::ServiceGateway>(serviceMgr, aiChatBus);
    auto aiApiAdapter = env.createEo<DPlane::service::AiApiAdapter>(
        cfg.apiUrl, cfg.apiKey, cfg.model, router, serviceMgr, serviceGateway);
    cliAdapter.setAiApiAdapterAddr(aiApiAdapter);
}

} // namespace

int main()
{
    fw::EoEnv env;
    TaskPool pool;

    utils::gSysLog() = utils::createSysLog();

    auto cfg = loadApiConfig();

    userAccess::CliAdapter cliAdapter(env.system());
    buildSystem(env, pool, cfg, cliAdapter);

    printBanner();
    cliAdapter.showPrompt();
    cliAdapter.run();

    LG_INFO("[main] goodbye.");
    return 0;
}
