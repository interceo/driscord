#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void* disrcord_app;

disrcord_app disrcord_create();

void disrcord_destroy(disrcord_app app);

void disrcord_connect(disrcord_app app, const char* url);

void disrcord_disconnect(disrcord_app app);

void disrcord_toggle_mute(disrcord_app app);

int disrcord_muted(disrcord_app app);

#ifdef __cplusplus
}
#endif