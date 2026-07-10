// src/DPlane/service/ServiceMgr.cpp

#include "DPlane/service/ServiceMgr.hpp"

namespace DPlane::service
{

ServiceMgr::ServiceMgr(fw::EoConfig &cfg, fw::EoAddress businessMgrAddr)
    : fw::EoBase<ServiceMgr>(cfg), businessMgrAddr_(std::move(businessMgrAddr))
{
    sendTo(businessMgrAddr_, common::message::TempConfig{7});
}

void ServiceMgr::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 8)
    {
        serviceGatewayAddr_ = senderAddress();
    }
    else if (msg.tag == 11)
    {
        aiApiAdapterAddr_ = senderAddress();
    }
}

} // namespace DPlane::service
