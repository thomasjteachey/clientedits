-- Loads the real CenturionNameLimits.lua (path in MODULE) against stubbed
-- FrameXML and checks every short name box now takes "First Last".
local fail, pass, log = 0, 0, {}
local function check(cond, what)
	if cond then pass = pass + 1 else fail = fail + 1; log[#log + 1] = "FAIL " .. what end
end

local mailMax = 12
SendMailNameEditBox = { SetMaxLetters = function(self, n) mailMax = n end }
StaticPopupDialogs = {
	ADD_GUILDMEMBER = { maxLetters = 12 },
	ADD_RAIDMEMBER  = { maxLetters = 12 },
	ADD_TEAMMEMBER  = { maxLetters = 12 },
	ADD_MUTE        = { maxLetters = 12 },
	ADD_FRIEND      = { maxLetters = 77 },
	RENAME_PET      = { maxLetters = 12 },
}

assert(loadfile(MODULE))()

local longest = "Aaaaaaaaaaaa Bbbbbbbbbbbb" -- 12 + 1 + 12
check(#longest == 25, "longest pair is 25")
check(mailMax >= #longest, "mail To: fits the longest pair, got " .. mailMax)
for _, k in ipairs({ "ADD_GUILDMEMBER", "ADD_RAIDMEMBER", "ADD_TEAMMEMBER", "ADD_MUTE" }) do
	check(StaticPopupDialogs[k].maxLetters >= #longest, k .. " fits the longest pair")
end
check(StaticPopupDialogs.ADD_FRIEND.maxLetters == 77, "ADD_FRIEND left alone")
check(StaticPopupDialogs.RENAME_PET.maxLetters == 12, "pet names untouched")

-- A client without one of the frames must not error.
SendMailNameEditBox = nil
StaticPopupDialogs = {}
local ok = pcall(assert(loadfile(MODULE)))
check(ok, "loads with the frames missing")

OUT = string.format("pass=%d fail=%d %s", pass, fail, table.concat(log, "; "))
