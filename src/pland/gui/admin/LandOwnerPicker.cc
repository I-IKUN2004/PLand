#include "LandOwnerPicker.h"

#include "pland/gui/common/PaginatedForm.h"
#include "pland/gui/utils/BackUtils.h"
#include "pland/land/Land.h"

#include "ll/api/service/PlayerInfo.h"
#include "ll/api/utils/StringUtils.h"
#include "pland/PLand.h"
#include "pland/gui/common/SimpleInputForm.h"
#include "pland/land/repo/LandRegistry.h"

namespace land {
namespace gui {

struct LandOwnerPicker::Impl : std::enable_shared_from_this<Impl> {
    struct Entry {
        mce::UUID const   mOwner;
        std::string const mLowerName;
        std::string const mDisplayName;
        size_t const      mLandCount;
        explicit Entry(mce::UUID const& owner, std::string const& displayName, size_t landCount)
        : mOwner(owner),
          mLowerName(ll::string_utils::toLowerCase(displayName)),
          mDisplayName(displayName),
          mLandCount(landCount) {}
    };
    std::vector<Entry>                   mEntries;
    std::optional<std::string>           mFuzzyKeyword;
    Callback                             mCallback;
    ll::form::SimpleForm::ButtonCallback mBackTo;

    void _collectEntries() {
        auto& info  = ll::service::PlayerInfo::getInstance();
        auto  lands = PLand::getInstance().getLandRegistry().getLandsByOwner();
        mEntries.reserve(lands.size());
        for (auto const& [owner, landSet] : lands) {
            if (owner == SYSTEM_ACCOUNT_UUID) {
                mEntries.emplace_back(owner, "PLandSystem", landSet.size());
            } else {
                auto entry = info.fromUuid(owner);
                mEntries.emplace_back(owner, entry ? entry->name : owner.asString(), landSet.size());
            }
        }
    }

    void sendSearchForm(Player& player) {
        auto localeCode = player.getLocaleCode();
        SimpleInputForm::sendTo(
            player,
            "§l§d[星辰] §5领主模糊搜索"_trl(localeCode),
            "§7请输入您要查找的玩家名称："_trl(localeCode),
            mFuzzyKeyword.value_or(""),
            [data = shared_from_this()](Player& player, std::string keyword) {
                if (keyword.empty()) {
                    data->mFuzzyKeyword = std::nullopt;
                } else {
                    data->mFuzzyKeyword = ll::string_utils::toLowerCase(keyword);
                }
                data->sendTo(player);
            }
        );
    }

    void buildForm(PaginatedForm& form, std::string const& localeCode) {
        form.setTitle("§l§d[星辰] §5领主选择器"_trl(localeCode));
        form.setContent("§b✧ 全服资产档案库 ✧\n\n§7请选择您要查阅或管理的领主："_trl(localeCode));
        if (mBackTo) {
            back_utils::injectBackButton(form, mBackTo);
        }
        form.appendButton(
            "§l§2 模糊搜索\n§r§8[ 通过玩家名快速定位 ]"_trl(localeCode),
            "textures/ui/magnifyingGlass",
            "path",
            [data = shared_from_this()](Player& player) { data->sendSearchForm(player); }
        );
        for (auto const& entry : mEntries) {
            if (mFuzzyKeyword && entry.mLowerName.find(*mFuzzyKeyword) == std::string::npos) {
                continue;
            }
            form.appendButton(
                "§l§e▶ {} \n§r§8[ 名下资产: {} 处 ]"_trl(localeCode, entry.mDisplayName, entry.mLandCount),
                [data = shared_from_this(), uuid = entry.mOwner](Player& player) { data->mCallback(player, uuid); }
            );
        }
    }

    void sendTo(Player& player) {
        auto localeCode = player.getLocaleCode();
        auto fm         = PaginatedForm{};
        buildForm(fm, localeCode);
        fm.sendTo(player);
    }

    explicit Impl(Callback callback, ll::form::SimpleForm::ButtonCallback backTo)
    : mCallback(std::move(callback)),
      mBackTo(std::move(backTo)) {
        _collectEntries();
    }
};

void LandOwnerPicker::sendTo(Player& player, Callback callback, ll::form::SimpleForm::ButtonCallback backTo) {
    auto impl = std::make_shared<Impl>(std::move(callback), std::move(backTo));
    impl->sendTo(player);
}

}
}
