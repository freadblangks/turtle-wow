
-- Changes by SHANG
REPLACE INTO `creature` VALUES (2583648,62020,0,0,0,451,15675.8,16545.2,48.6117,2.28582,120,120,0,100,100,0,0,0);
DELETE FROM creature WHERE guid=2583648;
DELETE FROM creature_addon WHERE guid=2583648;
DELETE FROM creature_movement WHERE id=2583648;
DELETE FROM game_event_creature WHERE guid=2583648;
DELETE FROM game_event_creature_data WHERE guid=2583648;
DELETE FROM creature_battleground WHERE guid=2583648;
