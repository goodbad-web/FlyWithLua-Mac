#ifndef _FLYWITHLUA_H_
#define _FLYWITHLUA_H_

#include <string>
#include <cstdint>
#include <lua.hpp>

// Fmod insperation from Camille Bachmann
// https://bitbucket.org/Squirrel_FS/fmodplugin/src/master/

// include Fmod
#include "fmod.h"
#include "fmod_studio.h"
#include "fmod_errors.h"
#include "FmodIntegration.h"

#include "XPLMSound.h"

//Teddii: Enum fuer "logMsg"
enum ELogType
{
    logToAll    = 0,
    logToDevCon = 1,
    logToSqkBox = 2
};

namespace flywithlua {

using LuaScriptId = std::uint64_t;
static constexpr LuaScriptId kSystemLuaScriptId = 0;

/** Identifies the script currently executing on the shared Lua VM. */
LuaScriptId CurrentLuaScriptId();
bool IsLuaScriptQuarantined(LuaScriptId scriptId);
bool IsLuaPanelApiAllowed();

/**
 * Temporarily associates native callback execution with a script.
 * The Lua VM remains shared; this only provides ownership for callbacks,
 * diagnostics, and script-owned resources.
 */
class LuaScriptScope {
public:
    explicit LuaScriptScope(LuaScriptId scriptId);
    ~LuaScriptScope();

    LuaScriptScope(const LuaScriptScope&) = delete;
    LuaScriptScope& operator=(const LuaScriptScope&) = delete;

private:
    LuaScriptId previousScriptId_;
};

class LuaPanelApiScope {
public:
    explicit LuaPanelApiScope(bool allowed);
    ~LuaPanelApiScope();

    LuaPanelApiScope(const LuaPanelApiScope&) = delete;
    LuaPanelApiScope& operator=(const LuaPanelApiScope&) = delete;

private:
    bool previousAllowed_;
};

/** Report an error without stopping unrelated scripts. */
void ReportLuaScriptError(LuaScriptId scriptId,
                          const char* context,
                          const std::string& message);

void logMsg (ELogType logType, std::string message ); //Teddii: added parameter logType //void logMsg ( std::string message );
void CopyDataRefsToLua( void );
void CopyDataRefsToXPlane( void );

/** Log the error and quarantine the currently executing script when known. */
void panic(const std::string& message);

extern bool LuaIsRunning;                       // Are we working with Lua?
extern bool WeAreNotInDrawingState;
extern lua_State   *FWLLua;

extern std::string scriptDir;
extern std::string quarantineDir;
std::string JoinPath(const std::string& base, const std::string& child);
extern bool ReadAllScriptFiles();
extern int found_bad_function_script;
extern void DebugLua();
extern void process_read_ini_file();
extern int developer_mode;
extern int verbose_logging_mode;
}

#endif
