-- The Battlemaster (owner 2026-10-29: "add a similar bg frame with a bg
-- battlemaster (get rid of whitemane and aethalas)"). One world DB at a time
-- (centuriondevworld, then centurionworld). Every battleground battlemaster
-- opens the Battlemaster window; the hub keeps one, in both of its phases.
--
--   920151  Aethalas (WSG battlemaster, phase 65536)  -> the hub's Battlemaster
--   921151  its phase-262144 copy                      -> gone
--   920199, 921199  High Inquisitor Whitemane (Scarlet Chapel queue, both phases) -> gone

CREATE TABLE IF NOT EXISTS creature_bak_bgmaster_20261029 LIKE creature;
INSERT IGNORE INTO creature_bak_bgmaster_20261029
    SELECT * FROM creature WHERE guid IN (920151, 921151, 920199, 921199);
CREATE TABLE IF NOT EXISTS creature_template_bak_bgmaster_20261029 LIKE creature_template;
INSERT IGNORE INTO creature_template_bak_bgmaster_20261029
    SELECT * FROM creature_template WHERE entry = 920006;

DELETE FROM creature WHERE guid IN (921151, 920199, 921199);
DELETE FROM creature_addon WHERE guid IN (921151, 920199, 921199);

UPDATE creature SET phaseMask = 327680 WHERE guid = 920151;
UPDATE creature_template SET name = 'Battlemaster', subname = 'Battlegrounds' WHERE entry = 920006;

SELECT COUNT(*) AS backed_up FROM creature_bak_bgmaster_20261029;
SELECT guid, id, phaseMask FROM creature WHERE guid IN (920151, 921151, 920199, 921199);
SELECT entry, name, subname FROM creature_template WHERE entry = 920006;
