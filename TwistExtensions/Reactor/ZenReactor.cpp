#include "ZenReactor.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

#include "../BejeweledTwist.h"
#include <Extender/util.h>     // random(min, max) helper used by the extender

// ============================================================================
// Supernova Zen Reactor - implementation
// ============================================================================

namespace
{
    using namespace ZenReactor;

    // ------------------------------------------------------------------------
    // Reactor state
    // ------------------------------------------------------------------------

    ReactorConfig sConfig =
    {
        /*.enabled       =*/ true,
        /*.maxSupernovas =*/ 8,
        /*.maxDoom       =*/ 1,
        /*.spawnDelay    =*/ 120,
        /*.doomChance    =*/ 0.01f,
        /*.doomCooldown  =*/ 3600, // one minute of gameplay between attempts
        /*.chaosMode     =*/ false
    };

    int   sLevel           = 1;   // current reactor level
    int   sEvents          = 0;   // accumulated Supernova events (drives levels)
    int   sTimer           = 0;   // frames since last pulse
    int   sDoomTimer       = 0;   // frames since last doom awakening attempt
    int   sPulses          = 0;
    int   sDoomCooldown    = 0;   // frames until doom may be attempted again

    bool  sInitialized     = false;

    // Whole-board 3-color palette (Zen only).
    std::vector<Sexy::Piece::Skin> sZenColors;

    BejeweledTwist sGame;         // own instance; the global one stays upstream

    // Level names, indexed 1..4.
    const char* const sLevelNames[5] =
    {
        "Unknown",
        "Dormant Ember",       // level 1
        "Stellar Furnace",     // level 2
        "Forbidden Whispers",  // level 3
        "Cosmic Instability"   // level 4
    };

    // ------------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------------

    int clampi(int value, int lo, int hi)
    {
        return std::max(lo, std::min(hi, value));
    }

    float clampf(float value, float lo, float hi)
    {
        return std::max(lo, std::min(hi, value));
    }

    // Level 4 "cosmic instability" halves the pulse delay.
    int effectiveSpawnDelay()
    {
        int delay = sConfig.spawnDelay;
        if (sLevel >= 4)
            delay = std::max(1, delay / 2);
        else if (sLevel >= 3)
            delay = std::max(1, (delay * 3) / 4);
        else if (sLevel >= 2)
            delay = std::max(1, (delay * 7) / 8);

        if (sConfig.chaosMode)
            delay = std::max(1, delay / 10);

        return delay;
    }

    // Level 3+ raises the doom chance, chaos mode raises it further.
    float effectiveDoomChance()
    {
        float chance = sConfig.doomChance;
        if (sLevel >= 3)
            chance *= 2.0f;
        if (sConfig.chaosMode)
            chance *= 10.0f;

        return clampf(chance, 0.0f, 0.5f);
    }

    int maxSupernovas()
    {
        int cap = sConfig.maxSupernovas;
        if (sLevel >= 2)
            cap += 2;
        if (sConfig.chaosMode)
            cap = clampi(cap * 2, cap, 64);

        return clampi(cap, 0, 64);
    }

    int eventsForNextLevel()
    {
        if (sLevel <= 1) return LEVEL2_AT;
        if (sLevel == 2) return LEVEL3_AT;
        if (sLevel == 3) return LEVEL4_AT;
        return -1; // max level
    }

