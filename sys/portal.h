#ifndef PORTAL_H_
#define PORTAL_H_

/* Defintions for the InterMUD portal. */

#define PORTAL_OBJECT           "/obj/portal"
#define PORTAL_SERVER           "/obj/portal_server"
#define PORTAL_CONNECTION       "/obj/portal_connection"
#define PORTAL_ROOM             "/room/portal"
#define PORTAL_GUEST            "/obj/player"
#define PORTAL_FILE             "/PORTALS"

#define DEFAULT_PORTAL_EXIT     "/room/church" // Where to put players, whose portal is unknown

/* Configuration entries in the portal file. */

#define P_CONF_SRC_PORTAL_NAME  "Portal"
#define P_CONF_ROOM             "Room"
#define P_CONF_DEST_MUD         "DestMUD"
#define P_CONF_DEST_PORTAL_NAME "DestPortal"
#define P_CONF_DEST_PORT        "DestPort"
#define P_CONF_DEST_IP          "DestIP"

/* Portal message entries */

#define P_MSG_TYPE              "Type"
#define P_MSG_PLAYER            "Player"
#define P_MSG_MUD               "MUD"
#define P_MSG_DATA              "Data"   // Command, message or save data.
#define P_MSG_PORTAL            "Portal"
#define P_MSG_IP                "IP"
#define P_MSG_PORT              "Port"
#define P_MSG_CHARACTER         "Char"

/* Portal messages types */

#define P_TYPE_HELLO            "Hi"     // Initial message.
#define P_TYPE_ENTER            "Enter"  // Player entered a guest world.
#define P_TYPE_MOVE             "Move"   // Guest moves to another guest world.
#define P_TYPE_LEAVE            "Leave"  // Guest must leave (he quit in his own world)
#define P_TYPE_QUIT             "Quit"   // He quit in the guest world.
#define P_TYPE_COMMAND          "Cmd"    // Player sent a command.
#define P_TYPE_MESSAGE          "Msg"    // Message to the real player
#define P_TYPE_PROMPT           "Prompt" // Prompt for the real player
#define P_TYPE_SAVE_DATA        "Save"   // Save data from the guest MUD.

/* Portal character entries. */

#define P_CHAR_NAME             "Name"   // The name with upper and lower case characters.
#define P_CHAR_GENDER           "Gender" // The gender (as string)

#endif /* PORTAL_H_ */
