#include "ISelector.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "pland/Global.h"
#include "pland/PLand.h"
#include "pland/aabb/LandAABB.h"
#include "pland/drawer/DrawHandleManager.h"
#include "pland/gui/NewLandGUI.h"
#include "pland/land/Config.h"
#include "pland/utils/FeedbackUtils.h"
#include "pland/utils/McUtils.h"

namespace land {

ISelector::ISelector(Player& player, LandDimid dimid, bool is3D)
: mPlayer(player.getWeakEntity()),
  mDimid(dimid),
  m3D(is3D) {
    auto localeCode            = player.getLocaleCode();
    mTitlePacket.mTitleText    = "§l§d[星辰] §5{}选区"_trl(localeCode, m3D ? "3D" : "2D");
    mSubTitlePacket.mTitleText = "§7请使用指令 /pland set a 或使用 '{}' 选定点 A"_trl(localeCode, Config::cfg.selector.alias);
}

ISelector::~ISelector() {
    auto player = getPlayer();
    if (!player) {
        return;
    }
    if (mDrawedRange) {
        PLand::getInstance().getDrawHandleManager()->getOrCreateHandle(*player)->remove(mDrawedRange);
    }
}

optional_ref<Player> ISelector::getPlayer() const {
    auto player = mPlayer.tryUnwrap<Mob>();
    if (!player || player->getEntityTypeId() != ActorType::Player) {
        return nullptr;
    }
    return static_cast<Player*>(player.as_ptr());
}

LandDimid ISelector::getDimensionId() const { return mDimid; }

std::optional<BlockPos> ISelector::getPointA() const { return mPointA; }

std::optional<BlockPos> ISelector::getPointB() const { return mPointB; }

void ISelector::setPointA(BlockPos const& point) {
    if (mPointA) {
        mPointA = point;
        onPointAUpdated();
    } else {
        mPointA = point;
        onPointASet();
    }
    if (mPointA && mPointB) {
        onPointABSet();
    }
}

void ISelector::setPointB(BlockPos const& point) {
    if (mPointB) {
        mPointB = point;
        onPointBUpdated();
    } else {
        mPointB = point;
        onPointBSet();
    }
    if (mPointA && mPointB) {
        onPointABSet();
    }
}

void ISelector::setYRange(int start, int end) {
    if (!isPointABSet()) {
        return;
    }
    mPointA->y = start;
    mPointB->y = end;
    if (auto player = getPlayer()) {
        feedback_utils::sendText(
            player,
            "§e[星辰] §a选区高度范围已成功设置为: §f{} §a~ §f{}"_trl(player->getLocaleCode(), mPointA->y, mPointB->y)
        );
    }
}

void ISelector::checkAndSwapY() {
    if (mPointA && mPointB) {
        if (mPointA->y > mPointB->y) std::swap(mPointA->y, mPointB->y);
    }
}

bool ISelector::isPointASet() const { return mPointA.has_value(); }
bool ISelector::isPointBSet() const { return mPointB.has_value(); }
bool ISelector::isPointABSet() const { return mPointA.has_value() && mPointB.has_value(); }
bool ISelector::is3D() const { return m3D; }

void ISelector::sendTitle() const {
    if (auto player = getPlayer()) {
        mTitlePacket.sendTo(*player);
        mSubTitlePacket.sendTo(*player);
    }
}

std::optional<LandAABB> ISelector::newLandAABB() const {
    if (mPointA && mPointB) {
        auto aabb = LandAABB::make(LandPos::make(*mPointA), LandPos::make(*mPointB));
        aabb.fix();
        return aabb;
    }
    return std::nullopt;
}

std::string ISelector::dumpDebugInfo() const {
    return "DimensionId: {}, PointA: {}, PointB: {}, is3D: {}"_tr(
        mDimid,
        mPointA.has_value() ? mPointA->toString() : "nullopt",
        mPointB.has_value() ? mPointB->toString() : "nullopt",
        is3D()
    );
}

void ISelector::onPointASet() {
    if (auto player = getPlayer()) {
        feedback_utils::sendText(player, "§e[星辰] §a已成功选定点 A: §f{}"_trl(player->getLocaleCode(), *mPointA));

        mSubTitlePacket.mTitleText =
            "§7请使用指令 /pland set b 或使用 '{}' 选定点 B"_trl(player->getLocaleCode(), Config::cfg.selector.alias);
    }
}

void ISelector::onPointBSet() {
    if (auto player = getPlayer()) {
        feedback_utils::sendText(player, "§e[星辰] §a已成功选定点 B: §f{}"_trl(player->getLocaleCode(), *mPointB));
    }
}

void ISelector::onPointAUpdated() {
    if (auto player = getPlayer()) {
        feedback_utils::sendText(player, "§e[星辰] §a已更新点 A 坐标: §f{}"_trl(player->getLocaleCode(), *mPointA));
    }
}

void ISelector::onPointBUpdated() {
    if (auto player = getPlayer()) {
        feedback_utils::sendText(player, "§e[星辰] §a已更新点 B 坐标: §f{}"_trl(player->getLocaleCode(), *mPointB));
    }
}

void ISelector::onPointABSet() {
    auto player = getPlayer();
    if (!player) {
        return;
    }

    auto localeCode = player->getLocaleCode();

    mTitlePacket.mTitleText    = "§l§d[星辰] §a选区完成"_trl(localeCode);
    mSubTitlePacket.mTitleText = "§7请在此范围内输入 /pland buy 呼出认购菜单"_trl(localeCode, Config::cfg.selector.alias);

    if (!is3D()) {
        auto dimension = player->getLevel().getDimension(getDimensionId()).lock();
        if (dimension) {
            auto& range = dimension->mHeightRange;

            this->mPointA->y = range->mMin;
            this->mPointB->y = range->mMax;

            onPointConfirmed();
        } else {
            feedback_utils::sendErrorText(player, "§e[星辰] §c底层引擎获取维度数据失败！"_trl(localeCode));
        }
        return;
    }

    checkAndSwapY();
    gui::NewLandGUI::sendConfirmPrecinctsYRange(*player);
}

void ISelector::onPointConfirmed() {
    auto player = getPlayer();
    if (!player) {
        return;
    }

    auto handle = PLand::getInstance().getDrawHandleManager()->getOrCreateHandle(*player);

    if (mDrawedRange) {
        handle->remove(mDrawedRange);
    }
    mDrawedRange = handle->draw(*newLandAABB(), mDimid, mce::Color::GREEN());
}

void ISelector::tick() { sendTitle(); }

}
