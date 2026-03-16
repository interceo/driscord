#include "disrcord.h"
#include "app.hpp"

disrcord_app disrcord_create()
{
    Config cfg;
    return new App(cfg);
}

void disrcord_destroy(disrcord_app app)
{
    delete static_cast<App*>(app);
}

void disrcord_connect(disrcord_app app, const char* url)
{
    static_cast<App*>(app)->connect(url);
}

void disrcord_disconnect(disrcord_app app)
{
    static_cast<App*>(app)->disconnect();
}

void disrcord_toggle_mute(disrcord_app app)
{
    static_cast<App*>(app)->toggle_mute();
}

int disrcord_muted(disrcord_app app)
{
    return static_cast<App*>(app)->muted();
}