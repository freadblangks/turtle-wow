/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
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

#ifndef _MAP_BUILDER_H
#define _MAP_BUILDER_H

#include <vector>
#include <set>
#include <map>

#include "TerrainBuilder.h"
#include "IntermediateValues.h"

#include "TileBuilder.h"
#include "IVMapManager.h"
#include "WorldModel.h"

#include "Recast.h"
#include "DetourNavMesh.h"

using namespace VMAP;
// G3D namespace typedefs conflicts with ACE typedefs

namespace MMAP
{
    typedef std::map<uint32, std::set<uint32>*> TileList;
    struct Tile
    {
        Tile() : chf(NULL), solid(NULL), cset(NULL), pmesh(NULL), dmesh(NULL) {}
        ~Tile()
        {
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            rcFreeHeightField(solid);
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
        }
        rcCompactHeightfield* chf;
        rcHeightfield* solid;
        rcContourSet* cset;
        rcPolyMesh* pmesh;
        rcPolyMeshDetail* dmesh;
    };

    class MapBuilder
    {
        friend class TileBuilder;

        public:
            MapBuilder(bool skipLiquid = false, bool skipContinents = false, bool skipJunkMaps = true, bool skipBattlegrounds = false, bool debugOutput = false, bool bigBaseUnit = false, bool quick = false, const char* offMeshFilePath = nullptr);
            ~MapBuilder();

            // builds all mmap tiles for the specified map id (ignores skip settings)
            void buildMap(uint32 mapID);

            // builds an mmap tile for the specified map and its mesh
            void buildSingleTile(uint32 mapID, uint32 tileX, uint32 tileY);

            // builds list of maps, then builds all of mmap tiles (based on the skip settings)
            void buildAllMaps();

            void WaitForAllTilesToBeBuild();

            void ShutdownAsyncBuilders();

            void StartupAsyncBuilders();

            bool IsBusy();

            void buildGameObject(std::string modelName, uint32 displayId);
            void buildTransports();

        private:
            // detect maps and tiles
            void discoverTiles();
            std::set<uint32>* getTileList(uint32 mapID);

            void buildNavMesh(uint32 mapID, dtNavMesh*& navMesh);

            void getTileBounds(uint32 tileX, uint32 tileY,
                               float* verts, int vertCount,
                               float* bmin, float* bmax);
            void getGridBounds(uint32 mapID, uint32& minX, uint32& minY, uint32& maxX, uint32& maxY);

            bool shouldSkipMap(uint32 mapID);
            bool isTransportMap(uint32 mapID);
            

            TileList m_tiles;
            TerrainBuilder* m_terrainBuilder;
            rcContext* m_rcContext;

            bool m_debugOutput;

            const char* m_offMeshFilePath;
            bool m_skipContinents;
            bool m_skipJunkMaps;
            bool m_skipBattlegrounds;
            bool m_quick;
            bool m_bigBaseUnit;

            std::atomic<bool> m_cancel;
            std::vector<TileBuilder*> workers;

            ProducerConsumerQueue<TileInfo> m_tileQueue;
    };

    struct MapSettings
    {
        float agentMaxClimbModelTerrainTransition;
        float agentMaxClimbTerrain;
        float WalkableSlopeAngle;
        bool bIncludeLimitsOnRasterizeTriangles;
    };

    class MapBuilderConfig
    {
    public:

        void LoadConfigIfExist();

        const MapSettings* GetSettingsForMap(int MapID) const;

    protected:
        std::map<int, MapSettings> SettingCollection;
    };

    extern MapBuilderConfig gMMapBuilderConfig;
}

#endif
