-- The Arena Master (owner 2026-10-29: "ONE arena master ... delete all the now
-- extraneous arena npcs"). One world DB at a time (centuriondevworld, then
-- centurionworld at the live deploy). Every plain arena battlemaster is the
-- Arena Master; the hub keeps one, in both of its phases.
--
--   921046  the hub's second Arena Battlemaster (phase 262144 copy of 920046)
--   920200, 921200  Roxi, Arena Replays, at the hub (one per phase)
--   213825, 213828  Roxi in Orgrimmar and Stormwind
--   923100  Warden Kalvane (Four Corners)
--   930000  Dealer Rixa (Buffle Shuffle)
--   931000  Champion Tavrin (All-Star)

CREATE TABLE IF NOT EXISTS creature_bak_arenamaster_20261029 LIKE creature;
INSERT IGNORE INTO creature_bak_arenamaster_20261029
    SELECT * FROM creature WHERE guid IN (920046, 921046, 920200, 921200, 213825, 213828, 923100, 930000, 931000);
CREATE TABLE IF NOT EXISTS creature_template_bak_arenamaster_20261029 LIKE creature_template;
INSERT IGNORE INTO creature_template_bak_arenamaster_20261029
    SELECT * FROM creature_template WHERE entry = 920085;

DELETE FROM creature WHERE guid IN (921046, 920200, 921200, 213825, 213828, 923100, 930000, 931000);
DELETE FROM creature_addon WHERE guid IN (921046, 920200, 921200, 213825, 213828, 923100, 930000, 931000);

-- The one left: both hub phases (65536 | 262144), and its name.
UPDATE creature SET phaseMask = 327680 WHERE guid = 920046;
UPDATE creature_template SET name = 'Arena Master', subname = 'Arenas and Replays' WHERE entry = 920085;

SELECT COUNT(*) AS backed_up FROM creature_bak_arenamaster_20261029;
SELECT guid, id, phaseMask FROM creature WHERE guid IN (920046, 921046, 920200, 921200, 213825, 213828, 923100, 930000, 931000);
SELECT entry, name, subname FROM creature_template WHERE entry = 920085;
