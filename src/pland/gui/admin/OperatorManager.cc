#include "OperatorManager.h"

#include "LandOwnerPicker.h"
#include "pland/PLand.h"
#include "pland/gui/LandManagerGUI.h"
#include "pland/gui/PermTableEditor.h"
#include "pland/gui/common/AdvancedLandPicker.h"
#include "pland/land/Land.h"
#include "pland/land/LandTemplatePermTable.h"
#include "pland/land/repo/LandContext.h"
#include "pland/land/repo/LandRegistry.h"
#include "pland/utils/FeedbackUtils.h"

#include "ll/api/service/PlayerInfo.h"
#include "pland/gui/common/SimpleInputForm.h"
#include "pland/gui/utils/BackUtils.h"

#include <ll/api/form/SimpleForm.h>

namespace land::gui {

void OperatorManager::sendMainMenu(Player& player) {
    auto localeCode = player.getLocaleCode();

    if (!PLand::getInstance().getLandRegistry().isOperator(player.getUuid())) {
        feedback_utils::sendErrorText(player, "§e[星辰] §c权限不足：您不是领地系统管理员！"_trl(localeCode));
        return;
    }

    auto fm = ll::form::SimpleForm{};

    fm.setTitle("§l§d[星辰] §5OP 领地管理中心"_trl(localeCode));
    fm.setContent("§b✧ 最高权限管理后台 ✧\n\n§7请选择您要执行的管理员操作："_trl(localeCode));

    fm.appendButton("§l§a▶ 管理脚下领地\n§r§8[ 检索当前坐标领地 ]"_trl(localeCode), "textures/ui/free_download", "path", [](Player& self) {
        auto lands = PLand::getInstance().getLandRegistry().getLandAt(self.getPosition(), self.getDimensionId());
        if (!lands) {
            feedback_utils::sendErrorText(self, "§e[星辰] §c未检索到领地：您当前所处的坐标不在任何领地内！"_trl(self.getLocaleCode()));
            return;
        }
        LandManagerGUI::sendMainMenu(self, lands);
    });
    fm.appendButton("§l§6▶ 管理玩家领地\n§r§8[ 查阅指定领主资产 ]"_trl(localeCode), "textures/ui/FriendsIcon", "path", [](Player& self) {
        LandOwnerPicker::sendTo(self, static_cast<void (*)(Player&, mce::UUID)>(&sendAdvancedLandPicker), sendMainMenu);
    });
    fm.appendButton("§l§3▶ 管理指定领地\n§r§8[ 通过全局搜索或ID定位 ]"_trl(localeCode), "textures/ui/magnifyingGlass", "path", [](Player& self) {
        sendLandSelectModeMenu(self);
    });
    fm.appendButton("§l§e▶ 编辑默认权限\n§r§8[ 修改全服初始化权限表 ]"_trl(localeCode), "textures/ui/icon_map", "path", [](Player& self) {
        gui::PermTableEditor::sendTo(
            self,
            PLand::getInstance().getLandRegistry().getLandTemplatePermTable().get(),
            [](Player& self, LandPermTable newTable) {
                PLand::getInstance().getLandRegistry().getLandTemplatePermTable().set(newTable);
                feedback_utils::sendText(self, "§e[星辰] §a全服默认权限表已成功更新！"_trl(self.getLocaleCode()));
            },
            sendMainMenu
        );
    });

    fm.sendTo(player);
}

void OperatorManager::sendLandSelectModeMenu(Player& player) {
    auto localeCode = player.getLocaleCode();

    ll::form::SimpleForm fm;
    fm.setTitle("§l§d[星辰] §5管理指定领地"_trl(localeCode));
    fm.appendButton(
        "§l§2▶ 浏览全部领地\n§r§8[ 展开全服领地档案库 ]"_trl(localeCode),
        "textures/ui/achievements_pause_menu_icon",
        "path",
        [](Player& self) { sendAdvancedLandPicker(self, PLand::getInstance().getLandRegistry().getLands()); }
    );
    fm.appendButton("§l§9▶ 按领地 ID 查找\n§r§8[ 精确搜索指定序列号 ]"_trl(localeCode), "textures/ui/magnifyingGlass", "path", [](Player& self) {
        sendLandIdSearchForm(self);
    });
    back_utils::injectBackButton<sendMainMenu>(fm);
    fm.sendTo(player);
}

void OperatorManager::sendLandIdSearchForm(Player& player) {
    auto localeCode = player.getLocaleCode();
    SimpleInputForm::sendTo(
        player,
        "§l§d[星辰] §5领地精确搜索"_trl(localeCode),
        "§7请输入您要管理的领地序列号 (ID)："_trl(localeCode),
        "",
        [](Player& self, std::string input) {
            auto localeCode = self.getLocaleCode();
            try {
                LandID id = std::stoll(input);
                if (auto land = PLand::getInstance().getLandRegistry().getLand(id)) {
                    LandManagerGUI::sendMainMenu(self, land);
                } else {
                    feedback_utils::sendErrorText(self, "§e[星辰] §c搜索失败：未找到序列号为 {} 的领地！"_trl(localeCode, input));
                }
            } catch (...) {
                feedback_utils::sendErrorText(self, "§e[星辰] §c格式错误：请输入纯数字的合法领地 ID！"_trl(localeCode));
            }
        }
    );
}

void OperatorManager::sendAdvancedLandPicker(Player& player, mce::UUID targetPlayer) {
    sendAdvancedLandPicker(player, PLand::getInstance().getLandRegistry().getLands(targetPlayer));
}

void OperatorManager::sendAdvancedLandPicker(Player& player, std::vector<std::shared_ptr<Land>> lands) {
    AdvancedLandPicker::sendTo(
        player,
        lands,
        [](Player& self, std::shared_ptr<Land> ptr) { LandManagerGUI::sendMainMenu(self, ptr); },
        back_utils::wrapCallback<sendMainMenu>()
    );
}

}
