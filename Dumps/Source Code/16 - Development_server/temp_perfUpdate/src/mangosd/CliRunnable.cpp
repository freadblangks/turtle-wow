/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
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

/// \addtogroup mangosd
/// @{
/// \file

#include "Common.h"
#include "Language.h"
#include "Log.h"
#include "World.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Config/Config.h"
#include "Util.h"
#include "AccountMgr.h"
#include "CliRunnable.h"
#include "MapManager.h"
#include "Player.h"
#include "Chat.h"
#include "Chat/AsyncCommandHandlers.h"

#include <iterator>

void utf8print(std::any, const char* str)
{
#if PLATFORM == PLATFORM_WINDOWS
    std::wstring wtemp_buf;
    std::string temp_buf(str);

    if (!Utf8toWStr(temp_buf, wtemp_buf))
        return;

    // Guarantee nullptr termination
    if (!temp_buf.empty())
    {
        wtemp_buf.push_back('\0');
        temp_buf.resize(wtemp_buf.size());
        CharToOemBuffW(&wtemp_buf[0], &temp_buf[0], wtemp_buf.size());
    }

    printf("%s", temp_buf.c_str());
#else
    printf("%s", str);
#endif
}

void commandFinished(std::any, bool /*sucess*/)
{
    printf("mangos>");
    fflush(stdout);
}

