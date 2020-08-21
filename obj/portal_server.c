/*
 * The portal server.
 *
 * It manages all available portals.
 */

#pragma warn_deprecated, strong_types, save_types, pedantic, rtt_checks

#include "files.h"
#include "interactive_info.h"
#include "portal.h"

mapping config = ([]);          /* Configuration from PORTAL_FILE     */
int config_time;                /* File time of the PORTAL_FILE       */

mapping connections = ([:1]);   /* MUD name: connection object        */
mapping guests =      ([:1]);   /* "name@mud": player object          */
mapping guest_names = ([:2]);   /* player object: name; mud           */
mapping old_guest_names = ([:2]);
mapping travellers =  ([:2]);   /* player ob: portal info; start room */
mapping con_checks =  ([:0]);   /* connection objects to check,
                                   whether they are needed anymore.   */
mapping quitting =    ([:0]);   /* player objects of leaving guests.  */

private void start_timeout(object con);

/*
 * Log the message to /log/PORTAL.
 */
private void log_message(string msg, varargs mixed* args)
{
   log_file("PORTAL", sprintf("[%s] "+msg, strftime("%d.%m.%Y %H:%M:%S"), args...));
}

/*
 * Check the caller, it should be one of the portal programs.
 * If not, throw an error.
 */
void secure()
{
    if (member(({PORTAL_OBJECT, PORTAL_SERVER, PORTAL_CONNECTION, PORTAL_ROOM, PORTAL_GUEST}), load_name(previous_object())) < 0)
        raise_error("Illegal call.\n");
}

/*
 * (Re-)read the configuration from PORTAL_FILE.
 */
private void check_config()
{
    mapping section;
    int* ftime = get_dir(PORTAL_FILE, GETDIR_DATES);
    if (!sizeof(ftime) || config_time == ftime[0])
        return;

    config_time == ftime[0];
    config = ([]);
    foreach(string line: explode(read_file(PORTAL_FILE) || "", "\n"))
    {
        line = trim(line);
        if (!sizeof(line) || line[0] == '#')
            continue;
        if (line[0] == '[' && line[<1] == ']')
        {
            section = ([ P_CONF_SRC_PORTAL_NAME: line[1..<2] ]);
            config[line[1..<2]] = section;
        }
        else if (section)
        {
            int pos = strstr(line, ":");
            if (pos < 0)
                continue;

            string label = trim(line[..pos-1]);
            string value = trim(line[pos+1..]);
            switch (label)
            {
                case P_CONF_DEST_PORT:
                    m_add(section, label, to_int(value));
                    break;

                default:
                    m_add(section, label, value);
                    break;
            }
        }
    }
}

/*
 * Return the IP address of the connection object.
 */
private string get_connection_ip(object con)
{
    string addr = efun::interactive_info(con, II_IP_NUMBER);
    if(!strstr(addr, "::ffff:"))
        addr = addr[7..];
    return addr;
}

/*
 * Return the IP address in the configuration of a portal.
 */
private string get_address(mapping info)
{
    return info[P_CONF_DEST_IP];

    /* At this point alternative ways of determining
     * the IP address for the MUD (info[P_CONF_DEST_MUD])
     * can be implemented, eg. asking the InterMUD daemon.
     */
}

/*
 * Get (or create) the connection object for the given portal.
 */
private object get_connection(mapping info)
{
    string mud = info[P_CONF_DEST_MUD], lcmud = lower_case(mud);
    object con = connections[lcmud];
    if(con)
        return con;
    else
    {
        string ip = get_address(info);
        int port = info[P_CONF_DEST_PORT];

        if(!ip)
            return 0;

        con = clone_object(PORTAL_CONNECTION);
        con->set_mud(mud);
        con->connect(ip, port);
        con->send_tcp(([
            P_MSG_TYPE: P_TYPE_HELLO,
            P_MSG_MUD: MUD_NAME,
        ]));

        m_add(connections, lcmud, con);
        return con;
    }
}

