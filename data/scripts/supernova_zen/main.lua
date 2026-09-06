-- ============================================================
-- Supernova Zen Reactor
-- Bejeweled Twist Extender - Lua layer
--
-- The reactor core lives in the native extender
-- (TwistExtensions/Reactor). It pulses on the board update loop,
-- scans the board for safe spawn positions, enforces limits and
-- cooldowns, and ascends through reactor levels on its own.
--
-- This script is the "secret handshake": it tunes the reactor
-- and binds the cosmic keys. Nothing here can corrupt the board;
-- every native call is guarded.
--
-- Keys:
--   Z - toggle the reactor
--   S - summon a Supernova
--   D - awaken the Forbidden Doom Gem (respecting its cooldown)
--   R - reset the reactor cycle
--   I - peer into the reactor (status report)
-- ============================================================


-- =========================
-- Configuration
-- =========================
-- These are the defaults; the native core clamps anything unsafe.

setZenReactorConfig({
    enabled       = true,
    maxSupernovas = 8,     -- concurrent Supernovas on the board
    maxDoom       = 1,     -- the Doom Gem is legendary: one at a time
    spawnDelay    = 120,   -- frames between reactor pulses (level 1)
    doomChance    = 0.01,  -- chance per pulse for the Doom Gem to awaken
    doomCooldown  = 3600,  -- frames before the Doom Gem may be courted again
    chaosMode     = false  -- maximum chaos mode (see setChaosMode below)
})

print("[ZEN] ...something stirs beneath the board.")


-- =========================
-- Keyboard controls
-- =========================

local KEY_Z = 90  -- toggle reactor
local KEY_S = 83  -- spawn Supernova
local KEY_D = 68  -- spawn Doom
local KEY_R = 82  -- reset reactor
local KEY_I = 73  -- status report

function onKeyPress(key)
    if key == KEY_Z then
        local enabled = toggleZenReactor()
        if enabled then
            print("[ZEN] The cosmic heart begins to beat.")
        else
            print("[ZEN] Silence returns to the void.")
        end

    elseif key == KEY_S then
        if not spawnSupernova() then
            print("[ZEN] The stars refuse. (limit reached, board full, or not in game)")
        end

    elseif key == KEY_D then
        if not spawnDoom() then
            print("[ZEN] The Forbidden Gem slumbers on. (already present, cooling down, or not in game)")
        end

    elseif key == KEY_R then
        resetReactor()
        lastKnownLevel = 1
        print("[ZEN] The cycle begins anew.")

    elseif key == KEY_I then
        local s = getReactorStatus()
        print(string.format(
            "[ZEN] level %d (%s) | events %d | live supernovas %d | doom %d | pulses %d | next level at %s | chaos %s",
            s.level, s.levelName, s.supernovaEvents, s.liveSupernovas,
            s.liveDooms, s.pulses, tostring(s.nextLevelAt), tostring(s.chaosMode)))
    end
end


-- =========================
-- Board update
-- =========================
-- The native reactor does the heavy lifting on its own update hook.
-- Here we only listen for ascensions and announce them in style.

lastKnownLevel = 1

function onBoardUpdate()
    local s = getReactorStatus()

    if s.level > lastKnownLevel then
        lastKnownLevel = s.level
        if s.level == 4 then
            print("[ZEN] COSMIC INSTABILITY. Reality frays at the edges.")
        else
            print("[ZEN] The reactor ascends to level " .. s.level .. ".")
        end
    elseif s.level < lastKnownLevel then
        lastKnownLevel = s.level
    end
end
