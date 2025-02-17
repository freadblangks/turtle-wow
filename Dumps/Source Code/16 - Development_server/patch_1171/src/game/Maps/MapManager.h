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

#ifndef MANGOS_MAPMANAGER_H
#define MANGOS_MAPMANAGER_H

#include "Common.h"
#include "Platform/Define.h"
#include "Policies/Singleton.h"
#include "Map.h"
#include "GridStates.h"
#include <condition_variable>

class BattleGround;

enum
{
    MAP0_TOP_NORTH      = 1,
    MAP0_MIDDLE_NORTH   = 2,
    MAP0_IRONFORGE_AREA = 3,
    MAP0_MIDDLE         = 4,    // Burning stepps, Redridge monts, Blasted lands
    MAP0_STORMWIND_AREA = 5,    // Stormwind, Elwynn forest, Redridge Mts
    MAP0_SOUTH          = 6,    // Southern phase of the continent

    MAP1_NORTH          = 11,   // Stonetalon, Ashenvale, Darkshore, Felwood, Moonglade, Winterspring, Azshara, Desolace
    MAP1_DUROTAR        = 12,   // Durotar
    MAP1_UPPER_MIDDLE   = 13,   // Mulgore, Barrens, Dustwallow Marsh
    MAP1_LOWER_MIDDLE   = 14,   // Feralas, 1K needles
    MAP1_VALLEY         = 15,   // Orc and Troll starting area
    MAP1_ORGRIMMAR      = 16,   // Orgrimmar (on its own)
    MAP1_SOUTH          = 17,   // Silithus, Un'goro and Tanaris

    MAP0_FIRST          = 1,
    MAP0_LAST           = 10,
    MAP1_FIRST          = 11,
    MAP1_LAST           = 20,
};

struct MapID
{
    explicit MapID(uint32 id) : nMapId(id), nInstanceId(0) {}
    MapID(uint32 id, uint32 instid) : nMapId(id), nInstanceId(instid) {}

    bool operator<(const MapID& val) const
    {
        if(nMapId == val.nMapId)
            return nInstanceId < val.nInstanceId;

        return nMapId < val.nMapId;
    }

    bool operator==(const MapID& val) const { return nMapId == val.nMapId && nInstanceId == val.nInstanceId; }

    uint32 nMapId;
    uint32 nInstanceId;
};

class ThreadPool;
struct ScheduledTeleportData;

class MapManager : public MaNGOS::Singleton<MapManager, MaNGOS::ClassLevelLockable<MapManager, std::recursive_mutex> >
{
    friend class MaNGOS::OperatorNew<MapManager>;

    using  LOCK_TYPE = std::recursive_mutex;
    using LOCK_TYPE_GUARD = std::unique_lock<LOCK_TYPE>;
    typedef MaNGOS::ClassLevelLockable<MapManager, std::recursive_mutex>::Lock Guard;

    public:
        typedef std::map<MapID, Map* > MapMapType;

        void GetOrCreateContinentInstances(uint32 mapId, WorldObject* obj, std::unordered_set<Map*>& instances);
        uint32 GetContinentInstanceId(uint32 mapId, float x, float y, bool* transitionArea = nullptr);
        Map* CreateMap(uint32 mapId, const WorldObject* obj);
        Map* CreateBgMap(uint32 mapId, BattleGround* bg);
        Map* CreateTestMap(uint32 mapId, bool instanced, float posX, float posY);
        void DeleteTestMap(Map* map);
        Map* FindMap(uint32 mapId, uint32 instanceId = 0) const;
        void ScheduleNewWorldOnFarTeleport(Player* pPlayer);
        void CancelInstanceCreationForPlayer(Player* pPlayer) { m_scheduledNewInstancesForPlayers.erase(pPlayer); }

        void UpdateGridState(grid_state_t state, Map& map, NGridType& ngrid, GridInfo& ginfo, const uint32 &x, const uint32 &y, const uint32 &t_diff);

        // only const version for outer users
        void DeleteInstance(uint32 mapId, uint32 instanceId);

        void Initialize(void);
        void Update(uint32);

        std::vector<std::pair<float, float>> GetBorderPoints(uint32 mapId);

        void SetGridCleanUpDelay(uint32 t)
        {
            if( t < MIN_GRID_DELAY )
                i_gridCleanUpDelay = MIN_GRID_DELAY;
            else
                i_gridCleanUpDelay = t;
        }

        void SetMapUpdateInterval(uint32 t)
        {
            if( t > MIN_MAP_UPDATE_DELAY )
                t = MIN_MAP_UPDATE_DELAY;

            i_timer.SetInterval(t);
            i_timer.Reset();
        }

        //void LoadGrid(int mapId, int instId, float x, float y, const WorldObject* obj, bool no_unload = false);
        void UnloadAll();

        static bool ExistMapAndVMap(uint32 mapId, float x, float y);
        static bool IsValidMAP(uint32 mapId);

