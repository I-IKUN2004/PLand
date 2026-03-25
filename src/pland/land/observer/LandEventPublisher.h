#pragma once
#include "ILandObserver.h"


namespace land::observer {

class LandEventPublisher : public ILandObserver {
public:
    ~LandEventPublisher() override = default;

    void
    onOwnerChanged(std::shared_ptr<Land> const& land, mce::UUID const& oldOwner, mce::UUID const& newOwner) override;

    void onMemberAdded(std::shared_ptr<Land> const& land, mce::UUID const& member) override;

    void onMemberRemoved(std::shared_ptr<Land> const& land, mce::UUID const& member) override;

    void onMembersCleared(std::shared_ptr<Land> const& land) override;
};

} // namespace land::observer