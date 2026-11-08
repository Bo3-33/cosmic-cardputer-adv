#include "cosmic_lua.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>
static int noop(lua_State *) { return 0; }
static int installMockApi(lua_State *L) {
    lua_newtable(L);
    for (auto name : {"text", "line", "rect"}) { lua_pushcfunction(L, noop); lua_setfield(L, -2, name); }
    lua_setglobal(L, "ui");
    lua_newtable(L);
    for (auto name : {"background", "surface", "panel", "accent", "text"}) { lua_pushinteger(L, 1); lua_setfield(L, -2, name); }
    lua_setglobal(L, "theme");
    lua_newtable(L);
    lua_pushcfunction(L, noop); lua_setfield(L, -2, "load");
    lua_pushcfunction(L, noop); lua_setfield(L, -2, "save");
    lua_setglobal(L, "storage");
    return 0;
}
static bool open(CosmicLua &app, const char *s) { return app.open(s, strlen(s), nullptr, nullptr); }
int main(int argc, char **argv) {
    {
        CosmicLua app;
        assert(open(app, "local n=0; return {key=function(k) n=n+1 end, update=function(dt) n=n+dt end, draw=function() assert(n==2.5) end}"));
        assert(app.call("key", "enter"));
        assert(app.call("suspend")); assert(app.call("resume"));
        assert(app.call("key", "enter")); assert(app.call("update", nullptr, .5));
        assert(app.call("draw")); assert(app.memory() < CosmicLua::memoryLimit);
    }
    { CosmicLua app; assert(!open(app, "while true do end")); assert(app.error.find("instruction") != std::string::npos); }
    { CosmicLua app; assert(open(app, "return {update=function() while true do end end}")); assert(!app.call("update", nullptr, .1)); }
    { CosmicLua app; assert(!open(app, "local t={} while true do t[#t+1]=string.format('%099d%099d%099d%099d',#t,#t,#t,#t) end")); assert(app.error.find("memory") != std::string::npos); assert(app.memory() <= CosmicLua::memoryLimit); }
    { CosmicLua app; assert(!open(app, "return 1")); }
    { CosmicLua app; assert(!open(app, "this is not Lua")); }
    { CosmicLua app; assert(open(app, "assert(io==nil and os==nil and debug==nil and package==nil and dofile==nil and loadfile==nil and pcall==nil and setmetatable==nil); return {}")); }
    { CosmicLua app; assert(open(app, "return {draw=1}")); assert(!app.call("draw")); }
    // Instances have separate globals and closing one must not affect another.
    {
        CosmicLua a, b;
        assert(open(a, "value=1; return {draw=function() assert(value==1) end}"));
        assert(open(b, "value=2; return {draw=function() assert(value==2) end}"));
        assert(a.call("draw")); assert(b.call("draw"));
    }
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i]); std::stringstream source; source << file.rdbuf();
        std::string bytes = source.str();
        CosmicLua app;
        if (!app.open(bytes.data(), bytes.size(), installMockApi, nullptr)) { fprintf(stderr, "%s: %s\n", argv[i], app.error.c_str()); return 1; }
        assert(app.call("draw"));
        assert(app.call("key", "enter"));
        assert(app.call("update", nullptr, .1));
        assert(app.call("suspend")); assert(app.call("resume")); assert(app.call("draw"));
    }
    puts("Lua state retention, isolation, memory/instruction limits, errors and sample lifecycle passed");
}