    void recountBoard(int& supernovas, int& dooms)
    {
        supernovas = 0;
        dooms = 0;

        if (!sGame.hasGameManager())
            return;

        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
            {
                Sexy::Piece::SpecialType special = sGame.GetSpecial(x, y);
                if (special == Sexy::Piece::SUPERNOVA)
                    ++supernovas;
                else if (special == Sexy::Piece::DOOM)
                    ++dooms;
            }
        }
    }

    // Ask the board: "where can I create chaos without destroying everything?"
    // A position is safe when the gem exists, is a plain gem (no special,
    // not unmatchable/coal-like), is not directly under the player's cursor
    // and is not the piece currently held by the twist ring.
    bool findSafeSpawnPosition(int& outX, int& outY)
    {
        if (!sGame.hasGameManager())
            return false;

        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);
        if (w <= 0 || h <= 0)
            return false;

        // Positions where the player is currently interacting.
        int hoverX = -1, hoverY = -1;
        sGame.GetHoverPos(hoverX, hoverY);

        // Collect every safe candidate first, then pick one at random so
        // spawns feel cosmic instead of marching across the board.
        std::vector<int> candidates;
        candidates.reserve(static_cast<size_t>(w * h));

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
            {
                if (x == hoverX && y == hoverY)
                    continue;

                Sexy::Piece* piece = sGame.GetPiece(x, y);
                if (piece == nullptr)
                    continue;

                // Never overwrite a gem that already carries a special.
                if (sGame.GetSpecial(x, y) != Sexy::Piece::NONE)
                    continue;

                // Skip unmatchable placeholder skins (coal/locked slots).
                if (piece->skin == Sexy::Piece::Skin::UNMATCHABLE)
                    continue;

                // Skip the gem currently held by the twist ring.
                if (piece == gm->currentPiece)
                    continue;

                candidates.push_back(y * w + x);
            }
        }

        if (candidates.empty())
            return false;

        const int pick = candidates[random(0, static_cast<int>(candidates.size()) - 1)];
        outX = pick % w;
        outY = pick / w;
        return true;
    }

    bool spawnAt(int x, int y, Sexy::Piece::SpecialType special)
    {
        // Re-validate right before the write; the board may have shifted
        // between scanning and now.
        if (sGame.GetPiece(x, y) == nullptr)
            return false;
        if (sGame.GetSpecial(x, y) != Sexy::Piece::NONE)
            return false;

        sGame.SetPieceSpecial(x, y, special);
        return sGame.GetSpecial(x, y) == special;
    }

    void logEvent(const std::string& message)
    {
        printf_s("%s\n", message.c_str());
    }

    void addSupernovaEvent()
    {
        ++sEvents;

        const int newLevel =
            (sEvents >= LEVEL4_AT) ? 4 :
            (sEvents >= LEVEL3_AT) ? 3 :
            (sEvents >= LEVEL2_AT) ? 2 : 1;

        if (newLevel > sLevel)
        {
            sLevel = newLevel;
            logEvent(std::string("[ZEN] Reactor ascends to level ") +
                     std::to_string(sLevel) + ": " + getLevelName(sLevel));
        }
    }

    // ------------------------------------------------------------------------
    // Whole-board 3-color palette (Zen only)
    // ------------------------------------------------------------------------

    void applyZenBoardPalette()
    {
        if (!sInitialized || !sConfig.enabled || !sGame.hasGameManager())
            return;
        if (sZenColors.size() != 3)
            return;

        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);
        if (w <= 0 || h <= 0)
            return;

        // fast membership test for the 3 zen skins
        bool isZen[8] = {};
        for (const auto& s : sZenColors)
            if (static_cast<int>(s) >= 0 && static_cast<int>(s) <= 7)
                isZen[static_cast<int>(s)] = true;

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
            {
                Sexy::Piece* piece = sGame.GetPiece(x, y);
                if (piece == nullptr)
                    continue;
                if (piece == gm->currentPiece)
                    continue; // don't disturb the piece being twisted
                if (piece->skin == Sexy::Piece::Skin::UNMATCHABLE)
                    continue; // preserve unmatchable / coal-locked pieces

                if (!isZen[static_cast<int>(piece->skin)])
                {
                    // reassign to a random zen color.
                    // Non-flickery: once a piece is set to a zen color it stays
                    // until a new gem falls in with a non-zen skin.
                    sGame.SetPieceSkin(x, y, sZenColors[random(0, 2)]);
                }
            }
        }
    }
}

namespace ZenReactor
{
    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------

    void init()
    {
        if (sInitialized)
            return;

        sLevel        = 1;
        sEvents       = 0;
        sTimer        = 0;
        sDoomTimer    = 0;
        sPulses       = 0;
        sDoomCooldown = 0;
        sInitialized  = true;

        if (sZenColors.empty())
            randomizeZenColors();
        else
            logEvent(std::string("[ZEN] Board palette: ") +
                     SKIN_NAMES[static_cast<int>(sZenColors[0])] + ", " +
                     SKIN_NAMES[static_cast<int>(sZenColors[1])] + ", " +
                     SKIN_NAMES[static_cast<int>(sZenColors[2])] + ".");

        logEvent("[ZEN] Cosmic energy detected. The reactor sleeps... for now.");
    }

    void shutdown()
    {
        sInitialized = false;
    }

    // ------------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------------

    const ReactorConfig& getConfig()
    {
        return sConfig;
    }

