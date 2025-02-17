/* Copyright (C) 2006 - 2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

/* ScriptData
SDName: Uldaman
SD%Complete: 100
SDComment: Quest support: 2278 + 1 trash mob.
SDCategory: Uldaman
EndScriptData */

/* ContentData
mob_jadespine_basilisk
npc_lore_keeper_of_norgannon
EndContentData */

#include "scriptPCH.h"
#include "uldaman.h"

/*######
 ## go_keystone_chamber
 ######*/

bool GOHello_go_keystone_chamber(Player* pPlayer, GameObject* pGo)
{
    ScriptedInstance* pInstance = (ScriptedInstance*)pGo->GetInstanceData();

    if (!pInstance)
        return false;

    if (pPlayer)
        pInstance->SetData64(0, pPlayer->GetGUID()); // Ironaya first victim

    if (pGo)
        pGo->SetUInt32Value(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND);

    // save state
    pInstance->SetData(ULDAMAN_ENCOUNTER_IRONAYA_DOOR, DONE);

    return false;
}

// Return true to avoid db script attempt
bool ProcessEventId_event_awaken_stone_keeper(uint32 eventId, Object* source, Object* target, bool isStart)
{
    if (!source || source->GetTypeId() != TYPEID_PLAYER)
        return true;

    if (!target)
        return true;

    if (ScriptedInstance* instance = dynamic_cast<ScriptedInstance*>(((Player*)source)->GetInstanceData()))
        instance->SetData(ULDAMAN_ENCOUNTER_STONE_KEEPERS, IN_PROGRESS);

    return true;
}

/*######
## mob_jadespine_basilisk
######*/

struct mob_jadespine_basiliskAI : public ScriptedAI
{
    mob_jadespine_basiliskAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        Reset();
    }

    uint32 Cslumber_Timer;

    void Reset() override
    {
        Cslumber_Timer = 2000;
    }

    void UpdateAI(const uint32 diff) override
    {
        //Return since we have no target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
        {
            return;
        }

        //Cslumber_Timer
        if (Cslumber_Timer < diff)
        {
            //Cast
            m_creature->CastSpell(m_creature->GetVictim(), SPELL_CRYSTALLINE_SLUMBER, false);
            m_creature->GetThreatManager().modifyThreatPercent(m_creature->GetVictim(), -100);

            //Stop attacking target thats asleep and pick new target
            Cslumber_Timer = 28000;

            Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_TOPAGGRO, 0);

            if (!pTarget || pTarget == m_creature->GetVictim())
                pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_TOPAGGRO, 1);

            if (pTarget)
                AttackStart(pTarget);
        }
        else Cslumber_Timer -= diff;

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_mob_jadespine_basilisk(Creature* pCreature)
{
    return new mob_jadespine_basiliskAI(pCreature);
}

/*######
## npc_lore_keeper_of_norgannon
######*/

bool GossipHello_npc_lore_keeper_of_norgannon(Player* pPlayer, Creature* pCreature)
{
    if (pPlayer->GetQuestStatus(2278) == QUEST_STATUS_INCOMPLETE)
    {
        pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Who are the Earthen?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
    }

//    pPlayer->SEND_GOSSIP_MENU(1079, pCreature->GetGUID());

    pPlayer->SEND_GOSSIP_MENU(pPlayer->GetGossipTextId(pCreature), pCreature->GetObjectGuid());

    return true;
}

