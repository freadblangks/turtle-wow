
-- Changes by GHEOR
REPLACE INTO `creature` VALUES (2573797,14444,0,0,0,451,16893.7,15651.9,67.9052,6.06234,120,120,5,100,100,1,0,0);
REPLACE INTO `creature` VALUES (2573798,14444,0,0,0,451,16891.9,15652.7,68.073,5.1929,120,120,5,100,100,1,0,0);
DELETE FROM creature WHERE guid=2573797;
DELETE FROM creature_addon WHERE guid=2573797;
DELETE FROM creature_movement WHERE id=2573797;
DELETE FROM game_event_creature WHERE guid=2573797;
DELETE FROM game_event_creature_data WHERE guid=2573797;
DELETE FROM creature_battleground WHERE guid=2573797;
DELETE FROM creature WHERE guid=2573798;
DELETE FROM creature_addon WHERE guid=2573798;
DELETE FROM creature_movement WHERE id=2573798;
DELETE FROM game_event_creature WHERE guid=2573798;
DELETE FROM game_event_creature_data WHERE guid=2573798;
DELETE FROM creature_battleground WHERE guid=2573798;

-- Changes by SHANG
REPLACE INTO `gameobject` VALUES ( 5012032, 2004552, 1, -8868.46, -6537.86, 13.3536, 4.88236, 0, 0, 0.644532, -0.764578, 300, 300, 100, 1, 0, 0);
REPLACE INTO `gameobject` VALUES ( 5012032, 2004552, 1, -8868.46, -6537.86, 13.3536, 1.81538, 0, 0, 0.788085, 0.615566, 300, 300, 100, 1, 0, 0);
DELETE FROM gameobject WHERE guid = '5012032';
DELETE FROM game_event_gameobject WHERE guid = '5012032';
DELETE FROM gameobject_battleground WHERE guid = '5012032';
REPLACE INTO `creature` VALUES (2573799,61134,0,0,0,1,-7570.41,-6809.21,33.6094,1.43175,120,120,0,100,100,0,0,0);