    void applyConfig(const ReactorConfig& cfg)
    {
        sConfig.enabled       = cfg.enabled;
        sConfig.maxSupernovas = clampi(cfg.maxSupernovas, 0, 64);
        sConfig.maxDoom       = clampi(cfg.maxDoom, 0, 1);   // legendary: one max
        sConfig.spawnDelay    = clampi(cfg.spawnDelay, 1, 3600);
        sConfig.doomChance    = clampf(cfg.doomChance, 0.0f, 0.5f);
        sConfig.doomCooldown  = clampi(cfg.doomCooldown, 0, 1000000);
        sConfig.chaosMode     = cfg.chaosMode;
    }

    void setChaosMode(bool chaos)
    {
        sConfig.chaosMode = chaos;
        logEvent(chaos
            ? "[ZEN] MAXIMUM CHAOS. The reactor tears at its seams."
            : "[ZEN] The cosmos settles back into rhythm.");
    }

    // ------------------------------------------------------------------------
    // Enable / disable
    // ------------------------------------------------------------------------

    bool isEnabled()
    {
        return sConfig.enabled;
    }

    void setEnabled(bool enabled)
    {
        sConfig.enabled = enabled;
        logEvent(enabled
            ? "[ZEN] Reactor awakened."
            : "[ZEN] Reactor dormant.");
        // apply the palette immediately (next frame's hook also runs it)
        if (enabled)
            applyZenBoardPalette();
    }

    bool toggle()
    {
        setEnabled(!sConfig.enabled);
        return sConfig.enabled;
    }

    // ------------------------------------------------------------------------
    // Main loop - called from the board update hook
    // ------------------------------------------------------------------------

