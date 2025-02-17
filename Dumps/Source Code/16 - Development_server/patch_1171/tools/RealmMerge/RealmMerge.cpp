/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <string>
#include <iostream>
#include "Database/DatabaseEnv.h"
#include "Log.h"

DatabaseType CharacterDatabase1;
DatabaseType CharacterDatabase2;
std::string g_db1Name;
std::string g_db2Name;

enum AtLoginFlags
{
    AT_LOGIN_NONE              = 0x00,
    AT_LOGIN_RENAME            = 0x01,
    AT_LOGIN_RESET_SPELLS      = 0x02,
    AT_LOGIN_RESET_TALENTS     = 0x04,
    //AT_LOGIN_CUSTOMIZE         = 0x08, -- used in post-3.x
    //AT_LOGIN_RESET_PET_TALENTS = 0x10, -- used in post-3.x
    AT_LOGIN_FIRST             = 0x20,
};

std::string MakeConnectionString()
{
    std::string mysql_host;
    std::string mysql_port;
    std::string mysql_user;
    std::string mysql_pass;
    std::string mysql_db;

    printf("Host: ");
    getline(std::cin, mysql_host);
    if (mysql_host.empty())
        mysql_host = "127.0.0.1";

    printf("Port: ");
    getline(std::cin, mysql_port);
    if (mysql_port.empty())
        mysql_port = "3306";

    printf("User: ");
    getline(std::cin, mysql_user);
    if (mysql_user.empty())
        mysql_user = "root";

    printf("Password: ");
    getline(std::cin, mysql_pass);
    if (mysql_pass.empty())
        mysql_pass = "root";

    return mysql_host + ";" + mysql_port + ";" + mysql_user + ";" + mysql_pass + ";";
}

char GetChar()
{
    fseek(stdin, 0, SEEK_END);
    char const chr = getchar();
    fseek(stdin, 0, SEEK_END);
    return chr;
}

std::string GetString()
{
    std::string text;
    fseek(stdin, 0, SEEK_END);
    getline(std::cin, text);
    fseek(stdin, 0, SEEK_END);
    return text;
}

struct UniqueKeyData
{
    std::set<uint32> existingKeys1;
    std::set<uint32> existingKeys2;
    std::map<uint32, uint32> replacedKeys;
    uint32 counter = 0;

    std::pair<uint32, bool> ReplaceKeyIfNeeded(uint32 key)
    {
        auto itr = replacedKeys.find(key);
        if (itr != replacedKeys.end())
            return std::make_pair(itr->second, false);

        if (existingKeys1.find(key) == existingKeys1.end())
            return std::make_pair(key, false);

        ++counter;
        while (existingKeys1.find(counter) != existingKeys1.end() ||
               existingKeys2.find(counter) != existingKeys2.end())
        {
            ++counter;
        }

        replacedKeys.insert({ key, counter });
        return std::make_pair(counter, true);
    }
};