/*
 * Determine the connection object for the given visitor.
 * If there is none, remove the player (unless dontquit != 0).
 */
private varargs object get_guest_connection(object player, int dontquit)
{
    string mud;
    object con;

    if (load_name(player) != PORTAL_GUEST)
        return 0;

    mud = guest_names[player, 1];
    if (!mud)
        return 0;

    con = connections[lower_case(mud)];
    if(!con && !dontquit)
    {
        if (!member(quitting, player))
        {
            m_add(quitting, player);
            player->quit();
            m_delete(quitting, player);
        }
        return 0;
    }

    return con;
}

/*
 * Return all player objects of InterMUD visitors.
 */
object* query_guests()
{
    return m_indices(guests);
}

/*
 * Return the player object for an InterMUD visitor with the given name.
 * The name must be given as "name@mud".
 */
object query_guest(string name)
{
    return guests[lower_case(name)];
}

/*
 * Check whether a portal with the given name exists
 * (i.e. there is a configuration entry for it).
 */
int portal_exists(string name)
{
    check_config();

    return member(config, name);
}

/*
 * A player leaves a portal.
 *
 * This can be a returning traveller or a visitor coming to us.
 */
private varargs void leave_portal(object pl, mapping info)
{
    string dest;
    string msg = " leaves the portal.\n";
    object room;
    int i;

    if (info)
        dest = info[P_CONF_ROOM];
    else if (!member(travellers, pl))
    {
        dest = DEFAULT_PORTAL_EXIT;
        msg = " appears.\n";
    }
    else
        dest = travellers[pl, 1];

    room = load_object(dest);

    efun::set_this_player(pl);
    move_object(pl, room);
    m_delete(travellers, pl);

    tell_room(room, capitalize(pl->query_name()) + msg, ({ pl }));

    /* Print the current room's description. */
    room->long();
    for (object ob=first_inventory(room); ob; ob = next_inventory(ob))
    {
        string str;
        if (ob == pl)
            continue;

        str = ob->short();
        if (str)
        {
            write(str + ".\n");
            if (i++ > 40)
            {
                write("*** TRUNCATED\n");
                break;
            }
        }
    }
}

/*
 * Send a given player to another world.
 */
private void enter_portal(object player, mapping info)
{
    if (load_name(player) == PORTAL_GUEST && member(guest_names, player))
    {
        string name = guest_names[player, 0];
        object con = get_guest_connection(player);
        if(!con)
            return;

        player->save_me(1);

        con->send_tcp(([
            P_MSG_TYPE:    P_TYPE_MOVE,
            P_MSG_PLAYER:  name,
            P_MSG_MUD:     info[P_CONF_DEST_MUD],
            P_MSG_IP:      get_address(info),
            P_MSG_PORT:    info[P_CONF_DEST_PORT],
            P_MSG_PORTAL:  info[P_CONF_DEST_PORTAL_NAME]
        ]));

        player->quit();
        start_timeout(con);
    }
    else
    {
        object con = get_connection(info);
        if(!con)
        {
            leave_portal(player);
            return;
        }

        con->send_tcp(([
            P_MSG_TYPE:   P_TYPE_ENTER,
            P_MSG_PLAYER: player->query_real_name(),
            P_MSG_PORTAL: info[P_CONF_DEST_PORTAL_NAME],
            P_MSG_DATA:   player->get_intermud_data(info[P_CONF_DEST_MUD]),
            P_MSG_CHARACTER: ([
                P_CHAR_NAME:   capitalize(player->query_real_name()),
                P_CHAR_GENDER: player->query_gender_string(),
            ]),
        ]));
    }
}

/*
 * A player enters a portal object.
 */
void enter_portal_ob(object pl, string portal)
{
    object room;

    secure();
    check_config();

    if(!member(config, portal))
        return;

    room = load_object(PORTAL_ROOM);
    m_add(travellers, pl, config[portal], object_name(environment(pl)));
    move_object(pl, room);

    enter_portal(pl, config[portal]);
}

/*
 * Return 1, if the player object is travelling to another MUD.
 */
