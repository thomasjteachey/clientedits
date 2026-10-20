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
-- name and the pair is not (Surnames::SplitWhisperTarget). A one-word message
-- sent with Enter is handled by the ChatEdit_SendText hook at the bottom.

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

-- Enter pressed while the box is still waiting for that space: "/w Chromi hi"
-- is a one-word whisper to a one-word name, and it used to go nowhere at all.
-- The only reading that can be sent is the first word as the target and the
-- second as the message; the server still takes a first name alone when only
-- one character answers to it.
local function CenturionWhisper_IsWhisperCommand(command)
	command = strupper(command);
	local i = 1;
	while ( _G["SLASH_WHISPER"..i] ) do
		if ( strupper(_G["SLASH_WHISPER"..i]) == command ) then
			return true;
		end
		i = i + 1;
	end
	return false;
end

local stockSendText = ChatEdit_SendText;

function ChatEdit_SendText(editBox, addHistory)
	if ( CenturionWhisper_IsCenturionRealm() ) then
		local command, first, second = strmatch(editBox:GetText(), "^%s*(/%S+)%s+([^%s|]%S*)%s+(%S+)%s*$");
		if ( command and CenturionWhisper_IsWhisperCommand(command) ) then
			editBox:SetAttribute("tellTarget", first);
			editBox:SetAttribute("chatType", "WHISPER");
			editBox:SetText(second);
		end
	end
	return stockSendText(editBox, addHistory);
end
