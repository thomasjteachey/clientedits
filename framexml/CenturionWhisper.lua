-- CENTURION: whispers to "First Last" (game/Miscellaneous/Surnames.h on the server).
--
-- Every character has a family name on the Centurion realms, and a name is the
-- pair. The stock whisper box only keeps a two-word target together when the
-- pair is on its autocomplete list (friends, guild, group); anybody else was cut
-- at the space, leaving the header reading "Tell Elgrom:" with "Fernbloom" at the
-- front of the message. Clicking a name in chat did the same, because
-- ChatFrame_SendTell types "/w <name> " into the box and parses it like anything
-- else typed there.
--
-- So on those realms the box waits for the second word and takes both. The
-- rare character with no family name (Chromi, for one) still gets the whisper:
-- the server hands a second word back to the message when the first alone is a
-- name and the pair is not (Surnames::SplitWhisperTarget).

local CENTURION_WHISPER_REALMS = { ["centurion"] = true, ["centuriondev"] = true };

local function CenturionWhisper_IsCenturionRealm()
	local realm = GetRealmName and GetRealmName();
	return type(realm) == "string" and CENTURION_WHISPER_REALMS[strlower(realm)] == true;
end

local stockExtractTellTarget = ChatEdit_ExtractTellTarget;

function ChatEdit_ExtractTellTarget(editBox, msg)
	if ( not CenturionWhisper_IsCenturionRealm() ) then
		return stockExtractTellTarget(editBox, msg);
	end

	local target = strmatch(msg, "^%s*(.*)");
	-- Link-style targets ("|K...") are the stock code's business.
	if ( not target or strsub(target, 1, 1) == "|" ) then
		return stockExtractTellTarget(editBox, msg);
	end

	-- Nothing is decided until both words are typed and a space follows them;
	-- until then the player is still typing the name.
	local first, second, rest = strmatch(target, "^(%S+)%s+(%S+)%s(.*)$");
	if ( not first ) then
		return false;
	end

	editBox:SetAttribute("tellTarget", first.." "..second);
	editBox:SetAttribute("chatType", "WHISPER");
	editBox:SetText(rest);
	ChatEdit_UpdateHeader(editBox);
	return true;
end
