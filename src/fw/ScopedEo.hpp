#pragma once

#include "fw/EoTypes.hpp"

#include "caf/all.hpp"

#include <chrono>
#include <thread>
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

    template <typename Duration>
    bool receive_for(Duration timeout, fw::MessageHandler &mh)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (self_->has_next_message())
            {
                auto ptr = self_->next_message();
                self_->current_mailbox_element(ptr.get());
                mh(ptr->content());
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

  private:
    caf::scoped_actor self_;
};

} // namespace fw