/// Delete a user account and all associated characters in this realm
/// \todo This function has to be enhanced to respect the login/realm split (delete char, delete account chars in realm, delete account chars in realm then delete account
bool ChatHandler::HandleAccountDeleteCommand(char* args)
{
    if (!*args)
        return false;

    std::string account_name;
    uint32 account_id = ExtractAccountId(&args, &account_name);
    if (!account_id)
        return false;

    /// Commands not recommended call from chat, but support anyway
    /// can delete only for account with less security
    /// This is also reject self apply in fact
    if (HasLowerSecurityAccount (nullptr, account_id, true))
        return false;

    AccountOpResult result = sAccountMgr.DeleteAccount(account_id);
    switch(result)
    {
        case AOR_OK:
            PSendSysMessage(LANG_ACCOUNT_DELETED,account_name.c_str());
            break;
        case AOR_NAME_NOT_EXIST:
            PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        case AOR_DB_INTERNAL_ERROR:
            PSendSysMessage(LANG_ACCOUNT_NOT_DELETED_SQL_ERROR,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage(LANG_ACCOUNT_NOT_DELETED,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    return true;
}

/**
 * Collects all GUIDs (and related info) from deleted characters which are still in the database.
 *
 * @param foundList    a reference to an std::list which will be filled with info data
 * @param useName      use a name/guid search (true) or an account name / account id search (false)
 * @param searchString the search string which either contains a player GUID (low part) or a part of the character-name
 * @return             returns false if there was a problem while selecting the characters (e.g. player name not normalizeable)
 */
bool ChatHandler::GetDeletedCharacterInfoList(DeletedInfoList& foundList, bool useName, std::string searchString)
{
    QueryResult* resultChar = nullptr;
    if (!searchString.empty())
    {
        if (useName)
        {
            // search by GUID
            if (isNumeric(searchString))
                resultChar = CharacterDatabase.PQuery("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL AND guid = %u LIMIT 0,50", uint32(atoi(searchString.c_str())));
            // search by name
            else
            {
                if (!normalizePlayerName(searchString))
                    return false;

                CharacterDatabase.escape_string(searchString);

                resultChar = CharacterDatabase.PQuery("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL AND deleteInfos_Name " _LIKE_ " " _CONCAT2_("'%s'", "'%%'") " LIMIT 0,50", searchString.c_str());
            }
        }
        else
        {
            // search by account id
            if (isNumeric(searchString))
                resultChar = CharacterDatabase.PQuery("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL AND deleteInfos_Account = %u LIMIT 0,50", uint32(atoi(searchString.c_str())));
            // search by account name
            else
            {
                if (!AccountMgr::normalizeString(searchString))
                    return false;

                LoginDatabase.escape_string(searchString);
                QueryResult* result = LoginDatabase.PQuery("SELECT id FROM account WHERE username " _LIKE_ " " _CONCAT2_("'%s'", "'%%'"), searchString.c_str());
                std::list<uint32> list;
                if (result)
                {
                    do
                    {
                        Field* fields = result->Fetch();
                        uint32 acc_id = fields[0].GetUInt32();
                        list.push_back(acc_id);
                    } while (result->NextRow());

                    delete result;
                }

                if (list.size() < 1)
                    return false;
                std::stringstream accountStream;
                std::copy(list.begin(), list.end(), std::ostream_iterator<int>(accountStream, ","));
                std::string accounts = accountStream.str();
                accounts.pop_back();
                resultChar = CharacterDatabase.PQuery("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL AND deleteInfos_Account IN (%s) LIMIT 0,50", accounts.c_str());
            }
        }
    }
    else
        resultChar = CharacterDatabase.Query("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL LIMIT 0,50");

    if (resultChar)
    {
        do
        {
            Field* fields = resultChar->Fetch();

            DeletedInfo info;

            info.lowguid    = fields[0].GetUInt32();
            info.name       = fields[1].GetCppString();
            info.accountId  = fields[2].GetUInt32();

            // account name will be empty for nonexistent account
            sAccountMgr.GetName (info.accountId, info.accountName);

            info.deleteDate = time_t(fields[3].GetUInt64());

            foundList.push_back(info);
        } while (resultChar->NextRow());

        delete resultChar;
    }

    return true;
}

/**
 * Generate WHERE guids list by deleted info in way preventing return too long where list for existed query string length limit.
 *
 * @param itr          a reference to an deleted info list iterator, it updated in function for possible next function call if list to long
 * @param itr_end      a reference to an deleted info list iterator end()
 * @return             returns generated where list string in form: 'guid IN (gui1, guid2, ...)'
 */
std::string ChatHandler::GenerateDeletedCharacterGUIDsWhereStr(DeletedInfoList::const_iterator& itr, DeletedInfoList::const_iterator const& itr_end)
{
    std::ostringstream wherestr;
    wherestr << "guid IN ('";
    for(; itr != itr_end; ++itr)
    {
        wherestr << itr->lowguid;

        if (wherestr.str().size() > MAX_QUERY_LEN - 50)     // near to max query
        {
            ++itr;
            break;
        }

        DeletedInfoList::const_iterator itr2 = itr;
        if(++itr2 != itr_end)
            wherestr << "','";
    }
    wherestr << "')";
    return wherestr.str();
}

/**
 * Shows all deleted characters which matches the given search string, expected non empty list
 *
 * @see ChatHandler::HandleCharacterDeletedListCommand
 * @see ChatHandler::HandleCharacterDeletedRestoreCommand
 * @see ChatHandler::HandleCharacterDeletedDeleteCommand
 * @see ChatHandler::DeletedInfoList
 *
 * @param foundList contains a list with all found deleted characters
 */
void ChatHandler::HandleCharacterDeletedListHelper(DeletedInfoList const& foundList)
{
    if (!m_session)
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_HEADER);
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
    }

    for (DeletedInfoList::const_iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
    {
        std::string dateStr = TimeToTimestampStr(itr->deleteDate);

        if (!m_session)
            PSendSysMessage(LANG_CHARACTER_DELETED_LIST_LINE_CONSOLE,
                itr->lowguid, itr->name.c_str(), itr->accountName.empty() ? "<nonexistent>" : itr->accountName.c_str(),
                itr->accountId, dateStr.c_str());
        else
            PSendSysMessage(LANG_CHARACTER_DELETED_LIST_LINE_CHAT,
                itr->lowguid, itr->name.c_str(), itr->accountName.empty() ? "<nonexistent>" : itr->accountName.c_str(),
                itr->accountId, dateStr.c_str());
    }

    if (!m_session)
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
}

/**
 * Handles the '.character deleted list' command, which shows all deleted characters which matches the given search string
 *
 * @see ChatHandler::HandleCharacterDeletedListHelper
 * @see ChatHandler::HandleCharacterDeletedRestoreCommand
 * @see ChatHandler::HandleCharacterDeletedDeleteCommand
 * @see ChatHandler::DeletedInfoList
 *
 * @param args the search string which either contains a player GUID or a part of the character-name
 */
bool ChatHandler::HandleCharacterDeletedListNameCommand(char * args)
{
    return HandleCharacterDeletedListCommand(args, true);
}
bool ChatHandler::HandleCharacterDeletedListCommand(char* args, bool useName)
{
    DeletedInfoList foundList;
    if (!GetDeletedCharacterInfoList(foundList, useName, args))
        return false;

    // if no characters have been found, output a warning
    if (foundList.empty())
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
        return false;
    }

    HandleCharacterDeletedListHelper(foundList);
    return true;
}

/**
 * Restore a previously deleted character
 *
 * @see ChatHandler::HandleCharacterDeletedListHelper
 * @see ChatHandler::HandleCharacterDeletedRestoreCommand
 * @see ChatHandler::HandleCharacterDeletedDeleteCommand
 * @see ChatHandler::DeletedInfoList
 *
 * @param delInfo the informations about the character which will be restored
 */
void ChatHandler::HandleCharacterDeletedRestoreHelper(DeletedInfo& delInfo)
{
    if (delInfo.accountName.empty())                    // account not exist
    {
        PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_ACCOUNT, delInfo.name.c_str(), delInfo.lowguid, delInfo.accountId);
        return;
    }

    // check character count
    uint32 charcount = sAccountMgr.GetCharactersCount(delInfo.accountId);
    if (charcount >= 10)
    {
        PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_FULL, delInfo.name.c_str(), delInfo.lowguid, delInfo.accountId);
        return;
    }

    if (sObjectMgr.GetPlayerGuidByName(delInfo.name))
    {
        PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_NAME, delInfo.name.c_str(), delInfo.lowguid, delInfo.accountId);
        delInfo.name = std::to_string(delInfo.lowguid);
    }

    // use blocking query as we need to reload character into cache
    CharacterDatabase.DirectPExecute("UPDATE characters SET name='%s', account='%u', deleteDate=NULL, deleteInfos_Name=NULL, deleteInfos_Account=NULL WHERE deleteDate IS NOT NULL AND guid = %u",
        delInfo.name.c_str(), delInfo.accountId, delInfo.lowguid);
    sObjectMgr.LoadPlayerCacheData(delInfo.lowguid);
}

