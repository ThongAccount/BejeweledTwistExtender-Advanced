#pragma once

#include <string>
#include <vector>

#include "../BejeweledTwist.h"

// ============================================================================
// Supernova Zen Reactor
//
// A hidden cosmic gameplay mode for Bejeweled Twist.
//
// The reactor is a self-contained core that:
//   * pulses on the board update loop (60 fps) and spawns Supernovas slowly
//   * treats the Doom Gem as a legendary, once-per-board event
//   * understands the board (scans for safe, non-destructive spawn spots)
//   * progresses through reactor levels as Supernova events accumulate
//   * can restrict the WHOLE board to 3 colors (Zen only)
//   * is fully guarded: limits, cooldowns and validation everywhere
//
// No raw memory games beyond the proven BejeweledTwist accessors already
// used by the rest of the extender. The reactor never overwrites a gem.
// ============================================================================

namespace ZenReactor
{
    // Skin name table, kept in sync with BejeweledTwist::GetPieceSkinName.
    // This avoids depending on the (never-assigned) global gBejeweledTwist
    // at init time.
    constexpr const char* const SKIN_NAMES[8] =
    {
        "Red", "White", "Green", "Yellow",
        "Purple", "Orange", "Blue", "Unmatchable"
    };

    // Reactor configuration. Applied via setReactorConfig / Lua bindings.
    struct ReactorConfig
    {
        bool  enabled;        // reactor starts pulsing automatically
        int   maxSupernovas;  // concurrent Supernovas allowed on the board
        int   maxDoom;        // concurrent Doom Gems allowed (keep at 1)
        int   spawnDelay;     // frames between reactor pulses at level 1
        float doomChance;     // chance per pulse for the Doom Gem to awaken
        int   doomCooldown;   // frames before the Doom Gem may awaken again
        bool  chaosMode;      // maximum chaos (testing / "maximum chaos" mode)

        // Whole-board 3-color mode (Zen only).
        // Empty set = auto-pick 3 distinct normal skins at init.
        // Each entry is a Skin enum value (0..6). Specials keep their special
        // type but their skin is forced into this set, so Supernovas and the
        // Doom Gem also belong to the 3-color board.
        std::vector<int> zenColors;
    };

    // Snapshot of the reactor state, safe to expose to Lua.
    struct ReactorStatus
    {
        bool  enabled;
        int   level;
        int   supernovaEvents;  // total Supernovas ever created (drives levels)
        int   liveSupernovas;   // currently on the board
        int   liveDooms;        // currently on the board
        int   pulses;           // reactor pulses since reset
        int   spawnDelay;       // effective frames between pulses
        float doomChance;       // effective doom chance per pulse
        int   nextLevelAt;      // events needed for the next level (-1 = max)
        bool  chaosMode;
    };

    // Level thresholds, in accumulated Supernova events.
    const int LEVEL2_AT = 10;
    const int LEVEL3_AT = 25;
    const int LEVEL4_AT = 50;

    // Lifecycle ----------------------------------------------------------------
    void init();      // call once at extender startup
    void shutdown();  // call on teardown (resets state)

    // Configuration ------------------------------------------------------------
    const ReactorConfig& getConfig();
    void applyConfig(const ReactorConfig& cfg);  // values are clamped to safe ranges
    void setChaosMode(bool chaos);               // maximum chaos mode

    // Enable / disable -----------------------------------------------------------
    bool isEnabled();
    void setEnabled(bool enabled);
    bool toggle();  // returns the new state

    // Main loop ----------------------------------------------------------------
    // Call once per board update. Safe to call at any time; every path is
    // guarded so the board update loop can never be crashed by the reactor.
    void update();

    // Manual spawning (keyboard / Lua). Returns true if something was spawned.
    bool spawnSupernova();
    bool spawnDoom();

    // Reset progression, timers and cooldowns (keeps enabled state).
    void reset();

    // Introspection --------------------------------------------------------------
    ReactorStatus getStatus();
    std::string   getLevelName(int level);

    // Zen board palette: whole board, 3 colors, Zen-only.
    const std::vector<Sexy::Piece::Skin>& getZenColors();   // current 3-color set
    void setZenColors(const std::vector<int>& colors);      // 3 distinct valid normal skins
    void randomizeZenColors();                              // pick 3 distinct normal skins
    std::string getZenColorName(Sexy::Piece::Skin skin);    // for logging / introspection
    bool isZenColor(Sexy::Piece::Skin skin);                // membership test
}
