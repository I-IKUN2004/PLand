#include "NewLandGUI.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/ModalForm.h"

#include "mc/world/actor/player/Player.h"

#include "pland/Global.h"
#include "pland/PLand.h"
#include "pland/aabb/LandAABB.h"
#include "pland/land/Config.h"
#include "pland/land/Land.h"
#include "pland/land/repo/LandRegistry.h"
#include "pland/selector/SelectorManager.h"
#include "pland/selector/land/SubLandCreateSelector.h"
#include "pland/service/LandManagementService.h"
#include "pland/service/ServiceLocator.h"
#include "pland/utils/FeedbackUtils.h"
#include "pland/utils/McUtils.h"

#include <string>

using namespace ll::form;

namespace land::gui {

void NewLandGUI::sendChooseLandDim(Player& player) {
    auto localeCode = player.getLocaleCode();

    ModalForm(
        ("§l§d[星辰] §5选择领地维度"_trl(localeCode)),
        "§b✧ 圈地模式选择 ✧\n\n§7请选择您的领地维度模式：\n\n§e[ 2D 模式 ] §f领地将贯穿整个 Y 轴 (从基岩到天空)\n§a[ 3D 模式 ] §f可自由定义 Y 轴的上下限范围"_trl(localeCode),
        "§l§e▶ 2D 贯穿模式"_trl(localeCode),
        "§l§a▶ 3D 自定义模式"_trl(localeCode)
    )
        .sendTo(player, [](Player& pl, ModalFormResult res, FormCancelReason) {
            if (!res.has_value()) {
                return;
            }

            bool is3D = !((bool)res.value());

            auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();

            auto expected = service.requestCreateOrdinaryLand(pl, is3D);
            if (!expected) {
                feedback_utils::sendError(pl, expected.error());
                return;
            }

            feedback_utils::sendText(
                pl,
                "§e[星辰] §a选区功能已开启，请使用指令 /pland set 或 {} 来选择 A/B 点"_trl(
                    pl.getLocaleCode(),
                    ConfigProvider::getSelectionConfig().alias
                )
            );
        });
}

void NewLandGUI::sendConfirmPrecinctsYRange(Player& player, std::string const& exception) {
    auto selector = land::PLand::getInstance().getSelectorManager()->getSelector(player);
    if (!selector) {
        return;
    }
    auto localeCode = player.getLocaleCode();

    CustomForm fm(("§l§d[星辰] §5确认 Y 轴范围"_trl(localeCode)));

    fm.appendLabel("§7请确认或微调您选区的 Y 轴 (高度) 范围。\n§8(若无需修改，可直接下拉点击提交)"_trl(localeCode));

    SubLandCreateSelector* subSelector = nullptr;
    std::shared_ptr<Land>  parentLand  = nullptr;
    if (subSelector = selector->as<SubLandCreateSelector>(); subSelector) {
        if (parentLand = subSelector->getParentLand(); parentLand) {
            auto& aabb = parentLand->getAABB();
            fm.appendLabel(
                "§c⚠ 注意：当前为子领地模式！\n§7子领地的高度必须被包含在父领地内。\n§f父领地 Y 轴限制: §a{} §f~ §a{}"_trl(
                    localeCode,
                    aabb.min.y,
                    aabb.max.y
                )
            );
        }
    }

    fm.appendInput("start", "§l§2▶ 开始 Y 轴 (底部)"_trl(localeCode), "int", std::to_string(selector->getPointA()->y));
    fm.appendInput("end", "§l§2▶ 结束 Y 轴 (顶部)"_trl(localeCode), "int", std::to_string(selector->getPointB()->y));

    fm.appendLabel(exception);

    fm.sendTo(player, [selector, subSelector, parentLand](Player& pl, CustomFormResult res, FormCancelReason) {
        if (!res.has_value()) {
            return;
        }

        std::string start = std::get<std::string>(res->at("start"));
        std::string end   = std::get<std::string>(res->at("end"));

        auto localeCode = pl.getLocaleCode();
        try {
            int64_t startY = std::stoll(start);
            int64_t endY   = std::stoll(end);
            if (startY >= INT32_MAX || startY <= INT32_MIN || endY >= INT32_MAX || endY <= INT32_MIN) {
                sendConfirmPrecinctsYRange(pl, "§e[星辰] §c数值输入异常，请输入正确的 Y 轴范围！"_trl(localeCode));
                return;
            }

            if (startY >= endY) {
                sendConfirmPrecinctsYRange(pl, "§e[星辰] §c高度逻辑错误：开始 Y 轴必须小于结束 Y 轴！"_trl(localeCode));
                return;
            }

            if (subSelector) {
                if (!subSelector || !parentLand) {
                    throw std::runtime_error("subSelector or parentLand is nullptr");
                }

                auto& aabb = parentLand->getAABB();
                if (startY < aabb.min.y || endY > aabb.max.y) {
                    sendConfirmPrecinctsYRange(
                        pl,
                        "§e[星辰] §c越界错误：子领地的高度范围超出了父领地的限制！"_trl(localeCode)
                    );
                    return;
                }
            }

            selector->setYRange(startY, endY);
            selector->onPointConfirmed();
        } catch (...) {
            sendConfirmPrecinctsYRange(pl, "§e[星辰] §c处理失败，请检查输入的 Y 轴坐标是否合法！"_trl(localeCode));
        }
    });
}

}