        static bool IsValidMapCoord(uint32 mapId, float x,float y)
        {
            return IsValidMAP(mapId) && MaNGOS::IsValidMapCoord(x,y);
        }

        static bool IsValidMapCoord(uint32 mapId, float x,float y,float z)
        {
            return IsValidMAP(mapId) && MaNGOS::IsValidMapCoord(x,y,z);
        }

        static bool IsValidMapCoord(uint32 mapId, float x,float y,float z,float o)
        {
            return IsValidMAP(mapId) && MaNGOS::IsValidMapCoord(x,y,z,o);
        }

        static bool IsValidMapCoord(WorldLocation const& loc)
        {
            return IsValidMapCoord(loc.mapId,loc.x,loc.y,loc.z,loc.o);
        }

        // modulos a radian orientation to the range of 0..2PI
        static float NormalizeOrientation(float o)
        {
            // fmod only supports positive numbers. Thus we have
            // to emulate negative numbers
            if(o < 0)
            {
                float mod = o *-1;
                mod = fmod(mod, 2.0f*M_PI_F);
                mod = -mod+2.0f*M_PI_F;
                return mod;
            }
            return fmod(o, 2.0f*M_PI_F);
        }

        void RemoveAllObjectsInRemoveList();

        bool CanPlayerEnter(uint32 mapId, Player* player);
        uint32 GenerateInstanceId() { return ++i_MaxInstanceId; }
        void InitMaxInstanceId();
        void InitializeVisibilityDistanceInfo();

        /* statistics */
        uint32 GetNumInstances();
        uint32 GetNumPlayersInInstances();


        //get list of all maps
        const MapMapType& Maps() const { return i_maps; }

        template<typename Do>
        void DoForAllMapsWithMapId(uint32 mapId, Do& _do);

        void ScheduleInstanceSwitch(Player* player, uint16 newInstance);
        void SwitchPlayersInstances();

        void ScheduleFarTeleport(Player* player, ScheduledTeleportData* data);
        void ExecuteDelayedPlayerTeleports();
        void ExecuteSingleDelayedTeleport(Player *player);
        void CancelDelayedPlayerTeleport(Player *player);
        void MarkContinentUpdateFinished();
        bool IsContinentUpdateFinished() const;

        bool waitContinentUpdateFinishedFor(std::chrono::milliseconds time) const;
        bool waitContinentUpdateFinishedUntil(std::chrono::high_resolution_clock::time_point time) const;
    private:

        // debugging code, should be deleted some day
        GridState* si_GridStates[MAX_GRID_STATE];
        int i_GridStateErrorCount = 0;

    private:

        MapManager();
        ~MapManager();

        MapManager(const MapManager &);
        MapManager& operator=(const MapManager &);

        void InitStateMachine();
        void DeleteStateMachine();

        Map* CreateInstance(uint32 id, Player * player);
        DungeonMap* CreateDungeonMap(uint32 id, uint32 InstanceId, DungeonPersistentState *save = nullptr);
        BattleGroundMap* CreateBattleGroundMap(uint32 id, uint32 InstanceId, BattleGround* bg);

        uint32 i_gridCleanUpDelay;
        MapMapType i_maps;
        IntervalTimer i_timer;

        uint32 i_MaxInstanceId;
        int i_maxContinentThread = 0;

        mutable std::mutex m_continentMutex;
        mutable std::condition_variable m_continentCV;
        std::atomic<int> i_continentUpdateFinished{0};

        std::unique_ptr<ThreadPool> m_threads;
        std::unique_ptr<ThreadPool> m_continentThreads;
        bool asyncMapUpdating = false;

        // Instanced continent zones
        const static int LAST_CONTINENT_ID = 2;
        std::mutex m_scheduledInstanceSwitches_lock[LAST_CONTINENT_ID];
        std::map<Player*, uint16 /* new instance */> m_scheduledInstanceSwitches[LAST_CONTINENT_ID]; // 2 continents

        // Handle creation of new maps for teleport while continents are being updated.
        void CreateNewInstancesForPlayers();
        void CreateNewInstancesForPlayersSync();
        std::unordered_set<Player*> m_scheduledNewInstancesForPlayers;

        std::mutex m_scheduledFarTeleportsLock;
        typedef std::map<Player*, ScheduledTeleportData*> ScheduledTeleportMap;
        ScheduledTeleportMap m_scheduledFarTeleports;

        void ExecuteSingleDelayedTeleport(ScheduledTeleportMap::iterator iter);
};

template<typename Do>
void MapManager::DoForAllMapsWithMapId(uint32 mapId, Do& _do)
{
    MapMapType::const_iterator start = i_maps.lower_bound(MapID(mapId, 0));
    MapMapType::const_iterator end = i_maps.lower_bound(MapID(mapId + 1, 0));
    for (auto itr = start; itr != end; ++itr)
        _do(itr->second);
}

#define sMapMgr MapManager::Instance()

#endif
