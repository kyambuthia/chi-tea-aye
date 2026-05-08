#include "app/App.h"

#include "sokol_app.h"

sapp_desc sokol_main(int argc, char* argv[]) {
    return app::makeAppDesc(argc, argv);
}