bool UpdateCharacterGuids()
{
    std::set<std::string> characterNames1;
    std::map<uint32, std::string> characterNames2;
    UniqueKeyData characterGuids;

    sLog.outInfo("Loading %s.characters...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `guid`, `name` FROM `characters`"));

    if (!result)
    {
        sLog.outError("Database %s contains no characters.", g_db1Name.c_str());
        return false;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 guid = fields[0].GetUInt32();
        std::string name = fields[1].GetCppString();

        characterGuids.existingKeys1.insert(guid);
        characterNames1.insert(name);

    } while (result->NextRow());

    sLog.outInfo("Loading %s.characters...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `guid`, `name` FROM `characters`"));

    if (!result)
    {
        sLog.outError("Database %s contains no characters.", g_db2Name.c_str());
        return false;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 guid = fields[0].GetUInt32();
        std::string name = fields[1].GetCppString();

        characterGuids.existingKeys2.insert(guid);
        characterNames2.insert({ guid, name });

    } while (result->NextRow());

    if (INT32_MAX < int64(characterGuids.existingKeys1.size() + characterGuids.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of characters exceeds max value of int32.");
        return false;
    }

    sLog.outInfo("Updating character names...");
    for (auto const& itr : characterNames2)
    {
        if (!itr.second.empty() && characterNames1.find(itr.second) != characterNames1.end())
        {
            sLog.outInfo("Name of character %s (%u) is taken. Marking for rename.", itr.second.c_str(), itr.first);
            CharacterDatabase2.PExecute("UPDATE `characters` SET `name` = guid, `at_login` = (`at_login` | %u) WHERE `guid` = %u", AT_LOGIN_RENAME, itr.first);
        }
    }

    int64 const maxCharGuid1 = *characterGuids.existingKeys1.rbegin();
    int64 const maxCharGuid2 = *characterGuids.existingKeys2.rbegin();
    int64 const maxCharGuid = std::max(maxCharGuid1, maxCharGuid2);

    if (INT32_MAX > int64(maxCharGuid + maxCharGuid2))
    {
        sLog.outInfo("Updating character guids (fast method)...");

        CharacterDatabase2.PExecute("UPDATE `auction` SET `itemowner` = (`itemowner` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `auction` SET `buyguid` = (`buyguid` + %u) WHERE `buyguid` != 0", maxCharGuid);
        sLog.outInfo("- Updated `auction` table");

        CharacterDatabase2.PExecute("UPDATE `characters` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `characters` table");

        CharacterDatabase2.PExecute("UPDATE `character_action` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_action` table");

        CharacterDatabase2.PExecute("UPDATE `character_armory_stats` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_armory_stats` table");

        CharacterDatabase2.PExecute("UPDATE `character_aura` SET `guid` = (`guid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `character_aura` SET `caster_guid` = (`caster_guid` + %u) WHERE `caster_guid` <= %u", maxCharGuid, maxCharGuid2);
        sLog.outInfo("- Updated `character_aura` table");

        CharacterDatabase2.PExecute("UPDATE `character_aura_suspended` SET `guid` = (`guid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `character_aura_suspended` SET `caster_guid` = (`caster_guid` + %u) WHERE `caster_guid` <= %u", maxCharGuid, maxCharGuid2);
        sLog.outInfo("- Updated `character_aura_suspended` table");

        CharacterDatabase2.PExecute("UPDATE `character_battleground_data` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_battleground_data` table");

        CharacterDatabase2.PExecute("UPDATE `character_bgqueue` SET `PlayerGUID` = (`PlayerGUID` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_bgqueue` table");

        CharacterDatabase2.PExecute("UPDATE `character_deleted_items` SET `player_guid` = (`player_guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_deleted_items` table");

        CharacterDatabase2.PExecute("UPDATE `character_destroyed_items` SET `player_guid` = (`player_guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_destroyed_items` table");

        CharacterDatabase2.PExecute("UPDATE `character_egg_loot` SET `playerGuid` = (`playerGuid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_egg_loot` table");

        CharacterDatabase2.PExecute("UPDATE `character_forgotten_skills` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_forgotten_skills` table");

        CharacterDatabase2.PExecute("UPDATE `character_gifts` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_gifts` table");

        CharacterDatabase2.PExecute("UPDATE `character_homebind` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_homebind` table");

        CharacterDatabase2.PExecute("UPDATE `character_honor_cp` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_honor_cp` table");

        CharacterDatabase2.PExecute("UPDATE `character_instance` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_instance` table");

        CharacterDatabase2.PExecute("UPDATE `character_inventory` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_inventory` table");

        CharacterDatabase2.PExecute("UPDATE `character_item_logs` SET `playerLowGuid` = (`playerLowGuid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_item_logs` table");

        CharacterDatabase2.PExecute("UPDATE `character_pet` SET `owner` = (`owner` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_pet` table");

        CharacterDatabase2.PExecute("UPDATE `character_queststatus` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_queststatus` table");

        CharacterDatabase2.PExecute("UPDATE `character_reputation` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_reputation` table");

        CharacterDatabase2.PExecute("UPDATE `character_skills` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_skills` table");

        CharacterDatabase2.PExecute("UPDATE `character_social` SET `guid` = (`guid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `character_social` SET `friend` = (`friend` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_social` table");

        CharacterDatabase2.PExecute("UPDATE `character_spell` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_spell` table");

        CharacterDatabase2.PExecute("UPDATE `character_spell_cooldown` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_spell_cooldown` table");

        CharacterDatabase2.PExecute("UPDATE `character_spell_dual_spec` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_spell_dual_spec` table");

        CharacterDatabase2.PExecute("UPDATE `character_stats` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_stats` table");

        CharacterDatabase2.PExecute("UPDATE `character_ticket` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_ticket` table");

        CharacterDatabase2.PExecute("UPDATE `character_titles` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_titles` table");

        CharacterDatabase2.PExecute("UPDATE `character_transmogs` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_transmogs` table");

        CharacterDatabase2.PExecute("UPDATE `character_variables` SET `lowGuid` = (`lowGuid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_variables` table");

        CharacterDatabase2.PExecute("UPDATE `character_xp_from_log` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `character_xp_from_log` table");

        CharacterDatabase2.PExecute("UPDATE `corpse` SET `player` = (`player` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `corpse` table");

        CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `guid` = (`guid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `closedBy` = (`closedBy` + %u) WHERE `closedBy` != 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `assignedTo` = (`assignedTo` + %u) WHERE `assignedTo` != 0", maxCharGuid);
        sLog.outInfo("- Updated `gm_tickets` table");

        CharacterDatabase2.PExecute("UPDATE `groups` SET `leaderGuid` = (`leaderGuid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `groups` SET `mainTank` = (`mainTank` + %u) WHERE `mainTank` != 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `groups` SET `mainAssistant` = (`mainAssistant` + %u) WHERE `mainAssistant` != 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `groups` SET `looterGuid` = (`looterGuid` + %u) WHERE `looterGuid` != 0", maxCharGuid);
        sLog.outInfo("- Updated `groups` table");

        CharacterDatabase2.PExecute("UPDATE `group_instance` SET `leaderGuid` = (`leaderGuid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `group_instance` table");

        CharacterDatabase2.PExecute("UPDATE `group_member` SET `memberGuid` = (`memberGuid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `group_member` table");

        CharacterDatabase2.PExecute("UPDATE `guild` SET `leaderguid` = (`leaderguid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `guild` table");

        CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `creatorGuid` = (`creatorGuid` + %u) WHERE `creatorGuid` != 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `giftCreatorGuid` = (`giftCreatorGuid` + %u) WHERE `giftCreatorGuid` != 0", maxCharGuid);
        sLog.outInfo("- Updated `guild_bank` table");

        CharacterDatabase2.PExecute("UPDATE `guild_bank_log` SET `player` = (`player` + %u) WHERE `player` != 0", maxCharGuid);
        sLog.outInfo("- Updated `guild_bank_log` table");

        CharacterDatabase2.PExecute("UPDATE `guild_eventlog` SET `PlayerGuid1` = (`PlayerGuid1` + %u) WHERE `PlayerGuid1` != 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `guild_eventlog` SET `PlayerGuid2` = (`PlayerGuid2` + %u) WHERE `PlayerGuid2` != 0", maxCharGuid);
        sLog.outInfo("- Updated `guild_eventlog` table");

        CharacterDatabase2.PExecute("UPDATE `guild_member` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `guild_member` table");

        CharacterDatabase2.PExecute("UPDATE `hardcore_deaths` SET `lowGuid` = (`lowGuid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `hardcore_deaths` table");

        CharacterDatabase2.PExecute("UPDATE `item_instance` SET `owner_guid` = (`owner_guid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `item_instance` SET `creatorGuid` = (`creatorGuid` + %u) WHERE `creatorGuid` != 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `item_instance` SET `giftCreatorGuid` = (`giftCreatorGuid` + %u) WHERE `giftCreatorGuid` != 0", maxCharGuid);
        sLog.outInfo("- Updated `item_instance` table");

        CharacterDatabase2.PExecute("UPDATE `item_loot` SET `owner_guid` = (`owner_guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `item_loot` table");

        CharacterDatabase2.PExecute("UPDATE `logs_movement` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `logs_movement` table");

        CharacterDatabase2.PExecute("UPDATE `logs_warden` SET `guid` = (`guid` + %u) WHERE `guid` != 0", maxCharGuid);
        sLog.outInfo("- Updated `logs_warden` table");

        CharacterDatabase2.PExecute("UPDATE `mail` SET `sender` = (`sender` + %u) WHERE `mailTemplateId` = 0", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `mail` SET `receiver` = (`receiver` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `mail` table");

        CharacterDatabase2.PExecute("UPDATE `mail_items` SET `receiver` = (`receiver` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `mail_items` table");

        CharacterDatabase2.PExecute("UPDATE `petition` SET `ownerguid` = (`ownerguid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `petition` table");

        CharacterDatabase2.PExecute("UPDATE `petition_sign` SET `ownerguid` = (`ownerguid` + %u)", maxCharGuid);
        CharacterDatabase2.PExecute("UPDATE `petition_sign` SET `playerguid` = (`playerguid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `petition_sign` table");

        CharacterDatabase2.PExecute("UPDATE `pet_aura` SET `caster_guid` = (`caster_guid` + %u) WHERE `caster_guid` <= %u", maxCharGuid, maxCharGuid2);
        sLog.outInfo("- Updated `pet_aura` table");

        CharacterDatabase2.PExecute("UPDATE `store_racechange` SET `guid` = (`guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `store_racechange` table");

        CharacterDatabase2.PExecute("UPDATE `whisper_targets` SET `target_guid` = (`target_guid` + %u)", maxCharGuid);
        sLog.outInfo("- Updated `whisper_targets` table");
    }
    else
    {
        sLog.outInfo("Updating character guids (slow method)...");
        for (auto const& guid : characterGuids.existingKeys2)
        {
            std::pair<uint32, bool> newGuid = characterGuids.ReplaceKeyIfNeeded(guid);
            if (newGuid.second)
            {
                sLog.outInfo("Guid of character %u is taken. Updating to %u.", guid, newGuid.first);
                CharacterDatabase2.PExecute("UPDATE `auction` SET `itemowner` = %u WHERE `itemowner` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `auction` SET `buyguid` = %u WHERE `buyguid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `characters` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_action` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_armory_stats` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_aura` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_aura` SET `caster_guid` = %u WHERE `caster_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_aura_suspended` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_aura_suspended` SET `caster_guid` = %u WHERE `caster_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_battleground_data` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_bgqueue` SET `PlayerGUID` = %u WHERE `PlayerGUID` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_deleted_items` SET `player_guid` = %u WHERE `player_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_destroyed_items` SET `player_guid` = %u WHERE `player_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_egg_loot` SET `playerGuid` = %u WHERE `playerGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_forgotten_skills` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_gifts` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_homebind` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_honor_cp` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_instance` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_inventory` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_item_logs` SET `playerLowGuid` = %u WHERE `playerLowGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_pet` SET `owner` = %u WHERE `owner` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_queststatus` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_reputation` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_skills` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_social` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_social` SET `friend` = %u WHERE `friend` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_spell` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_spell_cooldown` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_spell_dual_spec` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_stats` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_ticket` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_titles` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_transmogs` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_variables` SET `lowGuid` = %u WHERE `lowGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `character_xp_from_log` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `corpse` SET `player` = %u WHERE `player` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `closedBy` = %u WHERE `closedBy` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `assignedTo` = %u WHERE `assignedTo` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `groups` SET `leaderGuid` = %u WHERE `leaderGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `groups` SET `mainTank` = %u WHERE `mainTank` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `groups` SET `mainAssistant` = %u WHERE `mainAssistant` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `groups` SET `looterGuid` = %u WHERE `looterGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `group_instance` SET `leaderGuid` = %u WHERE `leaderGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `group_member` SET `memberGuid` = %u WHERE `memberGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild` SET `leaderguid` = %u WHERE `leaderguid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `creatorGuid` = %u WHERE `creatorGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `giftCreatorGuid` = %u WHERE `giftCreatorGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild_bank_log` SET `player` = %u WHERE `player` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild_eventlog` SET `PlayerGuid1` = %u WHERE `PlayerGuid1` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild_eventlog` SET `PlayerGuid2` = %u WHERE  `PlayerGuid2` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `guild_member` SET `guid` = %u WHERE  `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `hardcore_deaths` SET `lowGuid` = %u WHERE  `lowGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `item_instance` SET `owner_guid` = %u WHERE `owner_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `item_instance` SET `creatorGuid` = %u WHERE `creatorGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `item_instance` SET `giftCreatorGuid` = %u WHERE `giftCreatorGuid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `item_loot` SET `owner_guid` = %u WHERE `owner_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `logs_movement` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `logs_warden` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `mail` SET `sender` = %u WHERE `sender` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `mail` SET `receiver` = %u WHERE `receiver` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `mail_items` SET `receiver` = %u WHERE `receiver` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `petition` SET `ownerguid` = %u WHERE `ownerguid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `petition_sign` SET `ownerguid` = %u WHERE `ownerguid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `petition_sign` SET `playerguid` = %u WHERE `playerguid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `pet_aura` SET `caster_guid` = %u WHERE `caster_guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `store_racechange` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                CharacterDatabase2.PExecute("UPDATE `whisper_targets` SET `target_guid` = %u WHERE `target_guid` = %u", newGuid.first, guid);
            }
        }
    }
    
    return true;
}

void UpdateAutoIncrementField(char const* fieldName, char const* tableName)
{
    sLog.outInfo("Loading %s.%s...", g_db1Name.c_str(), tableName);
    std::unique_ptr<QueryResult> result(CharacterDatabase1.PQuery("SELECT MAX(`%s`) FROM `%s`", fieldName, tableName));
    if (result)
    {
        uint32 maxId = 0;

        do
        {
            Field* fields = result->Fetch();

            maxId = fields[0].GetUInt32();

        } while (result->NextRow());

        result.reset(CharacterDatabase2.PQuery("SELECT MAX(`%s`) FROM `%s`", fieldName, tableName));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();

                uint32 db2Max = fields[0].GetUInt32();

                if (db2Max > maxId)
                    maxId = db2Max;

            } while (result->NextRow());
        }

        CharacterDatabase2.PExecute("UPDATE `%s` SET `%s` = `%s` + %u", tableName, fieldName, fieldName, maxId);
    }
}

void UpdateAutoIncrementFields()
{
    UpdateAutoIncrementField("id", "arena_stats_single");
    UpdateAutoIncrementField("id", "bounty_quest_targets");
    UpdateAutoIncrementField("id", "bugreport");
    UpdateAutoIncrementField("id", "bugreports");
    UpdateAutoIncrementField("Id", "characters_total_money");
    UpdateAutoIncrementField("id", "character_deleted_items");
    UpdateAutoIncrementField("Id", "character_egg_loot");
    UpdateAutoIncrementField("id", "character_item_logs");
    UpdateAutoIncrementField("ticket_id", "character_ticket");
    UpdateAutoIncrementField("log_id", "guild_bank_log");
    UpdateAutoIncrementField("id", "guild_bank_tabs");
    //UpdateAutoIncrementField("LogGuid", "guild_eventlog");
    UpdateAutoIncrementField("entry", "logs_warden");
    UpdateAutoIncrementField("transaction", "store_racechange");
}

bool UpdateItemGuids()
{
    UniqueKeyData itemGuids;

    sLog.outInfo("Loading %s.item_instance...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `guid` FROM `item_instance`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            itemGuids.existingKeys1.insert(guid);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.item_instance...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `guid` FROM `item_instance`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            itemGuids.existingKeys2.insert(guid);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(itemGuids.existingKeys1.size() + itemGuids.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of items exceeds max value of int32.");
        return false;
    }

    if (!itemGuids.existingKeys1.empty() && !itemGuids.existingKeys2.empty())
    {
        int64 const maxItemGuid1 = *itemGuids.existingKeys1.rbegin();
        int64 const maxItemGuid2 = *itemGuids.existingKeys2.rbegin();
        int64 const maxItemGuid = std::max(maxItemGuid1, maxItemGuid2);

        if (INT32_MAX > int64(maxItemGuid + maxItemGuid2))
        {
            sLog.outInfo("Updating item guids (fast method)...");

            CharacterDatabase2.PExecute("UPDATE `auction` SET `itemguid` = (`itemguid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `auction` table");

            CharacterDatabase2.PExecute("UPDATE `character_aura` SET `item_guid` = (`item_guid` + %u) WHERE `item_guid` != 0", maxItemGuid);
            sLog.outInfo("- Updated `character_aura` table");

            CharacterDatabase2.PExecute("UPDATE `character_aura_suspended` SET `item_guid` = (`item_guid` + %u) WHERE `item_guid` != 0", maxItemGuid);
            sLog.outInfo("- Updated `character_aura_suspended` table");

            CharacterDatabase2.PExecute("UPDATE `character_egg_loot` SET `itemGuid` = (`itemGuid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `character_egg_loot` table");

            CharacterDatabase2.PExecute("UPDATE `character_gifts` SET `item_guid` = (`item_guid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `character_gifts` table");

            CharacterDatabase2.PExecute("UPDATE `character_inventory` SET `item` = (`item` + %u)", maxItemGuid);
            CharacterDatabase2.PExecute("UPDATE `character_inventory` SET `bag` = (`bag` + %u) WHERE `bag` != 0", maxItemGuid);
            sLog.outInfo("- Updated `character_inventory` table");

            CharacterDatabase2.PExecute("UPDATE `character_item_logs` SET `itemLowGuid` = (`itemLowGuid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `character_item_logs` table");

            CharacterDatabase2.PExecute("UPDATE `item_instance` SET `guid` = (`guid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `item_instance` table");

            CharacterDatabase2.PExecute("UPDATE `item_loot` SET `guid` = (`guid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `item_loot` table");

            CharacterDatabase2.PExecute("UPDATE `mail_items` SET `item_guid` = (`item_guid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `mail_items` table");

            CharacterDatabase2.PExecute("UPDATE `petition` SET `charterguid` = (`charterguid` + %u)", maxItemGuid);
            sLog.outInfo("- Updated `petition` table");

            CharacterDatabase2.PExecute("UPDATE `pet_aura` SET `item_guid` = (`item_guid` + %u) WHERE `item_guid` != 0", maxItemGuid);
            sLog.outInfo("- Updated `pet_aura` table");
        }
        else
        {
            sLog.outInfo("Updating item guids (slow method)...");
            for (auto const& guid : itemGuids.existingKeys2)
            {
                std::pair<uint32, bool> newGuid = itemGuids.ReplaceKeyIfNeeded(guid);
                if (newGuid.second)
                {
                    sLog.outInfo("Guid of item %u is taken. Updating to %u.", guid, newGuid.first);
                    CharacterDatabase2.PExecute("UPDATE `auction` SET `itemguid` = %u WHERE `itemguid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_aura` SET `item_guid` = %u WHERE `item_guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_aura_suspended` SET `item_guid` = %u WHERE `item_guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_egg_loot` SET `itemGuid` = %u WHERE `itemGuid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_gifts` SET `item_guid` = %u WHERE `item_guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_inventory` SET `item` = %u WHERE `item` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_inventory` SET `bag` = %u WHERE `bag` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `character_item_logs` SET `itemLowGuid` = %u WHERE `itemLowGuid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `item_instance` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `item_loot` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `mail_items` SET `item_guid` = %u WHERE `item_guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `petition` SET `charterguid` = %u WHERE `charterguid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `pet_aura` SET `item_guid` = %u WHERE `item_guid` = %u", newGuid.first, guid);
                }
            }
        }
    }

    return true;
}

bool UpdatePetitionGuids()
{
    UniqueKeyData petitionGuids;

    sLog.outInfo("Loading %s.petition...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `petitionguid` FROM `petition`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            petitionGuids.existingKeys1.insert(guid);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.petition...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `petitionguid` FROM `petition`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            petitionGuids.existingKeys2.insert(guid);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(petitionGuids.existingKeys1.size() + petitionGuids.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of petitions exceeds max value of int32.");
        return false;
    }

    if (!petitionGuids.existingKeys1.empty() && !petitionGuids.existingKeys2.empty())
    {
        int64 const maxPetitionGuid1 = *petitionGuids.existingKeys1.rbegin();
        int64 const maxPetitionGuid2 = *petitionGuids.existingKeys2.rbegin();
        int64 const maxPetitionGuid = std::max(maxPetitionGuid1, maxPetitionGuid2);

        if (INT32_MAX > int64(maxPetitionGuid + maxPetitionGuid2))
        {
            sLog.outInfo("Updating petition guids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `petition` SET `petitionguid` = (`petitionguid` + %u)", maxPetitionGuid);
            CharacterDatabase2.PExecute("UPDATE `petition_sign` SET `petitionguid` = (`petitionguid` + %u)", maxPetitionGuid);
        }
        else
        {
            sLog.outInfo("Updating petition guids (slow method)...");
            for (auto const& guid : petitionGuids.existingKeys2)
            {
                std::pair<uint32, bool> newGuid = petitionGuids.ReplaceKeyIfNeeded(guid);
                if (newGuid.second)
                {
                    sLog.outInfo("Guid of petition %u is taken. Updating to %u.", guid, newGuid.first);
                    CharacterDatabase2.PExecute("UPDATE `petition` SET `petitionguid` = %u WHERE `petitionguid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `petition_sign` SET `petitionguid` = %u WHERE `petitionguid` = %u", newGuid.first, guid);
                }
            }
        }
    }

    return true;
}

