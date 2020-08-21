/*
 * The portal room.
 *
 * This is were we put all player objects that have gone abroad.
 */

#pragma warn_deprecated, strong_types, save_types, pedantic, rtt_checks

#include "commands.h"
#include "input_to.h"
#include "portal.h"

void init()
{
    if (!PORTAL_SERVER->is_traveller(this_player()))
    {
        move_object(this_player(), DEFAULT_PORTAL_EXIT);
        return;
    }

    add_action("cmd", "", AA_NOSPACE);
    input_to("cmd_loop", INPUT_IGNORE_BANG);
}

int cmd(string msg)
{
    if (!PORTAL_SERVER->is_traveller(this_player()))
    {
        remove_input_to(this_player());
        move_object(this_player(), DEFAULT_PORTAL_EXIT);
        return 1;
    }

    PORTAL_SERVER->send_command(this_player(), msg);
    return 1;
}

void cmd_loop(string str)
{
    if(present(this_player()))
        input_to("cmd_loop", INPUT_IGNORE_BANG);
    cmd(str || "");
}

void exit(object pl)
{
    remove_input_to(pl);
}
