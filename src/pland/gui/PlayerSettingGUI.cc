#include "PlayerSettingGUI.h"

#include "pland/PLand.h"
#include "pland/land/repo/LandRegistry.h"
#include "pland/utils/FeedbackUtils.h"

#include "mc/world/actor/player/Player.h"

#include "ll/api/form/CustomForm.h"

namespace land::gui {
using namespace ll::form;

void PlayerSettingGUI::sendTo(Player& player) {
    auto& setting = PLand::getInstance().getLandRegistry().getOrCreatePlayerSettings(player.getUuid());

    auto       localeCode = player.getLocaleCode();
    CustomForm fm(("§l§d[星辰] §5个人偏好设置"_trl(localeCode)));

    fm.appendToggle("showEnterLandTitle", "§l§2▶ 显示进入领地提示\n§r§8(跨越边界时屏幕中央弹字)"_trl(localeCode), setting.showEnterLandTitle);
    fm.appendToggle("showBottomContinuedTip", "§l§6▶ 持续显示底部提示\n§r§8(在领地内时底部常驻显示)"_trl(localeCode), setting.showBottomContinuedTip);

    fm.sendTo(player, [&setting](Player& pl, CustomFormResult res, FormCancelReason) {
        if (!res) {
            return;
        }

        setting.showEnterLandTitle     = std::get<uint64_t>(res->at("showEnterLandTitle"));
        setting.showBottomContinuedTip = std::get<uint64_t>(res->at("showBottomContinuedTip"));

        feedback_utils::sendText(pl, "§e[星辰] §a个人偏好设置已成功保存！"_trl(pl.getLocaleCode()));
    });
}

}