bool UpdatePetGuids()
{
    UniqueKeyData petGuids;

    sLog.outInfo("Loading %s.character_pet...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `id` FROM `character_pet`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            petGuids.existingKeys1.insert(guid);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.character_pet...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `id` FROM `character_pet`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            petGuids.existingKeys2.insert(guid);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(petGuids.existingKeys1.size() + petGuids.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of pets exceeds max value of int32.");
        return false;
    }

    if (!petGuids.existingKeys1.empty() && !petGuids.existingKeys2.empty())
    {
        int64 const maxPetGuid1 = *petGuids.existingKeys1.rbegin();
        int64 const maxPetGuid2 = *petGuids.existingKeys2.rbegin();
        int64 const maxPetGuid = std::max(maxPetGuid1, maxPetGuid2);

        if (INT32_MAX > int64(maxPetGuid + maxPetGuid2))
        {
            sLog.outInfo("Updating pet guids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `character_pet` SET `id` = (`id` + %u)", maxPetGuid);
            CharacterDatabase2.PExecute("UPDATE `pet_aura` SET `guid` = (`guid` + %u)", maxPetGuid);
            CharacterDatabase2.PExecute("UPDATE `pet_spell` SET `guid` = (`guid` + %u)", maxPetGuid);
            CharacterDatabase2.PExecute("UPDATE `pet_spell_cooldown` SET `guid` = (`guid` + %u)", maxPetGuid);
        }
        else
        {
            sLog.outInfo("Updating pet guids (slow method)...");
            for (auto const& guid : petGuids.existingKeys2)
            {
                std::pair<uint32, bool> newGuid = petGuids.ReplaceKeyIfNeeded(guid);
                if (newGuid.second)
                {
                    sLog.outInfo("Guid of pet %u is taken. Updating to %u.", guid, newGuid.first);
                    CharacterDatabase2.PExecute("UPDATE `character_pet` SET `id` = %u WHERE `id` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `pet_aura` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `pet_spell` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                    CharacterDatabase2.PExecute("UPDATE `pet_spell_cooldown` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                }
            }
        }
    }

    return true;
}

