#include "LandMainMenuGUI.h"
#include "LandManagerGUI.h"
#include "NewLandGUI.h"
#include "PlayerSettingGUI.h"
#include "common/SimpleLandPicker.h"
#include "pland/PLand.h"
#include "pland/gui/LandTeleportGUI.h"
#include "pland/land/Config.h"
#include "pland/land/repo/LandRegistry.h"
#include "utils/BackUtils.h"

#include <ll/api/form/SimpleForm.h>

namespace land::gui {

void LandMainMenuGUI::sendTo(Player& player) {
    auto localeCode = player.getLocaleCode();

    auto fm = ll::form::SimpleForm{};
    fm.setTitle("§l§d[星辰] §5领地主菜单"_trl(localeCode));
    fm.setContent("§b✧ 欢迎使用星辰领地管理系统 ✧\n\n§7请选择您要执行的操作："_trl(localeCode));

    fm.appendButton("§l§a▶ 新建领地\n§r§8[ 圈占新的专属地块 ]"_trl(localeCode), "textures/ui/anvil_icon", "path", [](Player& pl) {
        NewLandGUI::sendChooseLandDim(pl);
    });

    fm.appendButton("§l§6▶ 管理领地\n§r§8[ 设置权限与各项参数 ]"_trl(localeCode), "textures/ui/icon_spring", "path", [](Player& pl) {
        SimpleLandPicker::sendTo(
            pl,
            PLand::getInstance().getLandRegistry().getLands(pl.getUuid()),
            LandManagerGUI::sendMainMenu,
            gui::back_utils::wrapCallback<sendTo>()
        );
    });

    if (ConfigProvider::isLandTeleportEnabled()
        || PLand::getInstance().getLandRegistry().isOperator(player.getUuid())) {
        fm.appendButton("§l§3▶ 领地传送\n§r§8[ 快速折跃至名下资产 ]"_trl(localeCode), "textures/ui/icon_recipe_nature", "path", [](Player& pl) {
            LandTeleportGUI::sendTo(pl);
        });
    }

    fm.appendButton("§l§e▶ 个人设置\n§r§8[ 偏好与系统选项 ]"_trl(localeCode), "textures/ui/icon_setting", "path", [](Player& pl) {
        PlayerSettingGUI::sendTo(pl);
    });

    fm.appendButton("§l§c✖ 关闭面板"_trl(localeCode), "textures/ui/cancel", "path");
    fm.sendTo(player);
}

}
