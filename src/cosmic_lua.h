#pragma once
#if defined(COSMIC_CARDPUTER) || defined(COSMIC_LUA_STANDALONE)
#include <stddef.h>
#include <string>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
class CosmicLua {
public:
    static constexpr size_t memoryLimit = 40 * 1024;
    void *host = nullptr;
    std::string error;
    CosmicLua() = default;
    ~CosmicLua();
    CosmicLua(const CosmicLua &) = delete;
    CosmicLua &operator=(const CosmicLua &) = delete;
    bool open(const char *source, size_t length, lua_CFunction registerApi, void *context);
    bool call(const char *name, const char *key = nullptr, double dt = -1);
    size_t memory() const { return used; }
    lua_State *state() const { return L; }
    static CosmicLua *from(lua_State *L) { return *static_cast<CosmicLua **>(lua_getextraspace(L)); }

private:
    lua_State *L = nullptr;
    size_t used = 0;
    int remaining = 0, appRef = LUA_NOREF;
    lua_CFunction api = nullptr;
    const char *callbackName = nullptr, *callbackKey = nullptr;
    double callbackDt = -1;
    static void *allocate(void *, void *, size_t, size_t);
    static void quota(lua_State *, lua_Debug *);
    static int bootstrap(lua_State *);
    static int dispatch(lua_State *);
    static int retain(lua_State *);
    bool protectedCall(int nargs, int nresults);
};
#endif