    void update()
    {
        if (!sInitialized || !sConfig.enabled)
            return;

        if (!sGame.hasGameManager())
            return;

        ++sTimer;
        ++sDoomTimer;
        if (sDoomCooldown > 0)
            --sDoomCooldown;

        // Whole-board 3-color enforcement, every frame while Zen is on.
        // Runs before the pulse so newly-spawned Supernovas/Dooms are already
        // in the 3-color set the same frame they appear.
        applyZenBoardPalette();

        // The spawn scanner below already avoids the hovered/held gem,
        // so the pulse never has to stall on player input.
        const int delay = effectiveSpawnDelay();
        if (sTimer < delay)
            return;

        // ------------------------------------------------------------------
        // Reactor pulse
        // ------------------------------------------------------------------
        sTimer = 0;
        ++sPulses;

        int liveSupernovas = 0;
        int liveDooms = 0;
        recountBoard(liveSupernovas, liveDooms);

        // Supernova generation: one per pulse, only below the cap.
        if (liveSupernovas < maxSupernovas())
        {
            int x = -1, y = -1;
            if (findSafeSpawnPosition(x, y) && spawnAt(x, y, Sexy::Piece::SUPERNOVA))
            {
                addSupernovaEvent();
                logEvent("[ZEN] Supernova created at " +
                         std::to_string(x) + "," + std::to_string(y));
            }
        }

        // Doom awakening: legendary, once per board, heavily chilled.
        if (liveDooms < sConfig.maxDoom &&
            sDoomCooldown <= 0 &&
            sDoomTimer >= sConfig.doomCooldown)
        {
            const float chance = effectiveDoomChance();
            const float roll = static_cast<float>(random(0, 9999)) / 10000.0f;
            if (roll < chance)
            {
                int x = -1, y = -1;
                if (findSafeSpawnPosition(x, y) && spawnAt(x, y, Sexy::Piece::DOOM))
                {
                    logEvent("[ZEN] THE FORBIDDEN DOOM GEM AWAKENS at " +
                             std::to_string(x) + "," + std::to_string(y));
                }
                // Whether it spawned or not, do not hammer the dice.
                sDoomCooldown = sConfig.doomCooldown;
                sDoomTimer = 0;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Manual spawning (keyboard / Lua)
    // ------------------------------------------------------------------------

    bool spawnSupernova()
    {
        if (!sGame.hasGameManager())
            return false;

        int liveSupernovas = 0, liveDooms = 0;
        recountBoard(liveSupernovas, liveDooms);

        if (liveSupernovas >= maxSupernovas())
        {
            logEvent("[ZEN] The board cannot hold another supernova.");
            return false;
        }

        int x = -1, y = -1;
        if (!findSafeSpawnPosition(x, y))
            return false;

        if (!spawnAt(x, y, Sexy::Piece::SUPERNOVA))
            return false;

        addSupernovaEvent();
        logEvent("[ZEN] Supernova created at " + std::to_string(x) + "," + std::to_string(y));
        return true;
    }

    bool spawnDoom()
    {
        if (!sGame.hasGameManager())
            return false;

        int liveSupernovas = 0, liveDooms = 0;
        recountBoard(liveSupernovas, liveDooms);

        if (liveDooms >= sConfig.maxDoom || sDoomCooldown > 0)
        {
            logEvent("[ZEN] The Forbidden Gem refuses to awaken again so soon.");
            return false;
        }

        int x = -1, y = -1;
        if (!findSafeSpawnPosition(x, y))
            return false;

        if (!spawnAt(x, y, Sexy::Piece::DOOM))
            return false;

        logEvent("[ZEN] THE FORBIDDEN DOOM GEM AWAKENS at " +
                 std::to_string(x) + "," + std::to_string(y));

        sDoomCooldown = sConfig.doomCooldown;
        sDoomTimer = 0;
        return true;
    }

    // ------------------------------------------------------------------------
    // Reset
    // ------------------------------------------------------------------------

    void reset()
    {
        sLevel        = 1;
        sEvents       = 0;
        sTimer        = 0;
        sDoomTimer    = 0;
        sPulses       = 0;
        sDoomCooldown = 0;

        logEvent("[ZEN] Reactor reset. The cycle begins anew.");
    }

    // ------------------------------------------------------------------------
    // Introspection
    // ------------------------------------------------------------------------

    ReactorStatus getStatus()
    {
        ReactorStatus status;
        status.enabled       = sConfig.enabled;
        status.level         = sLevel;
        status.supernovaEvents = sEvents;
        status.pulses        = sPulses;
        status.spawnDelay    = effectiveSpawnDelay();
        status.doomChance    = effectiveDoomChance();
        status.nextLevelAt   = eventsForNextLevel();
        status.chaosMode     = sConfig.chaosMode;

        int liveSupernovas = 0, liveDooms = 0;
        recountBoard(liveSupernovas, liveDooms);
        status.liveSupernovas = liveSupernovas;
        status.liveDooms      = liveDooms;

        return status;
    }

    std::string getLevelName(int level)
    {
        if (level < 0 || level > 4)
            return sLevelNames[0];
        return sLevelNames[level];
    }

    // Zen board palette ---------------------------------------------------------
    const std::vector<Sexy::Piece::Skin>& getZenColors()
    {
        return sZenColors;
    }

    std::string getZenColorName(Sexy::Piece::Skin skin)
    {
        const int idx = static_cast<int>(skin);
        if (idx < 0 || idx > 7)
            return SKIN_NAMES[7];
        return SKIN_NAMES[idx];
    }

    bool isZenColor(Sexy::Piece::Skin skin)
    {
        for (const auto& c : sZenColors)
            if (c == skin)
                return true;
        return false;
    }

    void setZenColors(const std::vector<int>& colors)
    {
        if (colors.size() != 3)
        {
            logEvent("[ZEN] setZenColors requires exactly 3 skin values (0..6). Keeping current palette.");
            return;
        }

        std::vector<Sexy::Piece::Skin> out;
        out.reserve(3);
        for (int v : colors)
        {
            if (v < 0 || v > 6)
            {
                logEvent(std::string("[ZEN] Invalid Zen color value ") + std::to_string(v) +
                         "; keeping current palette.");
                return;
            }
            out.push_back(static_cast<Sexy::Piece::Skin>(v));
        }

        // require distinct colors
        if (out[0] == out[1] || out[0] == out[2] || out[1] == out[2])
        {
            logEvent("[ZEN] Zen colors must be distinct. Keeping current palette.");
            return;
        }

        sZenColors = std::move(out);
        logEvent(std::string("[ZEN] Board palette set to ") +
                 getZenColorName(sZenColors[0]) + ", " +
                 getZenColorName(sZenColors[1]) + ", " +
                 getZenColorName(sZenColors[2]) + ".");
    }

    void randomizeZenColors()
    {
        sZenColors.clear();
        std::vector<Sexy::Piece::Skin> pool;
        for (int i = 0; i < 7; ++i)
            pool.push_back(static_cast<Sexy::Piece::Skin>(i));

        // pick 3 distinct normal skins
        for (int i = 0; i < 3; ++i)
        {
            const int idx = random(0, static_cast<int>(pool.size()) - 1 - i);
            sZenColors.push_back(pool[idx]);
            pool.erase(pool.begin() + idx);
        }

        logEvent(std::string("[ZEN] Board palette randomized to ") +
                 getZenColorName(sZenColors[0]) + ", " +
                 getZenColorName(sZenColors[1]) + ", " +
                 getZenColorName(sZenColors[2]) + ".");
    }
}
