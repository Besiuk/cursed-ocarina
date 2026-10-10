#include "CursedScript_DebugItems.h"
#include "CursedEquipment_KokiriSword.h"
#include "CursedEquipment_StickSaber.h"
#include "CursedScript_Effects.h"
#include "soh/Enhancements/SwitchAge.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Cursed {

namespace {

// Fills the ammo of every owned item up to the capacity its upgrade allows
void RefillAmmo() {
    struct Entry {
        int item;
        int upgrade;
    };
    const Entry entries[] = {
        { ITEM_BOW, UPG_QUIVER },    { ITEM_SLINGSHOT, UPG_BULLET_BAG }, { ITEM_BOMB, UPG_BOMB_BAG },
        { ITEM_STICK, UPG_STICKS },  { ITEM_NUT, UPG_NUTS },
    };
    for (const Entry& entry : entries) {
        if (INV_CONTENT(entry.item) != ITEM_NONE) {
            AMMO(entry.item) = static_cast<s8>(CUR_CAPACITY(entry.upgrade));
        }
    }
    if (INV_CONTENT(ITEM_BOMBCHU) != ITEM_NONE) {
        AMMO(ITEM_BOMBCHU) = 50;
    }
    if (INV_CONTENT(ITEM_BEAN) != ITEM_NONE) {
        AMMO(ITEM_BEAN) = 10;
    }
}

} // namespace

const std::vector<DebugUpgrade>& GetDebugUpgrades() {
    static const std::vector<DebugUpgrade> upgrades = {
        { "Bullet Bag", UPG_BULLET_BAG, 3, { "None", "30", "40", "50" } },
        { "Quiver", UPG_QUIVER, 3, { "None", "30", "40", "50" } },
        { "Bomb Bag", UPG_BOMB_BAG, 3, { "None", "20", "30", "40" } },
        { "Strength", UPG_STRENGTH, 3, { "None", "Goron Bracelet", "Silver Gauntlets", "Golden Gauntlets" } },
        { "Scale", UPG_SCALE, 2, { "None", "Silver", "Golden" } },
        { "Wallet", UPG_WALLET, 2, { "Child (99)", "Adult (200)", "Giant (500)" } },
        { "Deku Sticks", UPG_STICKS, 3, { "None", "10", "20", "30" } },
        { "Deku Nuts", UPG_NUTS, 3, { "None", "20", "30", "40" } },
    };
    return upgrades;
}

int GetUpgradeLevel(const DebugUpgrade& upgrade) {
    return CUR_UPG_VALUE(upgrade.upgradeId);
}

void ChangeUpgradeLevel(const DebugUpgrade& upgrade, int delta) {
    int level = GetUpgradeLevel(upgrade) + delta;
    if (level < 0) {
        level = 0;
    }
    if (level > upgrade.maxLevel) {
        level = upgrade.maxLevel;
    }
    Inventory_ChangeUpgrade(static_cast<s16>(upgrade.upgradeId), static_cast<s16>(level));
    if (upgrade.upgradeId == UPG_WALLET) {
        const int cap = CUR_CAPACITY(UPG_WALLET);
        if (gSaveContext.rupees > cap) {
            gSaveContext.rupees = static_cast<u16>(cap);
        }
    }
    RefillAmmo();
}

void ResetItemStates() {
    Cursed::RepairKokiriSword();
    Cursed::ResetStickSaber();
}

void GiveAllEquipment() {
    // A fresh set of equipment also means an intact Kokiri sword
    Cursed::RepairKokiriSword();

    // Inventory items (one slot each)
    const int items[] = {
        ITEM_STICK,      ITEM_NUT,        ITEM_BOMB,        ITEM_BOW,       ITEM_ARROW_FIRE, ITEM_DINS_FIRE,
        ITEM_SLINGSHOT,  ITEM_OCARINA_TIME, ITEM_BOMBCHU,   ITEM_LONGSHOT,  ITEM_ARROW_ICE,  ITEM_FARORES_WIND,
        ITEM_BOOMERANG,  ITEM_LENS,       ITEM_BEAN,        ITEM_HAMMER,    ITEM_ARROW_LIGHT, ITEM_NAYRUS_LOVE,
    };
    for (const int item : items) {
        INV_CONTENT(item) = static_cast<u8>(item);
    }
    for (int slot = SLOT_BOTTLE_1; slot < SLOT_BOTTLE_1 + 4; slot++) {
        gSaveContext.inventory.items[slot] = ITEM_BOTTLE;
    }

    // Equipment: all swords, shields, tunics and boots
    for (int i = EQUIP_INV_SWORD_KOKIRI; i <= EQUIP_INV_SWORD_BIGGORON; i++) {
        gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, i);
    }
    for (int i = EQUIP_INV_SHIELD_DEKU; i <= EQUIP_INV_SHIELD_MIRROR; i++) {
        gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, i);
    }
    for (int i = EQUIP_INV_TUNIC_KOKIRI; i <= EQUIP_INV_TUNIC_ZORA; i++) {
        gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_TUNIC, i);
    }
    for (int i = EQUIP_INV_BOOTS_KOKIRI; i <= EQUIP_INV_BOOTS_HOVER; i++) {
        gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_BOOTS, i);
    }
    gSaveContext.bgsFlag = 1;
    gSaveContext.swordHealth = 8;

    // Magic, so Din's Fire, arrows and friends can be used right away
    gSaveContext.magicCapacity = gSaveContext.magic = 0x60;
    gSaveContext.magicLevel = 2;
    gSaveContext.isMagicAcquired = true;
    gSaveContext.isDoubleMagicAcquired = true;

    RefillAmmo();
    Notify("All equipment given");
}

void DebugSwitchAge() {
    if (gPlayState == nullptr) {
        Notify("No game loaded");
        return;
    }
    SwitchAge();
}

bool DebugIsAdult() {
    return gSaveContext.linkAge == LINK_AGE_ADULT;
}

} // namespace Cursed