bool UpdateCorpseGuids()
{
    UniqueKeyData corpseGuids;

    sLog.outInfo("Loading %s.corpse...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `guid` FROM `corpse`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            corpseGuids.existingKeys1.insert(guid);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.corpse...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `guid` FROM `corpse`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid = fields[0].GetUInt32();
            corpseGuids.existingKeys2.insert(guid);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(corpseGuids.existingKeys1.size() + corpseGuids.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of corpses exceeds max value of int32.");
        return false;
    }

    if (!corpseGuids.existingKeys1.empty() && !corpseGuids.existingKeys2.empty())
    {
        int64 const maxCorpseGuid1 = *corpseGuids.existingKeys1.rbegin();
        int64 const maxCorpseGuid2 = *corpseGuids.existingKeys2.rbegin();
        int64 const maxCorpseGuid = std::max(maxCorpseGuid1, maxCorpseGuid2);

        if (INT32_MAX > int64(maxCorpseGuid + maxCorpseGuid2))
        {
            sLog.outInfo("Updating corpse guids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `corpse` SET `guid` = (`guid` + %u)", maxCorpseGuid);
        }
        else
        {
            sLog.outInfo("Updating corpse guids (slow method)...");
            for (auto const& guid : corpseGuids.existingKeys2)
            {
                std::pair<uint32, bool> newGuid = corpseGuids.ReplaceKeyIfNeeded(guid);
                if (newGuid.second)
                {
                    sLog.outInfo("Guid of corpse %u is taken. Updating to %u.", guid, newGuid.first);
                    CharacterDatabase2.PExecute("UPDATE `corpse` SET `guid` = %u WHERE `guid` = %u", newGuid.first, guid);
                }
            }
        }
    }

    return true;
}

bool UpdateGroupIds()
{
    UniqueKeyData groupIds;

    sLog.outInfo("Loading %s.groups...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `groupId` FROM `groups`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 groupId = fields[0].GetUInt32();
            groupIds.existingKeys1.insert(groupId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.groups...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `groupId` FROM `groups`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 groupId = fields[0].GetUInt32();
            groupIds.existingKeys2.insert(groupId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(groupIds.existingKeys1.size() + groupIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of groups exceeds max value of int32.");
        return false;
    }

    if (!groupIds.existingKeys1.empty() && !groupIds.existingKeys2.empty())
    {
        int64 const maxGroupId1 = *groupIds.existingKeys1.rbegin();
        int64 const maxGroupId2 = *groupIds.existingKeys2.rbegin();
        int64 const maxGroupId = std::max(maxGroupId1, maxGroupId2);

        if (INT32_MAX > (maxGroupId + maxGroupId2))
        {
            sLog.outInfo("Updating group ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `groups` SET `groupId` = (`groupId` + %u)", maxGroupId);
            CharacterDatabase2.PExecute("UPDATE `group_member` SET `groupId` = (`groupId` + %u)", maxGroupId);
        }
        else
        {
            sLog.outInfo("Updating group ids (slow method)...");
            for (auto const& id : groupIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = groupIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of group %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `groups` SET `groupId` = %u WHERE `groupId` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `group_member` SET `groupId` = %u WHERE `groupId` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateGuildIds()
{
    std::set<std::string> guildNames1;
    std::map<uint32, std::string> guildNames2;
    UniqueKeyData guildIds;

    sLog.outInfo("Loading %s.guild...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `guildid`, `name` FROM `guild`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guildId = fields[0].GetUInt32();
            std::string name = fields[1].GetCppString();

            guildIds.existingKeys1.insert(guildId);
            guildNames1.insert(name);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.guild...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `guildid`, `name` FROM `guild`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guildId = fields[0].GetUInt32();
            std::string name = fields[1].GetCppString();

            guildIds.existingKeys2.insert(guildId);
            guildNames2.insert({ guildId, name });

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(guildIds.existingKeys1.size() + guildIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of guilds exceeds max value of int32.");
        return false;
    }

    auto GenerateGuildName = [&]() -> std::string
    {
        for (char chr1 = 'a'; chr1 < 'z'; chr1++)
        {
            for (char chr2 = 'a'; chr2 < 'z'; chr2++)
            {
                for (char chr3 = 'a'; chr3 < 'z'; chr3++)
                {
                    for (char chr4 = 'a'; chr4 < 'z'; chr4++)
                    {
                        std::string name2 = "PH ";
                        name2 += chr1;
                        name2 += chr2;
                        name2 += chr3;
                        name2 += chr4;

                        if (guildNames1.find(name2) == guildNames1.end())
                        {
                            guildNames1.insert(name2);
                            return name2;
                        }
                    }
                }
            }
        }

        return "DUPLICATE";
    };
    
    sLog.outInfo("Updating guild names...");
    for (auto const& itr : guildNames2)
    {
        if (!itr.second.empty() && guildNames1.find(itr.second) != guildNames1.end())
        {
            sLog.outInfo("Name of guild %s (%u) is taken. Marking for rename.", itr.second.c_str(), itr.first);
            CharacterDatabase2.PExecute("UPDATE `guild` SET `name` = '%s' WHERE `guildid` = %u", GenerateGuildName().c_str(), itr.first);
        }
    }

    if (!guildIds.existingKeys1.empty() && !guildIds.existingKeys2.empty())
    {
        int64 const maxGuildId1 = *guildIds.existingKeys1.rbegin();
        int64 const maxGuildId2 = *guildIds.existingKeys2.rbegin();
        int64 const maxGuildId = std::max(maxGuildId1, maxGuildId2);

        if (INT32_MAX > int64(maxGuildId + maxGuildId2))
        {
            sLog.outInfo("Updating guild ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `guild` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_bank_log` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_bank_money` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_bank_tabs` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_eventlog` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_house` SET `guild_id` = (`guild_id` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_member` SET `guildid` = (`guildid` + %u)", maxGuildId);
            CharacterDatabase2.PExecute("UPDATE `guild_rank` SET `guildid` = (`guildid` + %u)", maxGuildId);
        }
        else
        {
            sLog.outInfo("Updating guild ids (slow method)...");
            for (auto const& id : guildIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = guildIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of guild %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `guild` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_bank_log` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_bank_money` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_bank_tabs` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_eventlog` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_house` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_member` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_rank` SET `guildid` = %u WHERE `guildid` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateInstanceIds()
{
    UniqueKeyData instanceIds;

    sLog.outInfo("Loading %s.instance...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `id` FROM `instance`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 instanceId = fields[0].GetUInt32();
            instanceIds.existingKeys1.insert(instanceId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.instance...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `id` FROM `instance`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 instanceId = fields[0].GetUInt32();
            instanceIds.existingKeys2.insert(instanceId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(instanceIds.existingKeys1.size() + instanceIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of instances exceeds max value of int32.");
        return false;
    }

    if (!instanceIds.existingKeys1.empty() && !instanceIds.existingKeys2.empty())
    {
        int64 const maxInstanceId1 = *instanceIds.existingKeys1.rbegin();
        int64 const maxInstanceId2 = *instanceIds.existingKeys2.rbegin();
        int64 const maxInstanceId = std::max(maxInstanceId1, maxInstanceId2);

        if (INT32_MAX > (maxInstanceId + maxInstanceId2))
        {
            sLog.outInfo("Updating instance ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `bugreports` SET `playerInstanceId` = (`playerInstanceId` + %u) WHERE `playerInstanceId` != 0", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `character_battleground_data` SET `instance_id` = (`instance_id` + %u) WHERE `instance_id` != 0", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `character_instance` SET `instance` = (`instance` + %u)", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `corpse` SET `instance` = (`instance` + %u) WHERE `instance` != 0", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `creature_respawn` SET `instance` = (`instance` + %u) WHERE `instance` != 0", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `gameobject_respawn` SET `instance` = (`instance` + %u) WHERE `instance` != 0", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `group_instance` SET `instance` = (`instance` + %u)", maxInstanceId);
            CharacterDatabase2.PExecute("UPDATE `instance` SET `id` = (`id` + %u)", maxInstanceId);
        }
        else
        {
            sLog.outInfo("Updating instance ids (slow method)...");
            for (auto const& id : instanceIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = instanceIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of instance %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `bugreports` SET `playerInstanceId` = %u WHERE `playerInstanceId` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `character_battleground_data` SET `instance_id` = %u WHERE `instance_id` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `character_instance` SET `instance` = %u WHERE `instance` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `corpse` SET `instance` = %u WHERE `instance` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `creature_respawn` SET `instance` = %u WHERE `instance` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `gameobject_respawn` SET `instance` = %u WHERE `instance` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `group_instance` SET `instance` = %u WHERE `instance` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `instance` SET `id` = %u WHERE `id` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateMailIds()
{
    UniqueKeyData mailIds;

    sLog.outInfo("Loading %s.mail...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `id` FROM `mail`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 mailId = fields[0].GetUInt32();
            mailIds.existingKeys1.insert(mailId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.mail...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `id` FROM `mail`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 mailId = fields[0].GetUInt32();
            mailIds.existingKeys2.insert(mailId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(mailIds.existingKeys1.size() + mailIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of mails exceeds max value of int32.");
        return false;
    }

    if (!mailIds.existingKeys1.empty() && !mailIds.existingKeys2.empty())
    {
        int64 const maxMailId1 = *mailIds.existingKeys1.rbegin();
        int64 const maxMailid2 = *mailIds.existingKeys2.rbegin();
        int64 const maxMailid = std::max(maxMailId1, maxMailid2);

        if (INT32_MAX > int64(maxMailid + maxMailid2))
        {
            sLog.outInfo("Updating mail ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `mail` SET `id` = (`id` + %u)", maxMailid);
            CharacterDatabase2.PExecute("UPDATE `mail_items` SET `mail_id` = (`mail_id` + %u)", maxMailid);
        }
        else
        {
            sLog.outInfo("Updating mail ids (slow method)...");
            for (auto const& id : mailIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = mailIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of mail %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `mail` SET `id` = %u WHERE `id` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `mail_items` SET `mail_id` = %u WHERE `mail_id` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateItemTextIds()
{
    UniqueKeyData itemTextIds;

    sLog.outInfo("Loading %s.item_text...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `id` FROM `item_text`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 itemTextId = fields[0].GetUInt32();
            itemTextIds.existingKeys1.insert(itemTextId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.mail...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `id` FROM `item_text`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 itemTextId = fields[0].GetUInt32();
            itemTextIds.existingKeys2.insert(itemTextId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(itemTextIds.existingKeys1.size() + itemTextIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of item texts exceeds max value of int32.");
        return false;
    }

    if (!itemTextIds.existingKeys1.empty() && !itemTextIds.existingKeys2.empty())
    {
        int64 const maxItemTextId1 = *itemTextIds.existingKeys1.rbegin();
        int64 const maxItemTextId2 = *itemTextIds.existingKeys2.rbegin();
        int64 const maxItemTextId = std::max(maxItemTextId1, maxItemTextId2);

        if (INT32_MAX > int64(maxItemTextId + maxItemTextId2))
        {
            sLog.outInfo("Updating item text ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `text` = (`text` + %u) WHERE `text` != 0", maxItemTextId);
            CharacterDatabase2.PExecute("UPDATE `item_instance` SET `text` = (`text` + %u) WHERE `text` != 0", maxItemTextId);
            CharacterDatabase2.PExecute("UPDATE `item_text` SET `id` = (`id` + %u)", maxItemTextId);
            CharacterDatabase2.PExecute("UPDATE `mail` SET `itemTextId` = (`itemTextId` + %u) WHERE `itemTextId` != 0", maxItemTextId);
        }
        else
        {
            sLog.outInfo("Updating item text ids (slow method)...");
            for (auto const& id : itemTextIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = itemTextIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of item text %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `text` = %u WHERE `text` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `item_instance` SET `text` = %u WHERE `text` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `item_text` SET `id` = %u WHERE `id` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `mail` SET `itemTextId` = %u WHERE `itemTextId` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateTransmogIds()
{
    UniqueKeyData transmogIds;

    sLog.outInfo("Loading %s.item_transmogs...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `ID` FROM `item_transmogs`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 transmogId = fields[0].GetUInt32();
            transmogIds.existingKeys1.insert(transmogId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.item_transmogs...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `ID` FROM `item_transmogs`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 transmogId = fields[0].GetUInt32();
            transmogIds.existingKeys2.insert(transmogId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(transmogIds.existingKeys1.size() + transmogIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of transmogs exceeds max value of int32.");
        return false;
    }

    if (!transmogIds.existingKeys1.empty() && !transmogIds.existingKeys2.empty())
    {
        int64 const maxTransmogId1 = *transmogIds.existingKeys1.rbegin();
        int64 const maxTransmogId2 = *transmogIds.existingKeys2.rbegin();
        int64 const maxTransmogId = std::max(maxTransmogId1, maxTransmogId2);

        if (INT32_MAX > int64(maxTransmogId + maxTransmogId2))
        {
            sLog.outInfo("Updating transmog ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `item_transmogs` SET `ID` = (`ID` + %u)", maxTransmogId);
            CharacterDatabase2.PExecute("UPDATE `item_instance` SET `transmogrifyId` = (`transmogrifyId` + %u) WHERE `transmogrifyId` != 0", maxTransmogId);
            CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `transmogrifyId` = (`transmogrifyId` + %u) WHERE `transmogrifyId` != 0", maxTransmogId);
        }
        else
        {
            sLog.outInfo("Updating transmog ids (slow method)...");
            for (auto const& id : transmogIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = transmogIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of transmog %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `item_transmogs` SET `ID` = %u WHERE `ID` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `item_instance` SET `transmogrifyId` = %u WHERE `transmogrifyId` = %u", newId.first, id);
                    CharacterDatabase2.PExecute("UPDATE `guild_bank` SET `transmogrifyId` = %u WHERE `transmogrifyId` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateAuctionIds()
{
    UniqueKeyData auctionIds;

    sLog.outInfo("Loading %s.auction...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `id` FROM `auction`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 auctionId = fields[0].GetUInt32();
            auctionIds.existingKeys1.insert(auctionId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.auction...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `id` FROM `auction`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 auctionId = fields[0].GetUInt32();
            auctionIds.existingKeys2.insert(auctionId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(auctionIds.existingKeys1.size() + auctionIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of auctions exceeds max value of int32.");
        return false;
    }

    if (!auctionIds.existingKeys1.empty() && !auctionIds.existingKeys2.empty())
    {
        int64 const maxAuctionId1 = *auctionIds.existingKeys1.rbegin();
        int64 const maxAuctionId2 = *auctionIds.existingKeys2.rbegin();
        int64 const maxAuctionId = std::max(maxAuctionId1, maxAuctionId2);

        if (INT32_MAX > int64(maxAuctionId + maxAuctionId2))
        {
            sLog.outInfo("Updating auction ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `auction` SET `id` = (`id` + %u)", maxAuctionId);
        }
        else
        {
            sLog.outInfo("Updating auction ids (slow method)...");
            for (auto const& id : auctionIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = auctionIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of auction %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `auction` SET `id` = %u WHERE `id` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

bool UpdateTicketIds()
{
    UniqueKeyData ticketIds;

    sLog.outInfo("Loading %s.gm_tickets...", g_db1Name.c_str());
    std::unique_ptr<QueryResult> result(CharacterDatabase1.Query("SELECT `ticketId` FROM `gm_tickets`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 ticketId = fields[0].GetUInt32();
            ticketIds.existingKeys1.insert(ticketId);

        } while (result->NextRow());
    }

    sLog.outInfo("Loading %s.gm_tickets...", g_db2Name.c_str());
    result.reset(CharacterDatabase2.Query("SELECT `ticketId` FROM `gm_tickets`"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 ticketId = fields[0].GetUInt32();
            ticketIds.existingKeys2.insert(ticketId);

        } while (result->NextRow());
    }

    if (INT32_MAX < int64(ticketIds.existingKeys1.size() + ticketIds.existingKeys2.size()))
    {
        sLog.outError("Databases cannot be merged because number of tickets exceeds max value of int32.");
        return false;
    }

    if (!ticketIds.existingKeys1.empty() && !ticketIds.existingKeys2.empty())
    {
        int64 const maxTicketId1 = *ticketIds.existingKeys1.rbegin();
        int64 const maxTicketId2 = *ticketIds.existingKeys2.rbegin();
        int64 const maxTicketId = std::max(maxTicketId1, maxTicketId2);

        if (INT32_MAX > int64(maxTicketId + maxTicketId2))
        {
            sLog.outInfo("Updating ticket ids (fast method)...");
            CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `ticketId` = (`ticketId` + %u)", maxTicketId);
        }
        else
        {
            sLog.outInfo("Updating ticket ids (slow method)...");
            for (auto const& id : ticketIds.existingKeys2)
            {
                std::pair<uint32, bool> newId = ticketIds.ReplaceKeyIfNeeded(id);
                if (newId.second)
                {
                    sLog.outInfo("Id of ticket %u is taken. Updating to %u.", id, newId.first);
                    CharacterDatabase2.PExecute("UPDATE `gm_tickets` SET `ticketId` = %u WHERE `ticketId` = %u", newId.first, id);
                }
            }
        }
    }

    return true;
}

void DeleteNonCharacterData()
{
    sLog.outInfo("Deleting non character data from db 2...");
    CharacterDatabase2.PExecute("DELETE FROM `creature_respawn` WHERE `map` IN (0,1)");
    CharacterDatabase2.PExecute("DELETE FROM `gameobject_respawn` WHERE `map` IN (0,1)");
    CharacterDatabase2.Execute("TRUNCATE `game_event_status`");
    CharacterDatabase2.Execute("TRUNCATE `instance_reset`");
    CharacterDatabase2.Execute("TRUNCATE `logs_anticheat`");
    CharacterDatabase2.Execute("TRUNCATE `logs_shellcoin`");
    CharacterDatabase2.Execute("TRUNCATE `migrations`");
    CharacterDatabase2.Execute("TRUNCATE `saved_variables`");
    CharacterDatabase2.Execute("TRUNCATE `variables`");
    CharacterDatabase2.Execute("TRUNCATE `world`");
    CharacterDatabase2.Execute("TRUNCATE `worldstates`");
}

char const* g_mainLogFileName = "RealmMerge.log";

//=======================================================
int main(int argc, char* argv[])
{
    puts("This tool resolves guid conflicts between character databases,");
    puts("to help you merge DB-2 into DB-1 and consolidate the realms.");
    puts("It will update keys in DB-2 and you have to manually export");
    puts("the database to a sql file afterwards and insert into DB-1.");
    puts("-------------------------------------------------------------");
    puts("");
    puts("Please enter your database connection info.");
    puts("");

    std::string connString = MakeConnectionString();

    printf("Database 1 Name: ");
    g_db1Name = GetString();
    if (g_db1Name.empty())
    {
        puts("Error: You did not enter a database name.");
        GetChar();
        return 1;
    }

    printf("Database 2 Name: ");
    g_db2Name = GetString();
    if (g_db2Name.empty())
    {
        puts("Error: You did not enter a database name.");
        GetChar();
        return 1;
    }

    if (!CharacterDatabase1.Initialize("Character db to merge into", (connString + g_db1Name).c_str()))
    {
        puts("Error: Cannot connect to DB-1.");
        GetChar();
        return 1;
    }

    if (!CharacterDatabase2.Initialize("Character db to update", (connString + g_db2Name).c_str()))
    {
        puts("Error: Cannot connect to DB-2.");
        GetChar();
        return 1;
    }

    time_t startTime = time(nullptr);
    sLog.outInfo("Starting work at %u...", startTime);

    if (!UpdateCharacterGuids())
    {
        GetChar();
        return 1;
    }
    
    if (!UpdateItemGuids())
    {
        GetChar();
        return 1;
    }

    if (!UpdatePetitionGuids())
    {
        GetChar();
        return 1;
    }

    if (!UpdatePetGuids())
    {
        GetChar();
        return 1;
    }

    if (!UpdateCorpseGuids())
    {
        GetChar();
        return 1;
    }

    if (!UpdateGroupIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateGuildIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateInstanceIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateMailIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateItemTextIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateTransmogIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateAuctionIds())
    {
        GetChar();
        return 1;
    }

    if (!UpdateTicketIds())
    {
        GetChar();
        return 1;
    }

    UpdateAutoIncrementFields();
    DeleteNonCharacterData();

    sLog.outInfo("Work done in %u seconds.", (time(nullptr) - startTime));

    GetChar();
    return 0;
}
