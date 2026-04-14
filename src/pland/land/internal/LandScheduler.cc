#include "LandScheduler.h"

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/coro/InterruptableSleep.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/network/packet/SetTitlePacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "pland/Global.h"
#include "pland/PLand.h"
#include "pland/events/player/PlayerMoveEvent.h"
#include "pland/land/Config.h"
#include "pland/land/Land.h"
#include "pland/land/repo/LandRegistry.h"

#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <string>

namespace land::internal {

std::string getDisplayNameStr(std::string const& xuid, std::string const& realName) {
    static std::unordered_map<std::string, std::string> nickCache;
    static auto lastUpdate = std::chrono::steady_clock::time_point::min();
    auto now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate).count() > 10) {
        std::ifstream file("plugins/GwNickName/data.json");
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            std::regex rgx(R"("(\d{16,20})"\s*:\s*"([^"]+)")");
            std::smatch match;
            std::string::const_iterator searchStart(content.cbegin());
            nickCache.clear();
            while (std::regex_search(searchStart, content.cend(), match, rgx)) {
                nickCache[match[1].str()] = match[2].str();
                searchStart = match.suffix().first;
            }
        }
        lastUpdate = now;
    }

    auto it = nickCache.find(xuid);
    if (it != nickCache.end() && it->second != "未命名") {
        return "§e" + it->second + " §r§f(" + realName + ")§r";
    }
    return "§e" + realName + "§r";
}

struct LandScheduler::Impl {
    std::vector<Player*>                   mPlayers{};
    std::unordered_map<Player*, LandDimid> mDimensionMap{};
    std::unordered_map<Player*, LandID>    mLandIdMap{};

    ll::event::ListenerPtr mPlayerJoinServerListener{nullptr};
    ll::event::ListenerPtr mPlayerDisconnectListener{nullptr};
    ll::event::ListenerPtr mPlayerEnterLandListener{nullptr};

    std::shared_ptr<std::atomic<bool>>            mQuit{nullptr};
    std::shared_ptr<ll::coro::InterruptableSleep> mEventSchedulingSleep{nullptr};
    std::shared_ptr<ll::coro::InterruptableSleep> mLandTipSchedulingSleep{nullptr};

    void tickEvent() {
        auto& bus      = ll::event::EventBus::getInstance();
        auto& registry = PLand::getInstance().getLandRegistry();

        auto iter = mPlayers.begin();
        while (iter != mPlayers.end()) {
            try {
                auto player = *iter;

                auto const& currentPos   = player->getPosition();
                int const   currentDimId = player->getDimensionId();

                int&  lastDimId  = mDimensionMap[player];
                auto& lastLandID = mLandIdMap[player];

                auto   land          = registry.getLandAt(currentPos, currentDimId);
                LandID currentLandId = land ? land->getId() : INVALID_LAND_ID;

                if (currentDimId != lastDimId) {
                    if (lastLandID != INVALID_LAND_ID) {
                        bus.publish(event::PlayerLeaveLandEvent{*player, lastLandID});
                    }
                    lastDimId = currentDimId;
                }

                if (currentLandId != lastLandID) {
                    if (lastLandID != INVALID_LAND_ID) {
                        bus.publish(event::PlayerLeaveLandEvent{*player, lastLandID});
                    }
                    if (currentLandId != INVALID_LAND_ID) {
                        bus.publish(event::PlayerEnterLandEvent{*player, currentLandId});
                    }
                    lastLandID = currentLandId;
                }
                ++iter;
            } catch (...) {
                iter = mPlayers.erase(iter);
            }
        }
    }

    void tickLandTip() {
        auto& playerInfo = ll::service::PlayerInfo::getInstance();
        auto& registry   = PLand::getInstance().getLandRegistry();

        SetTitlePacket pkt(SetTitlePacket::TitleType::Actionbar);
        for (auto& [player, landId] : mLandIdMap) {
            if (landId == INVALID_LAND_ID) {
                continue;
            }

            if (!registry.getOrCreatePlayerSettings(player->getUuid()).showBottomContinuedTip) {
                continue;
            }

            auto land = registry.getLand(landId);
            if (!land) {
                continue;
            }

            auto& owner = land->getOwner();

            if (land->isSystemOwned()) {
                pkt.mTitleText = "§l§e[星辰] §f这里是 §c系统 §f的专属领地"_trl(player->getLocaleCode());
            } else if (land->isOwner(player->getUuid())) {
                pkt.mTitleText = "§l§e[星辰] §f您当前正处于领地 §a{}"_trl(player->getLocaleCode(), land->getName());
            } else {
                auto info = playerInfo.fromUuid(owner);
                std::string ownerDisplay;
                if (info.has_value()) {
                    ownerDisplay = getDisplayNameStr(info->xuid, info->name);
                } else {
                    ownerDisplay = "§e" + owner.asString() + "§r";
                }
                
                pkt.mTitleText = "§l§e[星辰] §f这里是 {} §f的私人领地"_trl(
                    player->getLocaleCode(),
                    ownerDisplay
                );
            }

            pkt.sendTo(*player);
        }
    }
};

