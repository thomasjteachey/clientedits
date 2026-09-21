-- CENTURION: room for a first AND last name in the boxes that take one.
--
-- A Centurion character is named by the pair ("Elgrom Fernbloom"), and the pair
-- can run to 25 characters: a 12-letter first name, a space, a 12-letter family
-- name (game/Miscellaneous/Surnames.h on the server). Stock 3.3.5 stops these
-- boxes at 12, the old single-name limit, so a longer pair simply could not be
-- typed - reported first as a Mark of Honor that could not be mailed. The
-- server already takes the pair everywhere (mail, invites, mutes); only the
-- boxes were short.
--
-- Add Friend and Ignore allow 77 already and are left alone.

local CENTURION_FULL_NAME_LETTERS = 12 + 1 + 12;

-- MailFrame.xml: <EditBox name="SendMailNameEditBox" letters="12">
if ( SendMailNameEditBox ) then
	SendMailNameEditBox:SetMaxLetters(CENTURION_FULL_NAME_LETTERS);
end

-- StaticPopup.lua dialogs with maxLetters = 12. StaticPopup_Show applies
-- maxLetters to the edit box each time a dialog opens, so the table is what
-- has to change.
for _, which in ipairs({ "ADD_GUILDMEMBER", "ADD_RAIDMEMBER", "ADD_TEAMMEMBER", "ADD_MUTE" }) do
	local dialog = StaticPopupDialogs and StaticPopupDialogs[which];
	if ( dialog and (dialog.maxLetters or 0) < CENTURION_FULL_NAME_LETTERS ) then
		dialog.maxLetters = CENTURION_FULL_NAME_LETTERS;
	end
end