int is_traveller(object pl)
{
    return member(travellers, pl);
}

/*
 * Send a command to another MUD.
 */
void send_command(object player, string cmd)
{
    object con;
    mapping info;

    secure();

    info = travellers[player, 0];
    con = info && get_connection(info);
    if(!con)
        leave_portal(player);

    con->send_tcp(([
        P_MSG_TYPE:   P_TYPE_COMMAND,
        P_MSG_PLAYER: player->query_real_name(),
        P_MSG_DATA:   cmd,
    ]));
}

/*
 * Send a message for a guest from another MUD.
 */
void send_message(string msg)
{
    object player = previous_object(), con = get_guest_connection(player);
    if(!con)
        return;

    con->send_tcp(([
        P_MSG_TYPE:   P_TYPE_MESSAGE,
        P_MSG_PLAYER: guest_names[player, 0],
        P_MSG_DATA:   msg,
    ]));
}

/*
 * Send the save data of a visitor to the original MUD.
 */
void send_savedata(string data)
{
    object player = previous_object(), con = get_guest_connection(player, 1);
    if(!con)
        return;

    con->send_tcp(([
        P_MSG_TYPE:   P_TYPE_SAVE_DATA,
        P_MSG_PLAYER: guest_names[player, 0],
        P_MSG_DATA:   data,
    ]));
}

/*
 * Send the visitor to the original MUD.
 */
void send_quit()
{
    object player = previous_object(), con = get_guest_connection(player, 1);
    if(!con)
        return;

    con->send_tcp(([
        P_MSG_TYPE:   P_TYPE_QUIT,
        P_MSG_PLAYER: guest_names[player, 0],
    ]));

    start_timeout(con);
}

/*
 * Determines the player object the given message is for.
 */
private object get_player(object con, mapping val)
{
    object player = find_player(val[P_MSG_PLAYER]);
    if(!player)
        log_message("Got unknown player for %Q: %Q\n", val[P_MSG_TYPE], val[P_MSG_PLAYER]);

    if(player)
    {
        mapping current = travellers[player, 0];
        if(!current || lower_case(current[P_CONF_DEST_MUD]) != lower_case(con->query_mud()))
        {
            log_message("MUD %Q wrongly sent %Q for player %Q.\n", con->query_mud(), val[P_MSG_TYPE], val[P_MSG_PLAYER]);
            player = 0;
        }
    }

    if(!player && val[P_MSG_TYPE] != P_TYPE_QUIT)
    {
        con->send_tcp(([
            P_MSG_TYPE:    P_TYPE_LEAVE,
            P_MSG_PLAYER:  val[P_MSG_PLAYER],
        ]));
    }

    return player;
}

/*
 * Process a message from another MUD.
 */
