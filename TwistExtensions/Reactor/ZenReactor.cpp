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

    // Forward declarations: the seed queue (defined below, next to the
    // settle detector) plants via helpers that appear later in this file.
    bool spawnAt(int x, int y, Sexy::Piece::SpecialType special);
    void logEvent(const std::string& message);
    void addSupernovaEvent();

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
    int   sQuietFrames     = 0;   // consecutive frames the board has been settled

    // Spawner state: seeds wait for gems that arrive via the game's own
    // fall pipeline, then ride them (like the game's native skull/bomb
    // spawners). The reactor never converts long-settled gems on a timer.
    int   sSupernovaSeeds  = 0;   // supernova seeds waiting for a fresh gem
    int   sDoomSeeds       = 0;   // doom seeds waiting for a fresh gem
    bool  sWasSettled      = false;
    bool  sHaveSnapshot    = false;

    struct FreshCell { int x; int y; };
    std::vector<FreshCell> sFreshCells;               // gems that just arrived
    std::vector<const Sexy::Piece*> sSlotSnapshot;    // piece pointer per slot

    // Per-frame slot snapshot for churn detection (the true idle signal).
    std::vector<const Sexy::Piece*> sFrameSlots;
    bool sHaveFrameSlots = false;

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

    // ------------------------------------------------------------------------
    // Settle detection
    // ------------------------------------------------------------------------

    // Consecutive fully-still frames required before the reactor may touch
    // the board. 90 frames = 1.5s at 60 fps. Covers the fall animation after
    // refills AND the deceptive pauses in the middle of chain reactions.
    const int SETTLE_FRAMES_REQUIRED = 90;

    // Nuke every Flame the moment it appears.  Always on, regardless of
    // reactor enable state.  The Flame detonation pipeline fights the
    // settle gate and orphans pending explosions (stuck Flame bug).
    // Lightning detonates on match and avoids the conflict entirely.
    void convertFlameToLightning()
    {
        if (!sGame.hasGameManager())
            return;

        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);
        if (w <= 0 || h <= 0)
            return;

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
            {
                if (sGame.GetSpecial(x, y) == Sexy::Piece::FLAME)
                {
                    sGame.SetPieceSpecial(x, y, Sexy::Piece::LIGHTNING);
                    logEvent("[ZEN] Flame nuked at " +
                             std::to_string(x) + "," + std::to_string(y));
                }
            }
        }
    }

    // True if any slot's piece pointer changed since the previous frame.
    // Falling gems, explosions destroying pieces and refills all recycle
    // slot pointers - this is the most reliable "the board is animating"
    // signal. It catches the quiet-LOOKING pauses inside a chain reaction
    // (all slots occupied while the next detonation is pending), which the
    // empty-slot check alone misses. Editing the board in those pauses
    // orphaned pending Flame explosions and froze the pipeline: the flame
    // never detonated and the column below it never refilled (observed).
    bool scanPointerChurn()
    {
        if (!sGame.hasGameManager())
        {
            sHaveFrameSlots = false;
            return false;
        }

        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);
        if (w <= 0 || h <= 0)
        {
            sHaveFrameSlots = false;
            return false;
        }

        std::vector<const Sexy::Piece*> now(static_cast<size_t>(w * h), nullptr);
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y)
                now[static_cast<size_t>(x + y * w)] = sGame.GetPiece(x, y);

        bool churned = true;
        if (sHaveFrameSlots && sFrameSlots.size() == now.size())
        {
            churned = false;
            for (size_t i = 0; i < now.size(); ++i)
            {
                if (now[i] != sFrameSlots[i])
                {
                    churned = true;
                    break;
                }
            }
        }

        sFrameSlots = std::move(now);
        sHaveFrameSlots = true;
        return churned;
    }

    // The reactor only ever acts on a fully settled board: every slot
    // occupied and no twist in progress. Touching pieces while the game is
    // clearing, refilling or dropping gems desyncs the game's fall state
    // machine and can leave gems stuck mid-fall forever (observed after
    // full board clears). Combined with the quiet-frame debounce above,
    // this also rides out the post-refill fall animation.
    bool boardIsSettled()
    {
        if (!sGame.hasGameManager())
            return false;

        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);
        if (w <= 0 || h <= 0)
            return false;

        // NOTE: currentPiece (the gem under the twist ring) is deliberately
        // NOT treated as motion - merely hovering the board keeps it set,
        // which starved the settle gate and queued every manual spawn.
        // All board edits skip currentPiece individually instead.

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
            {
                if (sGame.GetPiece(x, y) == nullptr)
                    return false; // clearing / refill in progress
            }
        }
        return true;
    }

    // A fresh gem is a slot whose piece pointer changed since the board was
    // last quiet - i.e. a gem the game itself just delivered through its
    // fall pipeline. Seeding only these gems means specials genuinely ride
    // the falling gems instead of overwriting settled ones.
    // The fresh list is REPLACED on every harvest: cells from an older fall
    // expire, so queued seeds can only ever land on the most recent delivery.
    void harvestFreshGems()
    {
        Sexy::GameManager* gm = sGame.getGameManager();
        const int w = clampi(gm->boardWidth, 0, 64);
        const int h = clampi(gm->boardHeight, 0, 64);
        if (w <= 0 || h <= 0)
        {
            sHaveSnapshot = false;
            sFreshCells.clear();
            return;
        }

        std::vector<const Sexy::Piece*> current(static_cast<size_t>(w * h), nullptr);
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y)
                current[static_cast<size_t>(x + y * w)] = sGame.GetPiece(x, y);

        std::vector<FreshCell> fresh;
        if (sHaveSnapshot && static_cast<int>(sSlotSnapshot.size()) == w * h)
        {
            for (int x = 0; x < w && static_cast<int>(fresh.size()) < 64; ++x)
            {
                for (int y = 0; y < h && static_cast<int>(fresh.size()) < 64; ++y)
                {
                    const size_t idx = static_cast<size_t>(x + y * w);
                    const Sexy::Piece* now = current[idx];
                    if (now == nullptr || now == sSlotSnapshot[idx])
                        continue;
                    fresh.push_back({x, y});
                }
            }
        }

        sFreshCells   = std::move(fresh);
        sSlotSnapshot = std::move(current);
        sHaveSnapshot = true;
    }

    bool isSeedable(int x, int y, int hoverX, int hoverY)
    {
        if (x == hoverX && y == hoverY)
            return false;

        Sexy::Piece* piece = sGame.GetPiece(x, y);
        if (piece == nullptr)
            return false;
        if (sGame.GetSpecial(x, y) != Sexy::Piece::NONE)
            return false;
        if (piece->skin == Sexy::Piece::Skin::UNMATCHABLE)
            return false;
        return true;
    }

    // Forward declaration (used by processSeedQueue's idle-fallback path).
    bool findSafeSpawnPosition(int& outX, int& outY);

    // Plant queued seeds. Doom has priority (it is the rarer event).
    // Fresh gems (delivered by the last fall) are seeded first so specials
    // ride the fall; with allowAnyCell, remaining seeds plant on any safe
    // cell once the board is fully idle - guarantees manual spawns always
    // land even when the game recycles piece objects (no pointer diff).
    void processSeedQueue(bool allowAnyCell)
    {
        if (sSupernovaSeeds == 0 && sDoomSeeds == 0)
            return;

        int hoverX = -1, hoverY = -1;
        sGame.GetHoverPos(hoverX, hoverY);

        // 1) ride the fresh gems from the last fall
        while (!sFreshCells.empty() && (sSupernovaSeeds > 0 || sDoomSeeds > 0))
        {
            const int x = sFreshCells.front().x;
            const int y = sFreshCells.front().y;
            sFreshCells.erase(sFreshCells.begin());

            if (!isSeedable(x, y, hoverX, hoverY))
                continue;

            if (sDoomSeeds > 0 && spawnAt(x, y, Sexy::Piece::DOOM))
            {
                --sDoomSeeds;
                sDoomCooldown = sConfig.doomCooldown;
                sDoomTimer = 0;
                logEvent("[ZEN] THE FORBIDDEN DOOM GEM ARRIVES at " +
                         std::to_string(x) + "," + std::to_string(y));
            }
            else if (sSupernovaSeeds > 0 && spawnAt(x, y, Sexy::Piece::SUPERNOVA))
            {
                --sSupernovaSeeds;
                addSupernovaEvent();
                logEvent("[ZEN] A Supernova rides the fall at " +
                         std::to_string(x) + "," + std::to_string(y));
            }
        }

        // 2) idle fallback: board fully quiet, plant wherever is safe
        if (!allowAnyCell)
            return;

        while (sSupernovaSeeds > 0 || sDoomSeeds > 0)
        {
            int x = -1, y = -1;
            if (!findSafeSpawnPosition(x, y))
                break; // no seedable cell left

            bool planted = false;
            if (sDoomSeeds > 0 && spawnAt(x, y, Sexy::Piece::DOOM))
            {
                --sDoomSeeds;
                sDoomCooldown = sConfig.doomCooldown;
                sDoomTimer = 0;
                logEvent("[ZEN] THE FORBIDDEN DOOM GEM AWAKENS at " +
                         std::to_string(x) + "," + std::to_string(y));
                planted = true;
            }
            else if (sSupernovaSeeds > 0 && spawnAt(x, y, Sexy::Piece::SUPERNOVA))
            {
                --sSupernovaSeeds;
                addSupernovaEvent();
                logEvent("[ZEN] Supernova created at " +
                         std::to_string(x) + "," + std::to_string(y));
                planted = true;
            }

            if (!planted)
                break; // cell rejected both - avoid spinning
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

    // The Doom Gem counts down on matchless moves and detonates at zero -
    // no second chance. Every doom the reactor spawns gets two moves to be
    // matched before it blows.
    const int DOOM_START_COUNTER = 2;

    bool spawnAt(int x, int y, Sexy::Piece::SpecialType special)
    {
        // Re-validate right before the write; the board may have shifted
        // between scanning and now.
        if (sGame.GetPiece(x, y) == nullptr)
            return false;
        if (sGame.GetSpecial(x, y) != Sexy::Piece::NONE)
            return false;

        sGame.SetPieceSpecial(x, y, special);

        // Doom arrives live: counter 2, ticking down on matchless moves.
        if (special == Sexy::Piece::DOOM &&
            sGame.GetSpecial(x, y) == Sexy::Piece::DOOM)
        {
            sGame.SetCounter(x, y, DOOM_START_COUNTER);
        }

        return sGame.GetSpecial(x, y) == special;
    }

    FILE* sLogFile = nullptr;

    void logEvent(const std::string& message)
    {
        printf_s("%s\n", message.c_str());
        if (!sLogFile)
            fopen_s(&sLogFile, "zen_reactor.log", "a");
        if (sLogFile)
        {
            fprintf(sLogFile, "%s\n", message.c_str());
            fflush(sLogFile);
        }
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
                if (sGame.GetSpecial(x, y) != Sexy::Piece::NONE)
                    continue; // preserve specials (Supernova, Doom, Flame, …)

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
        sQuietFrames  = 0;
        sSupernovaSeeds = 0;
        sDoomSeeds      = 0;
        sWasSettled     = false;
        sHaveSnapshot   = false;
        sFreshCells.clear();
        sHaveFrameSlots = false;
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
        if (sLogFile)
        {
            fclose(sLogFile);
            sLogFile = nullptr;
        }
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
        sQuietFrames = 0;
        sWasSettled  = false;
        sHaveSnapshot = false; // re-arm the fresh-gem diff on next enable
        sHaveFrameSlots = false;
        sFreshCells.clear();
        logEvent(enabled
            ? "[ZEN] Reactor awakened."
            : "[ZEN] Reactor dormant.");
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
        if (!sInitialized || !sGame.hasGameManager())
            return;

        // ------------------------------------------------------------------
        // Flame → Lightning: ALWAYS on.  Every Flame the game engine
        // creates is instantly nuked.  The Flame detonation pipeline
        // fights the settle gate and orphans pending explosions —
        // Lightning detonates on match and avoids it entirely.
        // ------------------------------------------------------------------
        convertFlameToLightning();

        // ------------------------------------------------------------------
        // Settle tracking: NEVER touch the board while it is in motion.
        // Runs even when the reactor is disabled so manual spawns can use
        // the same gate.
        // ------------------------------------------------------------------
        // Clearing, refilling and falling gems are the game's state machine;
        // editing skins or spawning specials mid-flight desyncs it and can
        // deadlock the board (gems stuck mid-fall after a full clear).
        if (scanPointerChurn())
        {
            // Pieces moved or changed this frame: the pipeline is animating
            // (fall, explosion, refill, or a mid-chain detonation pending).
            if (sWasSettled)
                sFreshCells.clear(); // motion started: queued fresh gems are stale
            sWasSettled  = false;
            sQuietFrames = 0;

            // --- DURING FALL: enforce 3-color palette immediately --------
            // Gems get zen skins the instant they appear, not after a
            // visible flash of random colors.  SetPieceSkin is a no-op if
            // the skin is already correct, so per-frame cost is minimal.
            if (sConfig.enabled)
                applyZenBoardPalette();

            // --- DURING FALL: seeds ride the falling gems ----------------
            // Harvest fresh cells as they arrive and plant queued seeds
            // on them right away, so Supernova/Doom appear mid-fall like
            // the game's native skull/bomb spawners.
            harvestFreshGems();
            processSeedQueue(false);

            return;
        }

        if (!boardIsSettled())
        {
            if (sWasSettled)
                sFreshCells.clear(); // motion started: queued fresh gems are stale
            sWasSettled  = false;
            sQuietFrames = 0;
            return;
        }
        sWasSettled = true;
        ++sQuietFrames;

        if (!sConfig.enabled)
            return; // asleep: keep the gate armed, touch nothing

        ++sTimer;
        ++sDoomTimer;
        if (sDoomCooldown > 0)
            --sDoomCooldown;

        if (sQuietFrames < SETTLE_FRAMES_REQUIRED)
            return;

        // ------------------------------------------------------------------
        // Fully quiet: fire the reactor pulse to queue seeds for the NEXT
        // fall.  Palette and harvesting are already done during the fall.
        // ------------------------------------------------------------------

        // The spawn scanner below already avoids the hovered/held gem,
        // so the pulse never has to stall on player input.
        const int delay = effectiveSpawnDelay();
        if (sTimer < delay)
            return;

        // ------------------------------------------------------------------
        // Reactor pulse: queue seeds; they ride the next gem fall.
        // ------------------------------------------------------------------
        sTimer = 0;
        ++sPulses;

        int liveSupernovas = 0;
        int liveDooms = 0;
        recountBoard(liveSupernovas, liveDooms);

        // Supernova seeds: only below the cap (live + queued).
        if (liveSupernovas + sSupernovaSeeds < maxSupernovas())
            ++sSupernovaSeeds;

        // Doom awakening: legendary, once per board, heavily chilled.
        // The roll queues a seed; the cooldown starts when it plants.
        if (sDoomSeeds == 0 &&
            liveDooms < sConfig.maxDoom &&
            sDoomCooldown <= 0 &&
            sDoomTimer >= sConfig.doomCooldown)
        {
            const float chance = effectiveDoomChance();
            const float roll = static_cast<float>(random(0, 9999)) / 10000.0f;
            if (roll < chance)
            {
                ++sDoomSeeds;
                logEvent("[ZEN] The Forbidden Gem stirs, waiting to ride the fall...");
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

        if (liveSupernovas + sSupernovaSeeds >= maxSupernovas())
        {
            logEvent("[ZEN] The board cannot hold another supernova.");
            return false;
        }

        // Settled board: plant immediately for instant feedback.
        if (sQuietFrames >= SETTLE_FRAMES_REQUIRED)
        {
            int x = -1, y = -1;
            if (!findSafeSpawnPosition(x, y))
                return false;
            if (!spawnAt(x, y, Sexy::Piece::SUPERNOVA))
                return false;

            addSupernovaEvent();
            logEvent("[ZEN] Supernova created at " + std::to_string(x) + "," + std::to_string(y));
            return true;
        }

        // Board in motion: queue the seed; it rides the next fall.
        ++sSupernovaSeeds;
        logEvent("[ZEN] A supernova seed joins the fall...");
        return true;
    }

    bool spawnDoom()
    {
        if (!sGame.hasGameManager())
            return false;

        int liveSupernovas = 0, liveDooms = 0;
        recountBoard(liveSupernovas, liveDooms);

        // Manual summon: the legendary rule is "maximum one Doom alive",
        // not the auto-spawn cooldown. Once the current Doom detonates or
        // is matched, another may be called.
        if (liveDooms + sDoomSeeds >= sConfig.maxDoom)
        {
            logEvent("[ZEN] The Forbidden Gem already walks the board.");
            return false;
        }

        // Settled board: plant immediately for instant feedback.
        if (sQuietFrames >= SETTLE_FRAMES_REQUIRED)
        {
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

        // Board in motion: queue the seed; it rides the next fall.
        ++sDoomSeeds;
        logEvent("[ZEN] The Forbidden Gem will ride the next fall...");
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
        sQuietFrames  = 0;
        sSupernovaSeeds = 0;
        sDoomSeeds      = 0;
        sWasSettled     = false;
        sHaveSnapshot   = false;
        sFreshCells.clear();
        sHaveFrameSlots = false;

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
        status.pendingSupernovas = sSupernovaSeeds;
        status.pendingDooms      = sDoomSeeds;

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