/**
 * Handles the '.character deleted restore' command, which restores all deleted characters which matches the given search string
 *
 * The command automatically calls '.character deleted list' command with the search string to show all restored characters.
 *
 * @see ChatHandler::HandleCharacterDeletedRestoreHelper
 * @see ChatHandler::HandleCharacterDeletedListCommand
 * @see ChatHandler::HandleCharacterDeletedDeleteCommand
 *
 * @param args the search string which either contains a player GUID or a part of the character-name
 */
bool ChatHandler::HandleCharacterDeletedRestoreCommand(char* args)
{
    // It is required to submit at least one argument
    if (!*args)
        return false;

    std::string searchString;
    std::string newCharName;
    uint32 newAccount = 0;

    // GCC by some strange reason fail build code without temporary variable
    std::istringstream params(args);
    params >> searchString >> newCharName >> newAccount;

    DeletedInfoList foundList;
    if (!GetDeletedCharacterInfoList(foundList, true, searchString))
        return false;

    if (foundList.empty())
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
        return false;
    }

    SendSysMessage(LANG_CHARACTER_DELETED_RESTORE);
    HandleCharacterDeletedListHelper(foundList);

    if (newCharName.empty())
    {
        // Drop nonexistent account cases
        for (DeletedInfoList::iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
            HandleCharacterDeletedRestoreHelper(*itr);
    }
    else if (foundList.size() == 1 && normalizePlayerName(newCharName))
    {
        DeletedInfo delInfo = foundList.front();

        // update name
        delInfo.name = newCharName;

        // if new account provided update deleted info
        if (newAccount && newAccount != delInfo.accountId)
        {
            delInfo.accountId = newAccount;
            sAccountMgr.GetName (newAccount, delInfo.accountName);
        }

        HandleCharacterDeletedRestoreHelper(delInfo);
    }
    else
        SendSysMessage(LANG_CHARACTER_DELETED_ERR_RENAME);

    return true;
}

bool ChatHandler::HandleCharacterEraseCommand(char* args)
{
    char* nameStr = ExtractLiteralArg(&args);
    if (!nameStr)
        return false;

    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&nameStr, &target, &target_guid, &target_name))
        return false;

    uint32 account_id;

    if (target)
    {
        account_id = target->GetSession()->GetAccountId();
        target->GetSession()->KickPlayer();
    }
    else
        account_id = sObjectMgr.GetPlayerAccountIdByGUID(target_guid);

    std::string account_name;
    sAccountMgr.GetName (account_id,account_name);

    Player::DeleteFromDB(target_guid, account_id, true, true);
    PSendSysMessage(LANG_CHARACTER_DELETED, target_name.c_str(), target_guid.GetCounter(), account_name.c_str(), account_id);
    return true;
}

