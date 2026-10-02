-- Loads the real staged CenturionChatFilters.lua against mocked client globals
-- and checks exactly which chat lines it swallows.
strfind = string.find
local registered = {}
function ChatFrame_AddMessageEventFilter(event, fn) registered[event] = fn end

dofile(FILE)

local pass, fail, log = 0, 0, {}
local function check(what, got, want)
  if got == want then pass = pass + 1
  else fail = fail + 1; log[#log+1] = ("FAIL %s: got %s want %s"):format(what, tostring(got), tostring(want)) end
end

check("registered for CHAT_MSG_RAID_BOSS_EMOTE", registered["CHAT_MSG_RAID_BOSS_EMOTE"] ~= nil, true)
check("registered for nothing else", next(registered, "CHAT_MSG_RAID_BOSS_EMOTE"), nil)

local f = registered["CHAT_MSG_RAID_BOSS_EMOTE"]
-- swallowed: every second the Violet Hold countdown prints
for _, s in ipairs({"Next wave in 30...", "Next wave in 5...", "Next wave in 1..."}) do
  check("swallows '"..s.."'", f(nil, "CHAT_MSG_RAID_BOSS_EMOTE", s), true)
end
-- kept: anything else that arrives as a raid boss emote
for _, s in ipairs({
    "Next wave in 5 seconds!",           -- reworded, prints (and tells us to fix the pattern)
    "The next wave arrives in 10 seconds.",
    "Ichoron begins to cast Drained!",
    "A wave crashes over you. Next wave in 5...",  -- not the whole line
    "Next wave in ...",
    "",
  }) do
  check("keeps '"..s.."'", f(nil, "CHAT_MSG_RAID_BOSS_EMOTE", s), false)
end
check("survives a nil message", f(nil, "CHAT_MSG_RAID_BOSS_EMOTE", nil), false)

OUT = ("pass=%d fail=%d\n%s"):format(pass, fail, table.concat(log, "\n"))
