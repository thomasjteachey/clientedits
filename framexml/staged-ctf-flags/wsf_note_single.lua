-- Remembers which flag was carried alone, so that while a second pickup has not
-- reached the client's carrier slots the one marker there keeps its flag.
local function CenturionCTF_NoteSingleCarrier()
	local aPlayer = CENTURION_WSG_AUTH.AllianceState == "PLAYER";
	local hPlayer = CENTURION_WSG_AUTH.HordeState == "PLAYER";
	if ( aPlayer and not hPlayer ) then
		CENTURION_WSG_AUTH.LastSingleSide = "A";
	elseif ( hPlayer and not aPlayer ) then
		CENTURION_WSG_AUTH.LastSingleSide = "H";
	elseif ( not aPlayer and not hPlayer ) then
		CENTURION_WSG_AUTH.LastSingleSide = nil;
	end
end

