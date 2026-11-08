#if defined(COSMIC_CARDPUTER) || defined(COSMIC_LUA_STANDALONE)
#include "cosmic_lua.h"
#include <stdlib.h>
CosmicLua::~CosmicLua() {
    if (L) lua_close(L);
}
void *CosmicLua::allocate(void *ud, void *ptr, size_t old, size_t size) {
    auto *self = static_cast<CosmicLua *>(ud);
    if (!ptr) old = 0;
    if (size == 0) {
        free(ptr);
        self->used -= old;
        return nullptr;
    }
    if (size > memoryLimit || self->used - old > memoryLimit - size) return nullptr;
    void *next = realloc(ptr, size);
    if (next) self->used = self->used - old + size;
    return next;
}
void CosmicLua::quota(lua_State *L, lua_Debug *) {
    CosmicLua *self = from(L);
    if (--self->remaining <= 0) luaL_error(L, "app exceeded its instruction budget");
}
int CosmicLua::bootstrap(lua_State *L) {
    const luaL_Reg libs[] = {
        {LUA_GNAME,       luaopen_base  },
        {LUA_TABLIBNAME,  luaopen_table },
        {LUA_STRLIBNAME,  luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math  },
        {LUA_UTF8LIBNAME, luaopen_utf8  },
        {nullptr,         nullptr       }
    };
    for (const luaL_Reg *lib = libs; lib->func; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    const char *blocked[] = {
        "dofile",
        "loadfile",
        "load",
        "print",
        "collectgarbage",
        "pcall",
        "xpcall",
        "setmetatable",
        "getmetatable"
    };
    for (auto name : blocked) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    // Keep costly string/pattern operations out of the tiny-device API.
    lua_getglobal(L, "string");
    const char *patterns[] = {"find", "match", "gmatch", "gsub", "rep", "dump"};
    for (auto name : patterns) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    if (from(L)->api) return from(L)->api(L);
    return 0;
}
bool CosmicLua::protectedCall(int nargs, int nresults) {
    remaining = 12; // Hook every 1000 VM instructions.
    lua_sethook(L, quota, LUA_MASKCOUNT, 1000);
    int result = lua_pcall(L, nargs, nresults, 0);
    lua_sethook(L, nullptr, 0, 0);
    if (result == LUA_OK) return true;
    const char *message = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
    error = message ? message : "Lua callback failed";
    lua_pop(L, 1);
    return false;
}
bool CosmicLua::open(const char *source, size_t length, lua_CFunction registerApi, void *context) {
    if (L) {
        error = "app already loaded";
        return false;
    }
    host = context;
    api = registerApi;
    L = lua_newstate(allocate, this);
    if (!L) {
        error = "not enough memory for Lua";
        return false;
    }
    *static_cast<CosmicLua **>(lua_getextraspace(L)) = this;
    lua_pushcfunction(L, bootstrap);
    if (!protectedCall(0, 0)) return false;
    if (luaL_loadbufferx(L, source, length, "main.lua", "t") != LUA_OK) {
        const char *message = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
        error = message ? message : "invalid Lua source";
        lua_pop(L, 1);
        return false;
    }
    if (!protectedCall(0, 1)) return false;
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        error = "main.lua must return an app table";
        return false;
    }
    lua_pushcfunction(L, retain);
    lua_insert(L, -2);
    if (!protectedCall(1, 0)) return false;
    return call("init");
}
int CosmicLua::retain(lua_State *L) {
    from(L)->appRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}
bool CosmicLua::call(const char *name, const char *key, double dt) {
    if (!L || appRef == LUA_NOREF || !error.empty()) return false;
    callbackName = name;
    callbackKey = key;
    callbackDt = dt;
    lua_pushcfunction(L, dispatch);
    return protectedCall(0, 0);
}
int CosmicLua::dispatch(lua_State *L) {
    CosmicLua *self = from(L);
    // Retrieve fields raw: an app metatable must not run outside the instruction hook.
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->appRef);
    lua_pushstring(L, self->callbackName);
    lua_rawget(L, -2);
    lua_remove(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_isfunction(L, -1)) return luaL_error(L, "%s must be a function", self->callbackName);
    int args = 0;
    if (self->callbackKey) {
        lua_pushstring(L, self->callbackKey);
        ++args;
    }
    if (self->callbackDt >= 0) {
        lua_pushnumber(L, self->callbackDt);
        ++args;
    }
    lua_call(L, args, 0);
    return 0;
}
#endif
