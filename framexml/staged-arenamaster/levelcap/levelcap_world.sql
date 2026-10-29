-- Centurion's level cap is 60 (owner 2026-10-29: "the max level is 60. so
-- 60-69 and 60-79 and 10-80 don't make sense"), and Tanaris leaves the
-- Battlemaster's list ("remoove tanaris from the bg list..."). One world DB at
-- a time; then .reload battleground_template and .reload disables.
--
-- A range that starts at or below 60 ends at 60. Ranges wholly above it
-- (Slavery Valley 71-80, Random 80-80) are left: nothing reaches them.

CREATE TABLE IF NOT EXISTS battleground_template_bak_levelcap_20261029 LIKE battleground_template;
INSERT IGNORE INTO battleground_template_bak_levelcap_20261029 SELECT * FROM battleground_template;

UPDATE battleground_template SET MaxLvl = 60 WHERE MaxLvl > 60 AND MinLvl <= 60;

-- Tanaris Deathmatch (104): switched off as a battleground. It already had no
-- bracket at level 60 (PvpDifficulty 80-80) and is off the custom-game list.
DELETE FROM disables WHERE sourceType = 3 AND entry = 104;
INSERT INTO disables (sourceType, entry, flags, params_0, params_1, comment)
    VALUES (3, 104, 0, '', '', 'Tanaris Deathmatch - off the Battlemaster list (owner 2026-10-29)');

SELECT ID, Comment, MinLvl, MaxLvl FROM battleground_template WHERE MaxLvl > 60 OR ID IN (1, 2, 104, 107, 890) ORDER BY ID;
SELECT sourceType, entry, comment FROM disables WHERE sourceType = 3;
