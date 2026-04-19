#include "pland/gui/LandManagerGUI.h"
#include "LandTeleportGUI.h"
#include "PermTableEditor.h"
#include "common/OnlinePlayerPicker.h"
#include "common/SimpleInputForm.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "pland/PLand.h"
#include "pland/economy/EconomySystem.h"
#include "pland/gui/common/OnlinePlayerPicker.h"
#include "pland/gui/utils/BackUtils.h"
#include "pland/land/Config.h"
#include "pland/land/Land.h"
#include "pland/land/repo/LandRegistry.h"
#include "pland/service/LandHierarchyService.h"
#include "pland/service/LandManagementService.h"
#include "pland/service/LandPriceService.h"
#include "pland/service/LeasingService.h"
#include "pland/service/ServiceLocator.h"
#include "pland/utils/FeedbackUtils.h"
#include "pland/utils/TimeUtils.h"

#include <string>
#include <utility>
#include <vector>

namespace land::gui {
using namespace ll::form;

void LandManagerGUI::sendMainMenu(Player& player, std::shared_ptr<Land> land) {
    auto fm = SimpleForm{};

    auto localeCode = player.getLocaleCode();
    fm.setTitle(("§l§d[星辰] §5领地管理 [ID:{}]"_trl(localeCode, land->getId())));

    auto& service = PLand::getInstance().getServiceLocator().getLandHierarchyService();

    std::string subContent;
    if (land->isParentLand()) {
        subContent = "§b下属子领地数量: §f{}"_trl(localeCode, land->getSubLandIDs().size());
    } else if (land->isMixLand()) {
        subContent = "§b下属子领地数量: §f{}\n§b父领地ID: §f{}\n§b父领地名称: §f{}"_trl(
            localeCode,
            land->getSubLandIDs().size(),
            service.getParent(land)->getId(),
            service.getParent(land)->getName()
        );
    } else {
        subContent = "§b父领地ID: §f{}\n§b父领地名称: §f{}"_trl(
            localeCode,
            land->hasParentLand() ? (std::to_string(service.getParent(land)->getId())) : "null",
            land->hasParentLand() ? service.getParent(land)->getName() : "null"
        );
    }

    std::string leaseContent;
    if (land->isLeased()) {
        auto state = land->getLeaseState();
        switch (state) {
        case LeaseState::None:
            break;
        case LeaseState::Active: {
            leaseContent =
                "§b租赁状态: §a正常\n§b剩余租期: §e{}"_trl(localeCode, time_utils::formatRemaining(land->getLeaseEndAt()));
            break;
        }
        case LeaseState::Frozen: {
            auto& priceService = PLand::getInstance().getServiceLocator().getLandPriceService();
            auto  detail       = priceService.calculateRenewCost(land, 0);
            leaseContent       = "§b租赁状态: §c已冻结\n§b欠费金额: §c{}"_trl(localeCode, detail.total);
            break;
        }
        case LeaseState::Expired: {
            leaseContent = "§b租赁状态: §4已到期(系统回收)"_trl(localeCode);
            break;
        }
        }
    }

    fm.setContent(
        "§e[ 领地基础档案 ]\n§b领地名称: §f{}\n§b领地类型: §f{}\n§b体积规模: §f{}x{}x{} = {}\n§b圈地范围: §f{}\n\n§e[ 附加信息 ]\n{}\n{}"_trl(
            localeCode,
            land->getName(),
            land->is3D() ? "3D" : "2D",
            land->getAABB().getBlockCountX(),
            land->getAABB().getBlockCountZ(),
            land->getAABB().getBlockCountY(),
            land->getAABB().getVolume(),
            land->getAABB().toString(),
            leaseContent,
            subContent
        )
    );

    bool const isAdmin     = PLand::getInstance().getLandRegistry().isOperator(player.getUuid());
    bool const canOperLand = isAdmin ||                             
                             !land->isLeased() ||                   
                             land->isLeaseActive();                 

    if (canOperLand) {
        fm.appendButton("§l§a▶ 编辑权限\n§r§8[ 设置各项交互权限 ]"_trl(localeCode), "textures/ui/sidebar_icons/promotag", "path", [land](Player& pl) {
            sendEditLandPermGUI(pl, land);
        });
        fm.appendButton("§l§2▶ 修改成员\n§r§8[ 添加或移除信任成员 ]"_trl(localeCode), "textures/ui/FriendsIcon", "path", [land](Player& pl) {
            sendChangeMember(pl, land);
        });
        fm.appendButton("§l§6▶ 修改领地名称\n§r§8[ 自定义专属地块名 ]"_trl(localeCode), "textures/ui/book_edit_default", "path", [land](Player& pl) {
            sendEditLandNameGUI(pl, land);
        });
    }

    if (land->isLeased()) {
        fm.appendButton("§l§g▶ 续费/缴费\n§r§8[ 缴纳租金以维持领地 ]"_trl(localeCode), "textures/ui/MCoin", "path", [land](Player& pl) {
            sendLeaseRenewGUI(pl, land);
        });
    }

    if (canOperLand) {
        if (ConfigProvider::isLandTeleportEnabled() || isAdmin) {
            fm.appendButton("§l§3▶ 传送到领地\n§r§8[ 快速折跃至该领地 ]"_trl(localeCode), "textures/ui/icon_recipe_nature", "path", [land](Player& pl) {
                LandTeleportGUI::impl(pl, land);
            });

            if (land->getAABB().hasPos(player.getPosition())) {
                fm.appendButton(
                    "§l§b▶ 设置传送点\n§r§8[ 将脚下设为传送点 ]"_trl(localeCode),
                    "textures/ui/Add-Ons_Nav_Icon36x36",
                    "path",
                    [land](Player& pl) {
                        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
                        if (auto res = service.setLandTeleportPos(pl, land, pl.getPosition())) {
                            feedback_utils::notifySuccess(pl, "§e[星辰] §a传送点已成功设置!"_trl(pl.getLocaleCode()));
                        } else {
                            feedback_utils::sendError(pl, res.error());
                        }
                    }
                );
            }
        }

        fm.appendButton(
            "§l§5▶ 领地过户\n§r§8[ 将该领地转让给他人 ]"_trl(localeCode),
            "textures/ui/sidebar_icons/my_characters",
            "path",
            [land](Player& pl) { sendTransferLandGUI(pl, land); }
        );

        if (Config::ensureSubLandFeatureEnabled() && land->canCreateSubLand()) {
            fm.appendButton("§l§d▶ 创建子领地\n§r§8[ 在当前范围内划分分区 ]"_trl(localeCode), "textures/ui/icon_recipe_nature", "path", [land](Player& pl) {
                sendCreateSubLandConfirm(pl, land);
            });
        }

        if (land->isOrdinaryLand() && !land->isLeased()) {
            fm.appendButton("§l§e▶ 重新选区\n§r§8[ 重新框选领地边界 ]"_trl(localeCode), "textures/ui/anvil_icon", "path", [land](Player& pl) {
                sendChangeRangeConfirm(pl, land);
            });
        }

        fm.appendButton("§l§c✖ 删除领地\n§r§8[ 永久注销该领地资产 ]"_trl(localeCode), "textures/ui/icon_trash", "path", [land](Player& pl) {
            showRemoveConfirm(pl, land);
        });
    }

    fm.sendTo(player);
}

void LandManagerGUI::sendLeaseRenewGUI(Player& player, std::shared_ptr<Land> const& land) {
    if (!land || !land->isLeased()) {
        return;
    }

    auto localeCode = player.getLocaleCode();

    std::string status;
    if (land->isLeaseFrozen()) {
        auto& priceService = PLand::getInstance().getServiceLocator().getLandPriceService();
        auto  detail       = priceService.calculateRenewCost(land, 0);
        status             = "§b当前状态: §c已冻结\n§b欠费金额: §c{}"_trl(localeCode, detail.total);
    } else {
        status = "§b当前状态: §a正常\n§b剩余到期时间: §e{}"_trl(localeCode, time_utils::formatRemaining(land->getLeaseEndAt()));
    }

    auto const& duration = ConfigProvider::getLeasingConfig().duration;

    CustomForm fm{"§l§d[星辰] §5租赁续费"_trl(localeCode)};
    fm.appendLabel(status);
    fm.appendSlider(
        "days",
        "§l§2▶ 续租天数"_trl(localeCode),
        duration.minPeriod,
        duration.maxPeriod,
        1.0,
        duration.minPeriod
    );
    fm.setSubmitButton("§l§a▶ 下一步"_trl(localeCode));
    fm.sendTo(player, [land](Player& pl, CustomFormResult const& res, FormCancelReason) {
        if (!res) {
            return;
        }

        auto days = static_cast<int>(std::get<double>(res->at("days")));
        confirmRenewDuration(pl, land, days);
    });
}

void LandManagerGUI::confirmRenewDuration(Player& player, std::shared_ptr<Land> const& land, int days) {
    auto  localeCode   = player.getLocaleCode();
    auto& priceService = PLand::getInstance().getServiceLocator().getLandPriceService();
    auto  detail       = priceService.calculateRenewCost(land, days);

    std::string content = "§b续租天数: §f{}\n§b每日租金: §f{}\n§b欠费天数: §c{}\n§b滞纳金: §c{}\n\n§8[ 结算明细 ]\n§7总计应付: §e{}\n§6{}"_trl(
        localeCode,
        days,
        detail.dailyRent,
        detail.overdueDays,
        detail.penalty,
        detail.total,
        EconomySystem::getInstance().getCostMessage(player, detail.total)
    );

    auto confirm = SimpleForm{};
    confirm.setTitle("§l§d[星辰] §5确认续租订单"_trl(localeCode));
    confirm.setContent(content);
    confirm
        .appendButton("§l§a✔ 确认续租"_trl(localeCode), "textures/ui/realms_green_check", "path", [land, days](Player& pl2) {
            auto& service = PLand::getInstance().getServiceLocator().getLeasingService();
            if (auto exp = service.renewLease(pl2, land, days)) {
                feedback_utils::notifySuccess(pl2, "§e[星辰] §a续租成功！"_trl(pl2.getLocaleCode()));
            } else {
                feedback_utils::sendError(pl2, exp.error());
            }
        });
    back_utils::injectBackButton(confirm, back_utils::wrapCallback<sendLeaseRenewGUI>(land));
    confirm.sendTo(player);
}

void LandManagerGUI::sendEditLandPermGUI(Player& player, std::shared_ptr<Land> const& ptr) {
    PermTableEditor::sendTo(
        player,
        ptr->getPermTable(),
        [ptr](Player& self, LandPermTable newTable) {
            ptr->setPermTable(newTable);
            feedback_utils::sendText(self, "§e[星辰] §a权限表已更新！"_trl(self.getLocaleCode()));
        },
        back_utils::wrapCallback<sendMainMenu>(ptr)
    );
}

void LandManagerGUI::showRemoveConfirm(Player& player, std::shared_ptr<Land> const& ptr) {
    switch (ptr->getType()) {
    case LandType::Ordinary:
    case LandType::Sub:
        confirmSimpleDelete(player, ptr);
        break;
    case LandType::Parent:
        confirmParentDelete(player, ptr);
        break;
    case LandType::Mix:
        confirmMixDelete(player, ptr);
        break;
    }
}

void LandManagerGUI::confirmSimpleDelete(Player& player, std::shared_ptr<Land> const& ptr) {
    if (!ptr->isOrdinaryLand() && !ptr->isSubLand()) {
        return;
    }
    auto localeCode = player.getLocaleCode();
    auto refund =
        ptr->isLeased() ? 0 : PLand::getInstance().getServiceLocator().getLandPriceService().getRefundAmount(ptr);
    auto content =
        ptr->isLeased()
            ? "§c⚠ 危险操作警告 ⚠\n\n§f您确定要删除领地 §e\"{}\" §f吗?\n§7租赁领地删除后 §c不会 §7退还任何租金。\n\n§4此操作完全不可逆，请极其谨慎地抉择!"_trl(
                  localeCode,
                  ptr->getName()
              )
            : "§c⚠ 危险操作警告 ⚠\n\n§f您确定要删除领地 §e\"{}\" §f吗?\n§7删除领地后，系统将为您退还 §a{} §7金币。\n\n§4此操作完全不可逆，请极其谨慎地抉择!"_trl(
                  localeCode,
                  ptr->getName(),
                  refund
              );

    SimpleForm fm;
    fm.setTitle("§l§d[星辰] §4确认注销领地?"_trl(localeCode));
    fm.setContent(content);
    fm.appendButton("§l§c✔ 确认删除"_trl(localeCode), "textures/ui/realms_green_check", "path", [ptr](Player& pl) {
        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
        if (auto expected = service.deleteLand(pl, ptr, service::DeletePolicy::CurrentOnly)) {
            feedback_utils::notifySuccess(pl, "§e[星辰] §a删除成功！"_trl(pl.getLocaleCode()));
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });
    fm.appendButton("§l§8✖ 返回"_trl(localeCode), "textures/ui/cancel", "path", [ptr](Player& pl) {
        sendMainMenu(pl, ptr);
    });
    fm.sendTo(player);
}

void LandManagerGUI::confirmParentDelete(Player& player, std::shared_ptr<Land> const& ptr) {
    auto fm = SimpleForm{};
    gui::back_utils::injectBackButton<sendMainMenu>(fm, ptr);

    auto localeCode = player.getLocaleCode();
    fm.setTitle("§l§d[星辰] §4删除领地 & 父领地"_trl(localeCode));
    fm.setContent(
        "§c⚠ 危险操作警告 ⚠\n\n§f您当前正在操作的是 §e父领地§f！\n§7该领地内包含了 §b{} §7个子领地。\n\n§4请选择您的删除策略："_trl(
            localeCode,
            ptr->getSubLandIDs().size()
        )
    );

    fm.appendButton("§l§c▶ 连同子领地一并彻底删除"_trl(localeCode), [ptr](Player& pl) {
        auto expected = PLand::getInstance().getServiceLocator().getLandManagementService().deleteLand(
            pl,
            ptr,
            service::DeletePolicy::Recursive
        );
        if (expected) {
            feedback_utils::notifySuccess(pl, "§e[星辰] §a删除成功！"_trl(pl.getLocaleCode()));
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });
    fm.appendButton("§l§6▶ 仅删除父级并提升子领地为独立领地"_trl(localeCode), [ptr](Player& pl) {
        auto expected = PLand::getInstance().getServiceLocator().getLandManagementService().deleteLand(
            pl,
            ptr,
            service::DeletePolicy::PromoteChildren
        );
        if (expected) {
            feedback_utils::notifySuccess(pl, "§e[星辰] §a删除成功！"_trl(pl.getLocaleCode()));
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });

    fm.sendTo(player);
}

void LandManagerGUI::confirmMixDelete(Player& player, std::shared_ptr<Land> const& ptr) {
    auto localeCode = player.getLocaleCode();

    auto fm = SimpleForm{};
    gui::back_utils::injectBackButton<sendMainMenu>(fm, ptr);

    fm.setTitle("§l§d[星辰] §4删除领地 & 混合领地"_trl(localeCode));
    fm.setContent(
        "§c⚠ 危险操作警告 ⚠\n\n§f您当前正在操作的是 §e混合领地§f！\n§7该领地内包含了 §b{} §7个子领地。\n\n§4请选择您的删除策略："_trl(
            localeCode,
            ptr->getSubLandIDs().size()
        )
    );

    fm.appendButton("§l§c▶ 连同子领地一并彻底删除"_trl(localeCode), [ptr](Player& pl) {
        auto expected = PLand::getInstance().getServiceLocator().getLandManagementService().deleteLand(
            pl,
            ptr,
            service::DeletePolicy::Recursive
        );
        if (expected) {
            feedback_utils::notifySuccess(pl, "§e[星辰] §a删除成功！"_trl(pl.getLocaleCode()));
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });
    fm.appendButton("§l§6▶ 仅删除本级并将其子领地移交给上级"_trl(localeCode), [ptr](Player& pl) {
        auto expected = PLand::getInstance().getServiceLocator().getLandManagementService().deleteLand(
            pl,
            ptr,
            service::DeletePolicy::TransferChildren
        );
        if (expected) {
            feedback_utils::notifySuccess(pl, "§e[星辰] §a删除成功！"_trl(pl.getLocaleCode()));
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });

    fm.sendTo(player);
}

void LandManagerGUI::sendEditLandNameGUI(Player& player, std::shared_ptr<Land> const& ptr) {
    auto localeCode = player.getLocaleCode();

    gui::SimpleInputForm::sendTo(
        player,
        "§l§d[星辰] §5修改领地名称"_trl(localeCode),
        "§7请输入您想要的全新领地名称："_trl(localeCode),
        ptr->getName(),
        [ptr](Player& pl, std::string result) {
            auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
            if (auto expected = service.setLandName(pl, ptr, result)) {
                feedback_utils::sendText(pl, "§e[星辰] §a领地名称已更新为 {} !"_trl(pl.getLocaleCode(), result));
            } else {
                feedback_utils::sendError(pl, expected.error());
            }
        }
    );
}

void LandManagerGUI::sendTransferLandGUI(Player& player, std::shared_ptr<Land> const& ptr) {
    auto localeCode = player.getLocaleCode();

    auto fm = SimpleForm{};
    gui::back_utils::injectBackButton<sendMainMenu>(fm, ptr);

    fm.setTitle("§l§d[星辰] §5领地过户中心"_trl(localeCode));
    fm.appendButton(
        "§l§a▶ 过户给在线玩家\n§r§8[ 从当前在线列表中选择 ]"_trl(localeCode),
        "textures/ui/sidebar_icons/my_characters",
        "path",
        [ptr](Player& self) { _sendTransferLandToOnlinePlayer(self, ptr); }
    );

    fm.appendButton(
        "§l§6▶ 过户给离线玩家\n§r§8[ 通过输入精确ID进行转让 ]"_trl(localeCode),
        "textures/ui/sidebar_icons/my_characters",
        "path",
        [ptr](Player& self) { _sendTransferLandToOfflinePlayer(self, ptr); }
    );

    fm.sendTo(player);
}

void LandManagerGUI::_sendTransferLandToOnlinePlayer(Player& player, const std::shared_ptr<Land>& ptr) {
    gui::OnlinePlayerPicker::sendTo(
        player,
        [ptr](Player& self, Player& target) {
            _confirmTransferLand(self, ptr, target.getUuid(), target.getRealName());
        },
        gui::back_utils::wrapCallback<sendTransferLandGUI>(ptr)
    );
}

void LandManagerGUI::_sendTransferLandToOfflinePlayer(Player& player, std::shared_ptr<Land> const& ptr) {
    auto localeCode = player.getLocaleCode();

    CustomForm fm("§l§d[星辰] §5过户给离线玩家"_trl(localeCode));
    fm.appendInput("playerName", "§l§2▶ 请输入目标玩家的精确名称"_trl(localeCode), "玩家名称");
    fm.sendTo(player, [ptr](Player& self, CustomFormResult const& res, FormCancelReason) {
        if (!res) {
            return;
        }
        auto localeCode = self.getLocaleCode();

        auto playerName = std::get<std::string>(res->at("playerName"));
        if (playerName.empty()) {
            feedback_utils::sendErrorText(self, "§e[星辰] §c玩家名称不能为空!"_trl(localeCode));
            sendTransferLandGUI(self, ptr);
            return;
        }

        auto playerInfo = ll::service::PlayerInfo::getInstance().fromName(playerName);
        if (!playerInfo) {
            feedback_utils::sendErrorText(self, "§e[星辰] §c未找到该玩家信息，请检查名称是否正确!"_trl(localeCode));
            sendTransferLandGUI(self, ptr);
            return;
        }
        auto& targetUuid = playerInfo->uuid;
        _confirmTransferLand(self, ptr, targetUuid, playerName);
    });
}

void LandManagerGUI::_confirmTransferLand(
    Player&                     player,
    const std::shared_ptr<Land>& ptr,
    mce::UUID                   target,
    std::string                 displayName
) {
    auto localeCode = player.getLocaleCode();

    SimpleForm fm;
    fm.setTitle("§l§d[星辰] §5确认领地过户"_trl(localeCode));
    fm.setContent("§c⚠ 核心资产转移警告 ⚠\n\n§f您即将把该领地的所有权转让给玩家: §e{}\n\n§7过户完成后，您将彻底失去对该领地的最高控制权。\n§4此操作完全不可逆，请极其谨慎地抉择!"_trl(
            localeCode,
            displayName
        ));
    fm.appendButton("§l§a✔ 确认过户"_trl(localeCode), "textures/ui/realms_green_check", "path", [ptr, target, displayName](Player& pl) {
        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
        if (auto expected = service.transferLand(pl, ptr, target)) {
            auto localeCode = pl.getLocaleCode();
            feedback_utils::sendText(pl, "§e[星辰] §a领地已成功转让给 {}"_trl(localeCode, displayName));
            if (auto targetPlayer = pl.getLevel().getPlayer(target)) {
                feedback_utils::sendText(
                    *targetPlayer,
                    "§e[星辰] §a您已接手来自 \"{}\" 的领地 \"{}\""_trl(localeCode, pl.getRealName(), ptr->getName())
                );
            }
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });
    fm.appendButton("§l§8✖ 取消返回"_trl(localeCode), "textures/ui/cancel", "path", [ptr](Player& pl) {
        sendTransferLandGUI(pl, ptr);
    });
    fm.sendTo(player);
}

void LandManagerGUI::sendCreateSubLandConfirm(Player& player, const std::shared_ptr<Land>& ptr) {
    auto localeCode = player.getLocaleCode();

    SimpleForm fm;
    fm.setTitle("§l§d[星辰] §5子领地划拨确认"_trl(localeCode));
    fm.setContent("§b✧ 子领地划分说明 ✧\n\n§7您即将在当前领地内部圈占一块全新区域。\n\n§e子领地特性：\n§f• 物理范围被限制在当前主领地内\n§f• 拥有完全独立的权限与成员系统\n\n§a是否确认开启选区？"_trl(localeCode));
    fm.appendButton("§l§a✔ 开启选区"_trl(localeCode), "textures/ui/realms_green_check", "path", [ptr](Player& pl) {
        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
        if (auto expected = service.requestCreateSubLand(pl)) {
            feedback_utils::sendText(
                pl,
                "§e[星辰] §a选区功能已开启，使用命令 /pland set 或使用 {} 来选择ab点"_trl(
                    pl.getLocaleCode(),
                    ConfigProvider::getSelectionConfig().alias
                )
            );
        } else {
            feedback_utils::sendError(pl, expected.error());
        }
    });
    fm.appendButton("§l§8✖ 取消"_trl(localeCode), "textures/ui/cancel", "path", [ptr](Player& pl) {
        LandManagerGUI::sendMainMenu(pl, ptr);
    });
    fm.sendTo(player);
}

void LandManagerGUI::sendChangeRangeConfirm(Player& player, std::shared_ptr<Land> const& ptr) {
    auto localeCode = player.getLocaleCode();

    SimpleForm fm;
    fm.setTitle("§l§d[星辰] §5领地范围重置"_trl(localeCode));
    fm.setContent("§b✧ 重新选区说明 ✧\n\n§7重新选区意味着您需要完全重新框选 A/B 两点，而不是简单的边缘微调。\n\n§e结算规则：\n§f系统将按照 [新选区价格] - [旧选区残值] 进行多退少补。\n\n§a是否确认开启选区？"_trl(localeCode));
    fm.appendButton("§l§a✔ 开启选区"_trl(localeCode), "textures/ui/realms_green_check", "path", [ptr](Player& self) {
        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
        if (auto expected = service.requestChangeRange(self, ptr)) {
            feedback_utils::sendText(
                self,
                "§e[星辰] §a选区功能已开启，使用命令 /pland set 或使用 {} 来选择ab点"_trl(
                    self.getLocaleCode(),
                    ConfigProvider::getSelectionConfig().alias
                )
            );
        } else {
            feedback_utils::sendError(self, expected.error());
        }
    });
    fm.appendButton("§l§8✖ 取消"_trl(localeCode), "textures/ui/cancel", "path", [ptr](Player& self) {
        LandManagerGUI::sendMainMenu(self, ptr);
    });
    fm.sendTo(player);
}

void LandManagerGUI::sendChangeMember(Player& player, std::shared_ptr<Land> ptr) {
    auto fm = SimpleForm{};
    gui::back_utils::injectBackButton<sendMainMenu>(fm, ptr);

    auto localeCode = player.getLocaleCode();
    fm.appendButton("§l§a▶ 添加在线成员\n§r§8[ 从当前在线列表中选择 ]"_trl(localeCode), "textures/ui/color_plus", "path", [ptr](Player& self) {
        _sendAddOnlineMember(self, ptr);
    });
    fm.appendButton("§l§6▶ 添加离线成员\n§r§8[ 通过输入精确ID进行添加 ]"_trl(localeCode), "textures/ui/color_plus", "path", [ptr](Player& self) {
        _sendAddOfflineMember(self, ptr);
    });

    auto& infos = ll::service::PlayerInfo::getInstance();
    for (auto& member : ptr->getMembers()) {
        auto i = infos.fromUuid(member);
        fm.appendButton("§l§c✖ 移除 §r§0" + (i.has_value() ? i->name : member.asString()), [member, ptr](Player& self) {
            _confirmRemoveMember(self, ptr, member);
        });
    }

    fm.sendTo(player);
}

void LandManagerGUI::_sendAddOnlineMember(Player& player, std::shared_ptr<Land> ptr) {
    gui::OnlinePlayerPicker::sendTo(
        player,
        [ptr](Player& self, Player& target) { _confirmAddMember(self, ptr, target.getUuid(), target.getRealName()); },
        gui::back_utils::wrapCallback<sendChangeMember>(ptr)
    );
}

void LandManagerGUI::_sendAddOfflineMember(Player& player, std::shared_ptr<Land> ptr) {
    auto localeCode = player.getLocaleCode();

    CustomForm fm("§l§d[星辰] §5添加离线成员"_trl(localeCode));
    fm.appendInput("playerName", "§l§2▶ 请输入目标玩家的精确名称"_trl(localeCode), "玩家名称");
    fm.sendTo(player, [ptr](Player& self, CustomFormResult const& res, FormCancelReason) {
        if (!res) {
            return;
        }
        auto localeCode = self.getLocaleCode();

        auto playerName = std::get<std::string>(res->at("playerName"));
        if (playerName.empty()) {
            feedback_utils::sendErrorText(self, "§e[星辰] §c玩家名称不能为空!"_trl(localeCode));
            sendChangeMember(self, ptr);
            return;
        }

        auto playerInfo = ll::service::PlayerInfo::getInstance().fromName(playerName);
        if (!playerInfo) {
            feedback_utils::sendErrorText(self, "§e[星辰] §c未找到该玩家信息，请检查名称是否正确!"_trl(localeCode));
            sendChangeMember(self, ptr);
            return;
        }

        auto& targetUuid = playerInfo->uuid;
        _confirmAddMember(self, ptr, targetUuid, playerName);
    });
}

void LandManagerGUI::_confirmAddMember(
    Player&               player,
    std::shared_ptr<Land> ptr,
    mce::UUID             member,
    std::string           displayName
) {
    auto localeCode = player.getLocaleCode();

    SimpleForm fm;
    fm.setTitle("§l§d[星辰] §5确认添加成员"_trl(localeCode));
    fm.setContent("§f您确定要授予玩家 §e{} §f本领地的成员权限吗？"_trl(localeCode, displayName));
    fm.appendButton("§l§a✔ 确认添加"_trl(localeCode), "textures/ui/realms_green_check", "path", [ptr, member](Player& self) {
        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
        if (auto expected = service.addMember(self, ptr, member)) {
            feedback_utils::sendText(self, "§e[星辰] §a添加成员成功!"_trl(self.getLocaleCode()));
        } else {
            feedback_utils::sendError(self, expected.error());
        }
    });
    fm.appendButton("§l§8✖ 取消"_trl(localeCode), "textures/ui/cancel", "path", [ptr](Player& self) {
        sendChangeMember(self, ptr);
    });
    fm.sendTo(player);
}

void LandManagerGUI::_confirmRemoveMember(Player& player, std::shared_ptr<Land> ptr, mce::UUID member) {
    auto info       = ll::service::PlayerInfo::getInstance().fromUuid(member);
    auto localeCode = player.getLocaleCode();

    SimpleForm fm;
    fm.setTitle("§l§d[星辰] §5确认移除成员"_trl(localeCode));
    fm.setContent("§f您确定要褫夺玩家 §e{} §f的领地成员权限吗？"_trl(localeCode, info.has_value() ? info->name : member.asString()));
    fm.appendButton("§l§c✔ 确认移除"_trl(localeCode), "textures/ui/realms_green_check", "path", [ptr, member](Player& self) {
        auto& service = PLand::getInstance().getServiceLocator().getLandManagementService();
        if (auto expected = service.removeMember(self, ptr, member)) {
            feedback_utils::sendText(self, "§e[星辰] §a移除成员成功!"_trl(self.getLocaleCode()));
        } else {
            feedback_utils::sendError(self, expected.error());
        }
    });
    fm.appendButton("§l§8✖ 取消"_trl(localeCode), "textures/ui/cancel", "path", [ptr](Player& self) {
        sendChangeMember(self, ptr);
    });
    fm.sendTo(player);
}

}
