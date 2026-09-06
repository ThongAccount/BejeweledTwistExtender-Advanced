#include <lua.hpp>
#include <cstdio>

#include "ZenReactor.h"

// ============================================================================
// Supernova Zen Reactor - Lua bindings
//
// Exposes a small, safe API surface to scripts. No raw memory, no pointers,
// no coordinates that could corrupt the board: every call maps onto the
// guarded ZenReactor core.
// ============================================================================

namespace
{
    int enableZenReactor(lua_State* lua)
    {
        ZenReactor::setEnabled(true);
        lua_pushboolean(lua, 1);
        return 1;
    }

    int disableZenReactor(lua_State* lua)
    {
        ZenReactor::setEnabled(false);
        lua_pushboolean(lua, 0);
        return 1;
    }

    int toggleZenReactor(lua_State* lua)
    {
        lua_pushboolean(lua, ZenReactor::toggle() ? 1 : 0);
        return 1;
    }

    int spawnSupernova(lua_State* lua)
    {
        lua_pushboolean(lua, ZenReactor::spawnSupernova() ? 1 : 0);
        return 1;
    }

    int spawnDoom(lua_State* lua)
    {
        lua_pushboolean(lua, ZenReactor::spawnDoom() ? 1 : 0);
        return 1;
    }

    int resetReactor(lua_State* lua)
    {
        ZenReactor::reset();
        return 0;
    }

    // getReactorStatus() -> table
    int getReactorStatus(lua_State* lua)
    {
        const ZenReactor::ReactorStatus s = ZenReactor::getStatus();

        lua_newtable(lua);

        lua_pushboolean(lua, s.enabled ? 1 : 0);
        lua_setfield(lua, -2, "enabled");

        lua_pushinteger(lua, s.level);
        lua_setfield(lua, -2, "level");

        lua_pushstring(lua, ZenReactor::getLevelName(s.level).c_str());
        lua_setfield(lua, -2, "levelName");

        lua_pushinteger(lua, s.supernovaEvents);
        lua_setfield(lua, -2, "supernovaEvents");

        lua_pushinteger(lua, s.liveSupernovas);
        lua_setfield(lua, -2, "liveSupernovas");

        lua_pushinteger(lua, s.liveDooms);
        lua_setfield(lua, -2, "liveDooms");

        lua_pushinteger(lua, s.pulses);
        lua_setfield(lua, -2, "pulses");

        lua_pushinteger(lua, s.spawnDelay);
        lua_setfield(lua, -2, "spawnDelay");

        lua_pushnumber(lua, s.doomChance);
        lua_setfield(lua, -2, "doomChance");

        lua_pushinteger(lua, s.nextLevelAt);
        lua_setfield(lua, -2, "nextLevelAt");

        lua_pushboolean(lua, s.chaosMode ? 1 : 0);
        lua_setfield(lua, -2, "chaosMode");

        return 1;
    }

    // Optional config view: getZenReactorConfig() -> table
    int getZenReactorConfig(lua_State* lua)
    {
        const ZenReactor::ReactorConfig& c = ZenReactor::getConfig();

        lua_newtable(lua);

        lua_pushboolean(lua, c.enabled ? 1 : 0);
        lua_setfield(lua, -2, "enabled");

        lua_pushinteger(lua, c.maxSupernovas);
        lua_setfield(lua, -2, "maxSupernovas");

        lua_pushinteger(lua, c.maxDoom);
        lua_setfield(lua, -2, "maxDoom");

        lua_pushinteger(lua, c.spawnDelay);
        lua_setfield(lua, -2, "spawnDelay");

        lua_pushnumber(lua, c.doomChance);
        lua_setfield(lua, -2, "doomChance");

        lua_pushinteger(lua, c.doomCooldown);
        lua_setfield(lua, -2, "doomCooldown");

        lua_pushboolean(lua, c.chaosMode ? 1 : 0);
        lua_setfield(lua, -2, "chaosMode");

        return 1;
    }

    // setZenReactorConfig(table) - missing keys keep their current value.
    // All values pass through the core's clamping, so scripts cannot push
    // the reactor into an unsafe state.
    int setZenReactorConfig(lua_State* lua)
    {
        if (!lua_istable(lua, 1))
        {
            printf_s("setZenReactorConfig: expected a table argument\n");
            return 0;
        }

        ZenReactor::ReactorConfig c = ZenReactor::getConfig();

        lua_getfield(lua, 1, "enabled");
        if (lua_isboolean(lua, -1)) c.enabled = lua_toboolean(lua, -1) != 0;
        lua_pop(lua, 1);

        lua_getfield(lua, 1, "maxSupernovas");
        if (lua_isnumber(lua, -1)) c.maxSupernovas = static_cast<int>(lua_tointeger(lua, -1));
        lua_pop(lua, 1);

        lua_getfield(lua, 1, "maxDoom");
        if (lua_isnumber(lua, -1)) c.maxDoom = static_cast<int>(lua_tointeger(lua, -1));
        lua_pop(lua, 1);

        lua_getfield(lua, 1, "spawnDelay");
        if (lua_isnumber(lua, -1)) c.spawnDelay = static_cast<int>(lua_tointeger(lua, -1));
        lua_pop(lua, 1);

        lua_getfield(lua, 1, "doomChance");
        if (lua_isnumber(lua, -1)) c.doomChance = static_cast<float>(lua_tonumber(lua, -1));
        lua_pop(lua, 1);

        lua_getfield(lua, 1, "doomCooldown");
        if (lua_isnumber(lua, -1)) c.doomCooldown = static_cast<int>(lua_tointeger(lua, -1));
        lua_pop(lua, 1);

        lua_getfield(lua, 1, "chaosMode");
        if (lua_isboolean(lua, -1)) c.chaosMode = lua_toboolean(lua, -1) != 0;
        lua_pop(lua, 1);

        ZenReactor::applyConfig(c);
        return 0;
    }
}

namespace ZenReactorLua
{
    void registerFunctions(lua_State* lua)
    {
        lua_register(lua, "enableZenReactor", enableZenReactor);
        lua_register(lua, "disableZenReactor", disableZenReactor);
        lua_register(lua, "toggleZenReactor", toggleZenReactor);
        lua_register(lua, "spawnSupernova", spawnSupernova);
        lua_register(lua, "spawnDoom", spawnDoom);
        lua_register(lua, "getReactorStatus", getReactorStatus);
        lua_register(lua, "resetReactor", resetReactor);
        lua_register(lua, "getZenReactorConfig", getZenReactorConfig);
        lua_register(lua, "setZenReactorConfig", setZenReactorConfig);
    }
}
