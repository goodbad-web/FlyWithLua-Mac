#ifndef THREE_JFPS_NATIVE_H
#define THREE_JFPS_NATIVE_H

#include "lua.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void threejfps_register_lua_functions(lua_State* state);
void threejfps_step(float deltaSeconds);
void threejfps_draw_hud(void);
int threejfps_handle_click(int x, int y, int mouseStatus);
int threejfps_handle_wheel(int x, int y, int wheel, int clicks);
void threejfps_on_lua_reset(void);
void threejfps_shutdown(void);
int threejfps_is_active(void);
void threejfps_enqueue_command(const char* jsonCommand);

#ifdef __cplusplus
}
#endif

#endif