void receive_tcp(object con, mapping val)
{
    secure();

    if(!con->query_mud() && val[P_MSG_TYPE] != P_TYPE_HELLO)
    {
        log_message("Got command without Hello: %Q", val[P_MSG_TYPE]);
        return;
    }

    switch(val[P_MSG_TYPE])
    {
        case P_TYPE_HELLO:
        {
            string mud = val[P_MSG_MUD], lcmud = lower_case(mud);

            if(con->query_mud())
            {
                if(con->query_mud() != mud)
                    log_message("Peer answered with wrong name: '%s' vs. '%s'\n", mud, con->query_mud());
            }
            else
            {
                string addr = get_connection_ip(con);

                con->set_mud(mud);
                check_config();

                foreach(string pname, mapping info: config)
                    if(lower_case(info[P_CONF_DEST_MUD]) == lcmud &&
                       info[P_CONF_DEST_IP] && info[P_CONF_DEST_IP] != addr)
                    {
                        log_message("Wrong source address for '%s': %s.\n", mud, addr);
                        destruct(con);
                        return;
                    }

                if(connections[lcmud])
                    log_message("Replacing connection for '%s'.\n", mud);
                m_add(connections, lcmud, con);

                con->send_tcp(([
                    P_MSG_TYPE: P_TYPE_HELLO,
                    P_MSG_MUD: MUD_NAME,
                ]));
            }
            break;
        }

        case P_TYPE_ENTER:
        {
            if(!member(config, val[P_MSG_PORTAL]))
            {
                log_message("Unknown portal from %Q: %Q\n", con->query_mud(), val[P_MSG_PORTAL]);

                con->send_tcp(([
                    P_MSG_TYPE: P_TYPE_QUIT,
                    P_MSG_PLAYER: val[P_MSG_PLAYER],
                ]));

                start_timeout(con);
            }
            else
            {
                mapping info = config[val[P_MSG_PORTAL]];
                string name = lower_case(val[P_MSG_PLAYER] + "@" + con->query_mud());
                object player = guests[name];

                if(!player)
                {
                    player = clone_object(PORTAL_GUEST);
                    efun::make_interactive(player, PORTAL_SERVER, con->query_mud(), "127.0.0.1", 0, 0);
                    player->setup_intermud_player(con->query_mud(), val[P_MSG_PLAYER], val[P_MSG_CHARACTER], val[P_MSG_DATA]);
                    m_add(guests, name, player);
                    m_add(guest_names, player, val[P_MSG_PLAYER], con->query_mud());
                }

                leave_portal(player, info);
            }
            break;
        }

        case P_TYPE_MOVE:
        {
            object player = get_player(con, val);

            if(!player)
                return;

            if(lower_case(val[P_MSG_MUD]) == lower_case(MUD_NAME))
            {
                // Back home :-)
                mapping info = config[val[P_MSG_PORTAL]];
                if(!info)
                    log_message("Unknown portal from %Q: %Q\n", con->query_mud(), val[P_MSG_PORTAL]);
                leave_portal(player, info);
            }
            else
            {
                mapping info = ([
                    P_CONF_DEST_MUD:         val[P_MSG_MUD],
                    P_CONF_DEST_IP:          val[P_MSG_IP],
                    P_CONF_DEST_PORT:        val[P_MSG_PORT],
                    P_CONF_DEST_PORTAL_NAME: val[P_MSG_PORTAL],
                ]);

                travellers[player, 0] = info;
                enter_portal(player, info);
            }
            start_timeout(con);
            break;
        }

        case P_TYPE_LEAVE:
        {
            string name = lower_case(val[P_MSG_PLAYER] + "@" + con->query_mud());
            object player = guests[name];
            if(player)
            {
                player->abort_intermud();
                m_delete(guest_names, player);
            }
            m_delete(guests, name);
            start_timeout(con);
            break;
        }

        case P_TYPE_QUIT:
        {
            object player = get_player(con, val);
            if(!player)
                return;

            leave_portal(player);
            start_timeout(con);
            break;
        }

        case P_TYPE_COMMAND:
        {
            string name = lower_case(val[P_MSG_PLAYER] + "@" + con->query_mud());
            object player = guests[name];
            if(!player)
            {
                con->send_tcp(([
                    P_MSG_TYPE:    P_TYPE_QUIT,
                    P_MSG_PLAYER:  val[P_MSG_PLAYER],
                ]));
            }
            else
                efun::send_interactive_input(player, val[P_MSG_DATA]);
            break;
        }

        case P_TYPE_MESSAGE:
        case P_TYPE_PROMPT:
        {
            object player = get_player(con, val);
            if(!player)
                return;

            tell_object(player, val[P_MSG_DATA]);
            break;
        }

        case P_TYPE_SAVE_DATA:
        {
            object player = get_player(con, val);
            if(!player)
                return;

            player->save_intermud_data(con->query_mud(), val[P_MSG_DATA]);
            break;
        }

        default:
            log_message("Unknown message: %#Q\n", val);
            break;
    }
}

/*
 * We successfully opened a connection to another MUD.
 */
void opened(object con, string name)
{
    secure();

    // Treat it as a Hello message.
    receive_tcp(con, (([ P_MSG_TYPE: P_TYPE_HELLO, P_MSG_MUD: name ])));
}

