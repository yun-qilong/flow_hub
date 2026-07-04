#pragma once

#include "fw/EoTypes.hpp"

#include "caf/all.hpp"

#include <chrono>
#include <utility>

namespace fw
{

class ScopedEo
{
  public:
    explicit ScopedEo(caf::actor_system &sys) : self_(sys) {}

    EoAddress address() const
    {
        return caf::actor_cast<EoAddress>(self_->address());
    }

    EoAddress senderAddress() const
    {
        return caf::actor_cast<EoAddress>(self_->current_sender());
    }

    template <typename Duration, typename... Handlers>
    bool receive_for(Duration timeout, Handlers &&...handlers)
    {
        return self_->receive_for(timeout, std::forward<Handlers>(handlers)...);
    }

  private:
    caf::scoped_actor self_;
};

} // namespace fw
