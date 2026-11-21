#include "lotr.h"
#include "cartoon.h"
#include "lotr_sdl.h"
#include "utils.h"

int main(int argc, char **argv)
{
    if (argc != 2 || !lotr_configure_edition("standard", argv[1], NULL))
        return 1;
    lotr_system_init();
    cartoon_show_credits(1);
    cartoon_play("opening");
    lotr_system_close();
    return 0;
}