bool GossipSelect_npc_lore_keeper_of_norgannon(Player* pPlayer, Creature* pCreature, uint32 uiSender, uint32 uiAction)
{
    switch (uiAction)
    {
        case GOSSIP_ACTION_INFO_DEF+1:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "What is a \"subterranean being matrix\"?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
            pPlayer->SEND_GOSSIP_MENU(1080, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+2:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "What are the anomalies you speak of?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
            pPlayer->SEND_GOSSIP_MENU(1081, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+3:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "What is a resilient foundation of construction?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
            pPlayer->SEND_GOSSIP_MENU(1082, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+4:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "So... the Earthen were made out of stone?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);
            pPlayer->SEND_GOSSIP_MENU(1083, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+5:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Anything else I should know about the Earthen?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 6);
            pPlayer->SEND_GOSSIP_MENU(1084, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+6:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "I think I understand the Creators' design intent for the Earthen now. What are the Earthen's anomalies that you spoke of earlier?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 7);
            pPlayer->SEND_GOSSIP_MENU(1085, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+7:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "What high-stress environments would cause the Earthen to destabilize?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 8);
            pPlayer->SEND_GOSSIP_MENU(1086, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+8:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "What happens when the Earthen destabilize?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 9);
            pPlayer->SEND_GOSSIP_MENU(1087, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+9:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Troggs?! Are the troggs you mention the same as the ones in the world today?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 10);
            pPlayer->SEND_GOSSIP_MENU(1088, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+10:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "You mentioned two results when the Earthen destabilize. What is the second?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 11);
            pPlayer->SEND_GOSSIP_MENU(1089, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+11:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Dwarves!!! Now you're telling me that dwarves originally came from the Earthen?!", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 12);
            pPlayer->SEND_GOSSIP_MENU(1090, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+12:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "These dwarves are the same ones today, yes? Do the dwarves maintain any other links to the Earthen?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 13);
            pPlayer->SEND_GOSSIP_MENU(1091, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+13:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Who are the Creators?", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 14);
            pPlayer->SEND_GOSSIP_MENU(1092, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+14:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "This is a lot to think about.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 15);
            pPlayer->SEND_GOSSIP_MENU(1093, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+15:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "I will access the discs now.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 16);
            pPlayer->SEND_GOSSIP_MENU(1094, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+16:
            pPlayer->CLOSE_GOSSIP_MENU();
            pPlayer->AreaExploredOrEventHappens(2278);
            break;
    }

    return true;
}

struct AnnoraAI : public ScriptedAI
{
    AnnoraAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_creature->SetVisibility(VISIBILITY_OFF);
        m_creature->SetFactionTemplateId(FACTION_STONED);
        m_uiNbScorpion = 0;
        isSpawned = false;
        Reset();
    }

    uint32 m_uiNbScorpion;
    bool isSpawned;

    void Reset() override
    {
    }

    void Aggro(Unit* pWho) override
    {
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!isSpawned)
        {
            std::list<Creature*> m_EscortList;
            m_uiNbScorpion = 0;

            GetCreatureListWithEntryInGrid(m_EscortList, m_creature, 7078, 30.0f);
            for (const auto& it : m_EscortList)
            {
                if (it->IsAlive())
                {
                    m_uiNbScorpion++;
                }
            }
            m_EscortList.clear();

            if (m_uiNbScorpion == 0)
            {
                m_creature->SetVisibility(VISIBILITY_ON);
                m_creature->GetMotionMaster()->MovePoint(1, -164.3657f, 210.7687f, -49.572f);
                isSpawned = true;
            }
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
        {
            return;
        }

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_annora(Creature* pCreature)
{
    return new AnnoraAI(pCreature);
}

enum
{
    SPELL_FIRE_SHIELD       =   2602,
    SPELL_FLAME_BUFFET      =   10452,

};

void AddSC_uldaman()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "mob_annora";
    newscript->GetAI = &GetAI_annora;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "mob_jadespine_basilisk";
    newscript->GetAI = &GetAI_mob_jadespine_basilisk;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_lore_keeper_of_norgannon";
    newscript->pGossipHello = &GossipHello_npc_lore_keeper_of_norgannon;
    newscript->pGossipSelect = &GossipSelect_npc_lore_keeper_of_norgannon;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_keystone_chamber";
    newscript->pGOHello = &GOHello_go_keystone_chamber;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "event_awaken_stone_keeper";
    newscript->pProcessEventId = &ProcessEventId_event_awaken_stone_keeper;
    newscript->RegisterSelf();
}
