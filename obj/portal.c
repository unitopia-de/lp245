/*
 * An InterMUD portal.
 *
 * Clone this object and initialize it with set_portal_name().
 */

#pragma warn_deprecated, strong_types, save_types, pedantic, rtt_checks, warn_unused_variables

#include <portal.h>

private string portal;

void set_portal_name(string name)
{
    if (!PORTAL_SERVER.portal_exists(name))
        raise_error(sprintf("Unknown portal: %s\n", name));

    portal = name;
}

string query_portal_name()
{
    return portal;
}

void long()
{
    write("A portal" + (portal ? " to " + portal : "") + ".\nYou may try to enter it.\n");
}

string short()
{
    return "A portal";
}

int id(string str)
{
    return str == "portal" || str == lower_case(portal);
}

void init()
{
    if (portal)
        add_action("enter_portal", "enter");
}

int enter_portal(string str)
{
    if (!str || !id(str))
        return notify_fail("Enter what?\n");

    write("You enter the portal.\n");
    say(capitalize(this_player()->query_name()) + " enters the portal.\n");
    if (interactive(this_player()))
        PORTAL_SERVER.enter_portal_ob(this_player(), portal);
    else
        write("Nothing happens.\n");
    return 1;
}