/// Close RA connection
bool ChatHandler::HandleQuitCommand(char* /*args*/)
{
    // processed in RASocket
    SendSysMessage(LANG_QUIT_WRONG_USE_ERROR);
    return true;
}

/// Exit the realm
bool ChatHandler::HandleServerExitCommand(char* /*args*/)
{
    SendSysMessage(LANG_COMMAND_EXIT);
    World::StopNow(SHUTDOWN_EXIT_CODE);
    return true;
}

/// Create an account
bool ChatHandler::HandleAccountCreateCommand(char* args)
{
    ///- %Parse the command line arguments
    char *szAcc = ExtractQuotedOrLiteralArg(&args);
    char *szPassword = ExtractQuotedOrLiteralArg(&args);
    if(!szAcc || !szPassword)
        return false;

    // normalized in accmgr.CreateAccount
    std::string account_name = szAcc;
    std::string password = szPassword;

    AccountOpResult result = sAccountMgr.CreateAccount(account_name, password);
    switch(result)
    {
        case AOR_OK:
            PSendSysMessage(LANG_ACCOUNT_CREATED,account_name.c_str());
            break;
        case AOR_NAME_TOO_LONG:
            SendSysMessage(LANG_ACCOUNT_TOO_LONG);
            SetSentErrorMessage(true);
            return false;
        case AOR_NAME_ALREDY_EXIST:
            SendSysMessage(LANG_ACCOUNT_ALREADY_EXIST);
            SetSentErrorMessage(true);
            return false;
        case AOR_DB_INTERNAL_ERROR:
            PSendSysMessage(LANG_ACCOUNT_NOT_CREATED_SQL_ERROR,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage(LANG_ACCOUNT_NOT_CREATED,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    return true;
}



#ifdef linux
// Non-blocking keypress detector, when return pressed, return 1, else always return 0
int kb_hit_return()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO+1, &fds, nullptr, nullptr, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}
#endif

/// %Thread start
void CliRunnable::operator()()
{
    thread_name("CLI");
    ///- Init new SQL thread for the world database (one connection call enough)
    WorldDatabase.ThreadStart();                                // let thread do safe mySQL requests

    char commandbuf[256];

    ///- Display the list of available CLI functions then beep
    

    if (sConfig.GetBoolDefault("BeepAtStart", true))
        printf("\a");                                       // \a = Alert

    // print this here the first time
    // later it will be printed after command queue updates
    printf("mangos>");

    ///- As long as the World is running (no World::m_stopEvent), get the command line and handle it
    while (!World::IsStopped())
    {
        fflush(stdout);
#ifdef linux
        while (!kb_hit_return() && !World::IsStopped())
            // With this, we limit CLI to 10commands/second
            usleep(100);
        if (World::IsStopped())
            break;
#endif
#ifndef WIN32

        int retval;
        do
        {
            fd_set rfds;
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            FD_ZERO(&rfds);
            FD_SET(0, &rfds);

            retval = select(1, &rfds, nullptr, nullptr, &tv);
        } while (!retval);

        if (retval == -1)
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
            break;
        }
#endif
        char *command_str = fgets(commandbuf,sizeof(commandbuf),stdin);
        if (command_str != nullptr)
        {
            for(int x=0;command_str[x];x++)
                if(command_str[x]=='\r'||command_str[x]=='\n')
            {
                command_str[x]=0;
                break;
            }

            if(!*command_str)
            {
                printf("mangos>");
                continue;
            }

            std::string command;
            if(!consoleToUtf8(command_str,command))         // convert from console encoding to utf8
            {
                printf("mangos>");
                continue;
            }

            sWorld.QueueCliCommand(new CliCommandHolder(0, SEC_CONSOLE, nullptr, command.c_str(), &utf8print, &commandFinished));
        }
        else if (feof(stdin))
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
        }
    }

    ///- End the database thread
    WorldDatabase.ThreadEnd();                                  // free mySQL thread resources
}