/*
 * The connection is closing (or already dead).
 */
void closing(object con)
{
    string mud, lcmud;

    secure();

    // Is it an established line?
    mud = con->query_mud();
    if(!mud)
        return;

    // Do we already have an alternative?
    lcmud = lower_case(mud);
    if(connections[lcmud] && connections[lcmud] != con)
        return;

    // If not, we need to get our players back.
    foreach(object pl, mapping info, string room: travellers)
        if(lower_case(info[P_CONF_DEST_MUD]) == lcmud)
            leave_portal(pl);

    // And throw out any visitors.
    lcmud = "@" + lcmud;
    foreach(string name, object pl: guests)
        if(strstr(name, lcmud) >= 0 && pl)
            pl->quit();
}

/*
 * Check whether we need the connection and close it otherwise.
 */
private void check_connection(object con)
{
    if(!con)
        return;

    do
    {
        string mud = con->query_mud(), lcmud;

        // Not an established connection?
        if(!mud)
            break;

        // We do have already a newer connection?
        lcmud = lower_case(mud);
        if(connections[lcmud] && connections[lcmud] != con)
            break;

        // We have players there.
        foreach(object pl, mapping info, string room: travellers)
            if(lower_case(info[P_CONF_DEST_MUD]) == lcmud)
                return;

        // We have visitors from there.
        lcmud = "@" + lcmud;
        foreach(string name, object pl: guests)
            if(strstr(name, lcmud) >= 0 && pl)
                return;

    } while(0);

    con->remove();
}

/*
 * Check all connections in the timeout list.
 */
private void check_connections()
{
    foreach(object con: con_checks)
        check_connection(con);

    con_checks = ([:0]);
}

/*
 * Check the connection after a short time.
 */
private void start_timeout(object con)
{
    m_add(con_checks, con);
    remove_call_out(#'check_connections);
    call_out(#'check_connections, 10);
}

/*
 * Check all connections and clean the guest list.
 */
void reset()
{
    foreach(string mud, object con: connections)
        check_connection(con);

    guests = filter(guests, function object(string name, object pl) { return pl; });
}

/*
 * Called for a guest for a remove_interactive(pl) call.
 */
void interactive_removed(object pl)
{
    if (!member(quitting, pl))
    {
        m_add(quitting, pl);
        pl->quit();
        m_delete(quitting, pl);
    }
}

/*
 * Called for a guest, when exec() is called upon it.
 */
void interactive_exec(object pl, object new)
{
    string name = guest_names[pl, 0], mud = guest_names[pl, 1];
    if (member(guest_names, new))
    {
        if (member(old_guest_names, pl))
        {
            name = old_guest_names[pl, 0];
            mud = old_guest_names[pl, 1];
            m_delete(old_guest_names, pl);
        }
        else
        {
            /* We need to save the information. */
            m_add(old_guest_names, new, guest_names[new,0], guest_names[new, 1]);
        }
    }

    m_add(guest_names, new, name, mud);
    guests[lower_case(name + "@" + mud)] = new;
}

/*
 * Called for a guest, when there is a message for him.
 */
void receive_message(object pl, string msg)
{
    object con = get_guest_connection(pl);
    if(!con)
        return;

    con->send_tcp(([
        P_MSG_TYPE:   P_TYPE_MESSAGE,
        P_MSG_PLAYER: guest_names[pl, 0],
        P_MSG_DATA:   msg,
    ]));
}

/*
 * Called for a guest, when there is a binary message for him.
 */
void receive_binary(object pl, string msg)
{
    // Currently we don't transmit them.
}

/*
 * Called for a guest, when there is a prompt to show.
 */
void receive_prompt(object pl, string msg)
{
    object con = get_guest_connection(pl);
    if(!con)
        return;

    con->send_tcp(([
        P_MSG_TYPE:   P_TYPE_PROMPT,
        P_MSG_PLAYER: guest_names[pl, 0],
        P_MSG_DATA:   msg,
    ]));
}