LandScheduler::LandScheduler() : impl(std::make_unique<Impl>()) {
    auto& bus = ll::event::EventBus::getInstance();

    impl->mQuit                   = std::make_shared<std::atomic<bool>>(false);
    impl->mEventSchedulingSleep   = std::make_shared<ll::coro::InterruptableSleep>();
    impl->mLandTipSchedulingSleep = std::make_shared<ll::coro::InterruptableSleep>();

    impl->mPlayerJoinServerListener =
        bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& ev) {
            auto& player = ev.self();
            if (player.isSimulatedPlayer()) {
                return;
            }
            impl->mPlayers.emplace_back(&player);
        });

    impl->mPlayerDisconnectListener =
        bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& ev) {
            auto& player = ev.self();
            if (player.isSimulatedPlayer()) {
                return;
            }

            auto ptr = &player;
            impl->mDimensionMap.erase(ptr);
            impl->mLandIdMap.erase(ptr);
            std::erase_if(impl->mPlayers, [&ptr](auto* p) { return p == ptr; });
        });

    impl->mPlayerEnterLandListener =
        bus.emplaceListener<event::PlayerEnterLandEvent>([](event::PlayerEnterLandEvent& ev) {
            auto const& conf = ConfigProvider::getNotificationsConfig();
            if (!conf.enterLandTip) {
                return;
            }

            auto& player   = ev.self();
            auto& registry = PLand::getInstance().getLandRegistry();

            if (!registry.getOrCreatePlayerSettings(player.getUuid()).showEnterLandTitle) {
                return;
            }

            auto land = registry.getLand(ev.landId());
            if (!land) {
                return;
            }

            SetTitlePacket title(SetTitlePacket::TitleType::Title);
            SetTitlePacket subTitle(SetTitlePacket::TitleType::Subtitle);

            if (land->isOwner(player.getUuid())) {
                title.mTitleText    = "§l§a" + land->getName();
                subTitle.mTitleText = "§7欢迎回家"_trl(player.getLocaleCode());
            } else {
                title.mTitleText    = "§l§e欢迎来到"_trl(player.getLocaleCode());
                subTitle.mTitleText = "§b" + land->getName();
            }

            title.sendTo(player);
            subTitle.sendTo(player);
        });

    ll::coro::keepThis([quit = impl->mQuit, sleep = impl->mEventSchedulingSleep, this]() -> ll::coro::CoroTask<> {
        while (!quit->load()) {
            co_await sleep->sleepFor(ll::chrono::ticks{5});
            if (quit->load()) {
                break;
            }

            if (impl->mPlayers.empty()) {
                continue;
            }

            try {
                impl->tickEvent();
            } catch (std::exception& e) {
                PLand::getInstance().getSelf().getLogger().error(
                    "An exception occurred while scheduling land events: {}",
                    e.what()
                );
            } catch (...) {
                PLand::getInstance().getSelf().getLogger().error(
                    "An unknown exception occurred while scheduling land events."
                );
            }
        }
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());

    auto& conf = ConfigProvider::getNotificationsConfig();
    if (conf.bottomContinuousTip) {
        ll::coro::keepThis([quit = impl->mQuit, sleep = impl->mLandTipSchedulingSleep, this]() -> ll::coro::CoroTask<> {
            while (!quit->load()) {
                co_await sleep->sleepFor(
                    ConfigProvider::getNotificationsConfig().bottomTipCycle * ll::chrono::ticks{20}
                );
                if (quit->load()) {
                    break;
                }

                if (impl->mLandIdMap.empty()) {
                    continue;
                }

                try {
                    impl->tickLandTip();
                } catch (std::exception& e) {
                    PLand::getInstance().getSelf().getLogger().error(
                        "An exception occurred while scheduling land tip: {}",
                        e.what()
                    );
                } catch (...) {
                    PLand::getInstance().getSelf().getLogger().error(
                        "An unknown exception occurred while scheduling land tip."
                    );
                }
            }
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    }
}

LandScheduler::~LandScheduler() {
    auto& bus = ll::event::EventBus::getInstance();
    bus.removeListener(impl->mPlayerEnterLandListener);
    bus.removeListener(impl->mPlayerJoinServerListener);
    bus.removeListener(impl->mPlayerDisconnectListener);
    impl->mQuit->store(true);
    impl->mEventSchedulingSleep->interrupt(true);
    impl->mLandTipSchedulingSleep->interrupt(true);
    impl->mPlayers.clear();
    impl->mDimensionMap.clear();
    impl->mLandIdMap.clear();
}

}
