-- Which physical flag each stock carrier marker shows: "A", "H" or nil when it
-- cannot be told. Stock markers come in client slot order (see the note at the
-- top): with two carriers the first is the Horde flag's, the second the
-- Alliance flag's. A single marker is whichever flag CWSG says is carried.
local function CenturionCTF_GetStockSides(stockCount)
	local sides = {};
	if ( stockCount >= 2 ) then
		sides[1] = "H";
		sides[2] = "A";
		return sides;
	end
	if ( stockCount ~= 1 ) then
		return sides;
	end

	if ( CenturionCTF_IsAuthReady() ) then
		local aPlayer = CENTURION_WSG_AUTH.AllianceState == "PLAYER";
		local hPlayer = CENTURION_WSG_AUTH.HordeState == "PLAYER";
		if ( aPlayer and not hPlayer ) then
			sides[1] = "A";
			return sides;
		elseif ( hPlayer and not aPlayer ) then
			sides[1] = "H";
			return sides;
		elseif ( aPlayer and hPlayer and CENTURION_WSG_AUTH.LastSingleSide ) then
			-- The second pickup has not reached the client's slots yet, so the
			-- one marker is still the flag that was carried alone before it.
			sides[1] = CENTURION_WSG_AUTH.LastSingleSide;
			return sides;
		end
	end

	local singleHeldToken = CenturionCTF_GetSingleHeldTokenFromWorldStates();
	if ( singleHeldToken == "AllianceFlag" ) then
		sides[1] = "A";
	elseif ( singleHeldToken == "HordeFlag" ) then
		sides[1] = "H";
	end
	return sides;
end

function CenturionWSG_GetFixedTokens(battlefieldFlags)
	local fixed = {};
	if ( not battlefieldFlags or not CenturionCTF_IsActive() ) then
		return fixed;
	end

	local stockCount = 0;
	for _, rawInfo in ipairs(battlefieldFlags) do
		if ( rawInfo.stock ) then
			stockCount = stockCount + 1;
		end
	end
	local sides = CenturionCTF_GetStockSides(stockCount);

	-- The client refreshes its carrier slots at most once every 5 seconds
	-- (Wow.exe 0x54CF60 throttles RequestBattlefieldPositions), but CWSG arrives
	-- the moment a flag changes hands. A stock marker whose flag CWSG no longer
	-- has on a player is the old carrier, so it is dropped instead of drawn.
	local authReady = CenturionCTF_IsAuthReady();
	local aPlayer = authReady and CENTURION_WSG_AUTH.AllianceState == "PLAYER";
	local hPlayer = authReady and CENTURION_WSG_AUTH.HordeState == "PLAYER";

	local kept = {};
	local seenStock = 0;
	for _, rawInfo in ipairs(battlefieldFlags) do
		local keep = true;
		local token = nil;
		if ( rawInfo.stock ) then
			seenStock = seenStock + 1;
			local side = sides[seenStock];
			if ( authReady ) then
				if ( side == "A" ) then
					keep = aPlayer;
				elseif ( side == "H" ) then
					keep = hPlayer;
				else
					keep = aPlayer or hPlayer;
				end
			end
			if ( side == "A" ) then
				token = "AllianceFlag";
			elseif ( side == "H" ) then
				token = "HordeFlag";
			else
				token = CenturionCTF_TokenFromPath(rawInfo.token) or rawInfo.token;
			end
		elseif ( rawInfo.authoritative and rawInfo.token ) then
			-- Authoritative CWSG entries are only used for dropped flags. Their
			-- token is already the physical flag identity.
			token = rawInfo.token;
		end
		if ( keep ) then
			table.insert(kept, { info = rawInfo, token = token });
		end
	end

	-- The map and minimap read this same list again right after the call, so a
	-- dropped stock marker is removed from it in place; the dropped-flag markers
	-- behind it move up into the two flag frames that get drawn.
	for i = #battlefieldFlags, 1, -1 do
		battlefieldFlags[i] = nil;
	end
	for i, entry in ipairs(kept) do
		battlefieldFlags[i] = entry.info;
		fixed[i] = entry.token;
	end
	return fixed;
end
