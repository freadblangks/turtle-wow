
-- Changes by VOLCA
REPLACE INTO `creature` VALUES (2577387,14368,0,0,0,429,296.276,-17.0893,-3.09262,1.64384,120,120,5,100,100,1,0,0);
REPLACE INTO `creature` VALUES (2577388,14368,0,0,0,1,16212.1,16227.5,4.94251,1.13483,120,120,5,100,100,1,0,0);
DELETE FROM creature WHERE guid=2577388;
DELETE FROM creature_addon WHERE guid=2577388;
DELETE FROM creature_movement WHERE id=2577388;
DELETE FROM game_event_creature WHERE guid=2577388;
DELETE FROM game_event_creature_data WHERE guid=2577388;
DELETE FROM creature_battleground WHERE guid=2577388;

-- Changes by SHANG
REPLACE INTO `creature` VALUES (2577389,61287,0,0,0,0,-1844.77,1855.75,64.6029,0.505751,120,120,0,100,100,0,0,0);
REPLACE INTO `creature_addon` (`guid`, `stand_state`) VALUES (2577389, 2);
REPLACE INTO `creature_addon` (`guid`, `stand_state`) VALUES (2577389, 1);
REPLACE INTO `gameobject` VALUES ( 5015510, 2001789, 0, -1843.75, 1853.51, 64.3471, 0.904735, 0, 0, 0.437096, 0.899415, 300, 300, 100, 1, 0, 0);
REPLACE INTO `object_scaling` (`fullGuid`, `scale`) VALUES (17370417347219458006, 0.800000);
REPLACE INTO `gameobject` VALUES ( 5015510, 2001789, 0, -1843.75, 1853.51, 64.3471, 0.904735, 0, 0, 0.437096, 0.899415, 300, 300, 100, 1, 0, 0);
