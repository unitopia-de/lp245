/*
 * A connection to another MUD.
 *
 * This connection is used for all players to and from this MUD.
 */

#pragma warn_deprecated, strict_types, save_types, pedantic, rtt_checks

#include "commands.h"
#include "configuration.h"
#include "portal.h"

#define MAX_BPS 65000   // The maximum number of bytes we send per second.
#define MAX_LENGTH 1000 // The maximum number of bytes per line

string *buffer = ({});	// Outgoing buffer for everything above MAX_BPS.
int out;		// Counter of bytes in this second.
int last;		// Time when we last have sent anything.

string head;		// The save_value() header we received.
int sent_head;		// Did we send our save_value() header?

string cmd_in;		// The message we are currently receiving.

string mud;		// Our peer.
int ready;              // Whether the connection is ready and verified.

/*
 * Log the message to /log/PORTAL.
 */
private void log_message(string msg, varargs mixed* args)
{
   log_file("PORTAL", sprintf("[%s] "+msg, strftime("%d.%m.%Y %H:%M:%S"), args...));
}

/*
 * Close this connection.
 */
int remove()
{
    if(mud)
        PORTAL_SERVER->closing(this_object());
    destruct(this_object());
    return 1;
}

private void send_delayed();

/*
 * Callback for establishing a TLS connection.
 *
 * We verify the peer's certificate here.
 */
private void ready(int result, object me)
{
    mixed cert;
    string name, role;

    if(result)
    {
        log_message("Connection refused: %Q\n", result);
        remove();
        return;
    }

    cert = tls_check_certificate(this_object());
    if(!cert || cert[0])
    {
        log_message("Certificate invalid: %Q\n", cert);
        remove();
        return;
    }

    for(int i=0; i<sizeof(cert[1]); i+=3)
        switch(cert[1][i])
        {
            case "2.5.4.3":  // CommonName
                name = cert[1][i+2];
                break;

            case "2.5.4.72": // Role
                role = cert[1][i+2];
                break;
        }

    if(role != "Portal")
    {
        log_message("Certificate's role invalid: %Q\n", cert);
        remove();
        return;
    }

    PORTAL_SERVER->opened(this_object(), name);

    ready = 1;

    remove_call_out(#'remove);
    remove_call_out(#'send_delayed);
    send_delayed();
}

/*
 * Callback for establishing a connection.
 */
int logon(int flag)
{
    if (flag < 0 || !efun::this_interactive())
    {
        remove();
        return 0;
    }

    configure_interactive(this_object(), IC_TELNET_ENABLED, 0);
    configure_interactive(this_object(), IC_PROMPT, "");

    add_action("input","", AA_NOSPACE);

    if(tls_query_connection_state(this_object()))
        ready(0, this_object());
    else
    {
#ifdef PORTAL_CERT
        configure_driver(DC_TLS_CERTIFICATE, PORTAL_CERT);
#endif
        tls_init_connection(this_object(), #'ready);
    }
    return 1;
}

/*
 * Our connection is dead.
 *
 * This is called from the master, when the connection was lost.
 */
void disconnect()
{
    // master->prepare_destruct() also calls disconnect(),
    // therefore do a destruct via call_out().
    if(mud)
        PORTAL_SERVER->closing(this_object());
    call_out(#'destruct, 0, this_object());
}

/*
 * Open a connection to the given host.
 *
 * Called from the portal server when we need a connection
 * to another MUD.
 */
void connect(string ip, int port)
{
    if(load_name(previous_object()) != PORTAL_SERVER)
        return;

    call_out(#'remove, 10);
    if(net_connect(ip, port))
    {
        log_message("Connection to %s:%d refused.\n", ip, port);
        remove();
    }
}

/*
 * Receive a message.
 */
static int input(string str)
{
    mixed value;

    if(!str)
        return 1;

    if(!head)
    {
        head = str + "\n";
        return 1;
    }

    if(!cmd_in)
        cmd_in = head;
    if(str[<1] == '\t')
    {
        cmd_in += str[0..<2];
        return 1;
    }

    str = cmd_in + str + "\n";
    cmd_in = 0;

    if(catch(value = restore_value(str)) || !mappingp(value))
    {
        remove();
        return 1;
    }

    PORTAL_SERVER->receive_tcp(this_object(), value);

    return 1;
}

/*
 * Send any remaining bytes out.
 */
private void send_delayed()
{
    int rest;

    if(last != time())
    {
        last = time();
        out = 0;
    }

    rest = MAX_BPS - out;

    while(sizeof(buffer) && rest > 0 && ready)
    {
        string msg = buffer[0];

        out += sizeof(msg);

        efun::tell_object(this_object(), msg[..rest-1]);

        if(rest < sizeof(msg))
        {
            buffer[0] = msg[rest..];
            break;
        }

        rest -= sizeof(msg);
        buffer = buffer[1..];
    }

    if(sizeof(buffer))
    {
        if(find_call_out(#'send_delayed)<0)
            call_out(#'send_delayed, 1);
    }
}

/*
 * Send a message out.
 */
void send_tcp(mixed val)
{
    string msg;

    if(load_name(previous_object()) != PORTAL_SERVER)
        return;

    msg = save_value(val);
    if(sent_head)
        msg = explode(msg, "\n")[1] + "\n";
    else
        sent_head = 1;

    while(sizeof(msg) > MAX_LENGTH)
    {
        buffer += ({msg[0..MAX_LENGTH-2] + "\t\n"});
        msg = msg[MAX_LENGTH-1..<1];
    }

    buffer += ({msg});
    if(find_call_out(#'send_delayed)<0)
        send_delayed();
}

/*
 * Catch any other tells. They should not go to the remote.
 */
void catch_tell(string str)
{
}

/*
 * Set the name of our peer (set by the portal server).
 */
void set_mud(string name)
{
    if(load_name(previous_object()) != PORTAL_SERVER)
        return;

    mud = name;
}

/*
 * The name of our peer.
 */
string query_mud()
{
    return mud;
}
