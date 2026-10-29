CREATE TABLE IF NOT EXISTS battleground_template_bak_obc15_20261029 LIKE battleground_template;
INSERT IGNORE INTO battleground_template_bak_obc15_20261029 SELECT * FROM battleground_template WHERE ID = 102;
UPDATE battleground_template SET MaxPlayersPerTeam = 15 WHERE ID = 102;
SELECT ID, Comment, MinPlayersPerTeam, MaxPlayersPerTeam FROM battleground_template WHERE ID = 102;
