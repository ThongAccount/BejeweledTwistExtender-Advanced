#pragma once

#include <lua.hpp>

// Lua bindings for the Supernova Zen Reactor.
// Registers the zen reactor functions onto a lua state.
namespace ZenReactorLua
{
    void registerFunctions(lua_State* lua);
}
