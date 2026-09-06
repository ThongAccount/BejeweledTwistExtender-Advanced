# BejeweledTwistExtender
Plugin system for Bejeweled Twist based on the <a href="https://github.com/PlatinumTeam/MBExtender-Legacy">MBExtender</a> that allows developers to add additional functionality to the game by writing plugins in native C++ code.

Includes several plugins:
- Counter-Clockwise Twisting, on right mouse button and enter (return) key
- Autosaving for Blitz and timed Challenges
- Support for 16:9, 21:9 and other (ultra)wide screen resolutions
- 3D acceleration fix
- Skull and Angel gem crash fixes
- EXPERIMENTAL: Multi-Twist (twisting while other gems are in motion, disabled by default)

All plugins that change gameplay can be toggled in a simple config file that is auto-created in the game folder.

# Supernova Zen Reactor

A hidden cosmic gameplay mode. Not a cheat, not a debug tool — the board slowly, deliberately evolves as the reactor awakens: Supernovas drift in one by one, the reactor climbs through levels of power, and on rare occasions the Forbidden Doom Gem stirs.

## Installation

1. Build the solution (or grab the `BejeweledTwistExtender-build` artifact from GitHub Actions). You need `TwistExtensions.dll`.
2. Place `TwistExtensions.dll` next to `BejeweledTwist.exe` (the loader will pick it up as usual).
3. Copy the `data/scripts/supernova_zen` folder into the game's `data/scripts` folder.
4. Start the game. When the reactor wakes you will see `[ZEN]` messages in the extender console log.

## What it does

- **Supernovas become common** — the reactor pulses on the board update loop and creates one Supernova per pulse, up to a cap (default 8 on the board). The board evolves slowly; it never floods instantly.
- **The Doom Gem remains legendary** — maximum one Doom Gem, extremely low random chance, a long cooldown between awakening attempts, and a special announcement when it happens. Every Doom the reactor spawns arrives with a counter of **2**: it ticks down on matchless moves and detonates at zero — no second chance.
- **Spawner semantics** — the reactor never converts settled gems on a timer. Its pulse queues seeds, and the seeds ride gems that the game's own fall pipeline just delivered, exactly like the native skull/bomb spawners.
- **The board is respected** — the reactor scans the board and only spawns onto plain, ordinary gems. It never overwrites an existing special, never touches the gem under your cursor or inside the twist ring, and skips unmatchable slots.
- **The game remains playable** — every limit, timer and roll lives in guarded native code. The reactor cannot corrupt the board or crash the update loop.
- **Settle gate** — the reactor only ever acts on a fully settled board (every slot occupied, no twist in progress, and a short quiet period after). It never touches gems during clears, refills or falls, so cascades and full-board clears resolve normally.

## Reactor levels

The reactor ascends as Supernova events accumulate:

| Level | Name | Requires | Effect |
|-------|------|----------|--------|
| 1 | Dormant Ember | — | Normal Supernova generation |
| 2 | Stellar Furnace | 10 events | Faster pulses, +2 Supernova cap |
| 3 | Forbidden Whispers | 25 events | Faster pulses, Doom chance doubled |
| 4 | Cosmic Instability | 50 events | Fastest pulses |

## Controls

| Key | Action |
|-----|--------|
| `Z` | Toggle the reactor |
| `S` | Summon a Supernova |
| `D` | Awaken the Forbidden Doom Gem (respects cooldown) |
| `R` | Reset the reactor cycle (level, counters, cooldowns) |
| `I` | Peer into the reactor (status report) |

## Lua usage

The reactor exposes a safe API to every script in `data/scripts`:

```lua
enableZenReactor()                      -- wake the reactor
disableZenReactor()                     -- let it sleep
toggleZenReactor()                      -- returns the new state (bool)
spawnSupernova()                        -- returns true if one was created
spawnDoom()                             -- returns true if it awoke
resetReactor()                          -- fresh cycle
getReactorStatus()                      -- status table (see below)
getZenReactorConfig()                   -- current config as a table
setZenReactorConfig({ ... })            -- partial update; values are clamped
```

`getReactorStatus()` returns:

```lua
{
  enabled = true,
  level = 2,
  levelName = "Stellar Furnace",
  supernovaEvents = 12,    -- total events ever (drives levels)
  liveSupernovas = 5,      -- currently on the board
  liveDooms = 0,
  pulses = 40,
  spawnDelay = 105,        -- effective frames between pulses
  doomChance = 0.01,       -- effective chance per pulse
  nextLevelAt = 25,        -- events needed for next level (-1 = max)
  chaosMode = false
}
```

Example configuration:

```lua
setZenReactorConfig({
    maxSupernovas = 8,
    maxDoom = 1,
    spawnDelay = 120,
    doomChance = 0.01,
    doomCooldown = 3600,
    chaosMode = false,
    enabled = true
})
```

All values are clamped in native code — scripts cannot push the reactor into an unsafe state (no unlimited Doom Gems, no zero-delay flooding, no invalid coordinates).

## Configuration reference

| Key | Default | Meaning |
|-----|---------|---------|
| `enabled` | `true` | Reactor pulses automatically |
| `maxSupernovas` | `8` | Concurrent Supernovas allowed on the board (hard cap 64) |
| `maxDoom` | `1` | Doom Gems allowed at once (hard cap 1 — it is legendary) |
| `spawnDelay` | `120` | Frames between pulses at level 1 (2 seconds at 60 fps) |
| `doomChance` | `0.01` | Chance per pulse for the Doom Gem to awaken |
| `doomCooldown` | `3600` | Frames before the Doom Gem may be courted again |
| `chaosMode` | `false` | Maximum chaos: ~10x pulse rate, 10x doom chance, doubled cap |

## Native API (C++)

The core lives in `TwistExtensions/Reactor/`:

- `ZenReactor.h/.cpp` — reactor core: state machine, board scanning, safe spawning, levels
- `ZenReactorLua.h/.cpp` — the Lua bindings registered through `ScriptGameFunctions`

The reactor pulses via a board-update hook registered in `mods/mods.cpp`, using the extender's existing hook infrastructure — no new game memory writes were introduced beyond the proven `BejeweledTwist` accessors.