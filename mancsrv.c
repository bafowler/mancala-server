#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */
#define GREETING "Welcome to Mancala. What is your name?\r\n"
#define WRONGTURNMSG "It is not your move.\r\n"
#define INVALIDMOVEMSG "Invalid move, please try again.\r\n"
#define MOVEMSG "Your move?\r\n"
#define EMPTYNAMEMSG "Your name cannot be empty, please try again.\r\n"
#define MATCHNAMEMSG "Another player has that name, please try again.\r\n"

int port = 3000;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME+1];
    char *name_ptr;               // Pointer to end of data in name
    int in_name;                  // Number of bytes currently in name
    int name_room;                // Room left in name
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits, pits[NPITS] is end pit
    struct player *next;
};
struct player *playerlist = NULL;          
struct player *uninit_playerlist = NULL; 
struct player *active_player = NULL;

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();
extern void broadcast(char *s, struct player *);
extern int accept_connection();
extern int init_new_player(int);
extern int get_player_name(struct player *);
extern int find_network_newline(const char *, int);
extern int read_from_player(struct player *);
extern void play(struct player *, int);
extern void remove_player(struct player *, struct player **);
extern void pass_turn(struct player *);
extern struct player * find_player(int, struct player *);
extern void display_game_state();

int main(int argc, char **argv) {
    char msg[MAXMESSAGE + 1];

    parseargs(argc, argv);
    makelistener();
    // Set up file descriptor table
    int maxfd = listenfd;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listenfd, &read_fds);
    // Main game loop
    while (!game_is_over()) {
        fd_set fds = read_fds;  // Make copy of file descriptor table
        // Block indefinitely until a player connects, enters input, or disconnects
        int ready = select(maxfd + 1, &fds, NULL, NULL, NULL);
        if (ready == -1) {
            perror("select");
            exit(1);
        }
        int num_processed = 0; // The number of clients processed in this iteration of the loop
        // Check if any player is asking to connect
        if (FD_ISSET(listenfd, &fds)) {
            num_processed++;
            int clientfd = accept_connection();
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            FD_SET(clientfd, &read_fds);   
            if (write(clientfd, GREETING, strlen(GREETING)) < 0) {
                perror("write");
                return 1;
            }
            printf("Accepted a new connection.\n");
        }
        // Check entire file descriptor table to see if any player has entered input or disconnected
        int client_closed;
        for(int i = 0; i<= maxfd; i++) {
            if (num_processed >= ready) {
                break;
            }
            if(FD_ISSET(i, &fds) && i != listenfd) {
                num_processed++;
                struct player *p = find_player(i, playerlist);
                if (p) {        // Player is initialized and in-game
                    client_closed = read_from_player(p);
                    if (client_closed) {
                        printf("%s has disconnected.\n", p->name);
                        sprintf(msg, "%s has disconnected.\r\n", p->name);
                        broadcast(msg, p);
                        FD_CLR(p->fd, &read_fds);
                        remove_player(p, &playerlist);
                        free(p);
                        if (close(i) == -1) {
                            perror("close");
                            exit(1);
                        }
                    }
                } else {        // Player is uninitialized
                    client_closed = init_new_player(i);
                    if (client_closed) {
                        printf("Uninitialized player has disconnected.\n");
                        FD_CLR(i, &read_fds);
                        if (close(i) == -1) {
                            perror("close");
                            exit(1);
                        }
                    }
                }
            }
        }
    }
    
    broadcast("\r\n", NULL);
    broadcast("Game over!\r\n", NULL);
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg, NULL);
    }

    return 0;
}

/*
 * Broadcast the game board for each player and whose move it is.
 */
void display_game_state() {
    char msg[MAXMESSAGE + 1];
    char indent[11];
    for (struct player *p = playerlist; p; p = p->next) {
        sprintf(msg, "%s:  ", p->name);     // The game board for a specific player
        for (int i = 0; i <= NPITS; i++) {  // Adding each pit to the game board
            if (i != NPITS) {
                sprintf(indent, "[%d]%d ", i, p->pits[i]);
            } else {
                sprintf(indent, "[end pit]%d", p->pits[i]);
            }
            strncat(msg, indent, sizeof(msg) - strlen(msg) - 3);
        }
        strncat(msg, "\r\n\0", sizeof(msg) - strlen(msg));
        msg[MAXMESSAGE - 2] = '\r';
        msg[MAXMESSAGE - 1] = '\n';
        msg[MAXMESSAGE] = '\0';
        broadcast(msg, NULL);
    }
    
    broadcast("\r\n", NULL);
    snprintf(msg, MAXMESSAGE, "It is %s's move.\r\n", active_player->name);
    broadcast(msg, active_player);  
    if (write(active_player->fd, MOVEMSG, strlen(MOVEMSG)) < 0) {
        perror("write");
        exit(1);
    }
}

/*
 * Return the player in the linked list beginning at head associated with the file 
 * descriptor fd, NULL if none exists.
 */
struct player *find_player(int fd, struct player *head) {
    for (struct player *p = head; p; p = p->next) {
        if (p->fd == fd) {
            return p;
        }
    }
    return NULL;
}

/*
 * Change the active player (whose move it is) to p, or to the first player if
 * p is NULL.
 */
void pass_turn(struct player *p) {
    if (p) {
        active_player = p;
    } else {
        active_player = playerlist;
    }
}

/*
 * Remove player p from the list of players beginning at head.
 */
void remove_player(struct player *p, struct player **head) {
    if (p == *head) {
        *head = p->next;
    } else {
        struct player *temp = *head;
        while (temp->next != p) {
            temp = temp->next;
        }
        temp->next = p->next; 
    }
    
    if (active_player == p) {
        pass_turn(p->next);
    }
    // Only display game state if a player is removed from active player list and 
    // there is at least one player in the game
    if (*head != uninit_playerlist && playerlist) { 
        display_game_state();
    }
}

/*
 * Process a move from the active player p.
 */
void play(struct player* p, int value) {
    char msg[MAXMESSAGE + 1];
    // The play value must be a non-zero pit
    if (value >= 0 && value < NPITS && p->pits[value] > 0) {
        int num_pebbles = p->pits[value];
        int i = value + 1;
        struct player *curr = p;
        int get_another_turn = 0;   // Boolean
    
        p->pits[value] = 0;
        // Distribute pebbles according to the rules of Mancala
        while (num_pebbles > 0) {
            (curr->pits[i])++;
            num_pebbles--;
            // Active player's endpit
            if (i == NPITS) {
                if (num_pebbles == 0) {
                    get_another_turn = 1;
                }
                i = 0;
                if (curr->next) {
                    curr = curr->next;            
                } else {
                    curr = playerlist;
                }
            // Skip the endpit of any non-active player
            } else if (i == NPITS - 1 && curr != p) {
                i = 0;
                if (curr->next) {
                    curr = curr->next;            
                } else {
                    curr = playerlist;
                }
            // Move to the next pit
            } else {
                i++;
            }
        }
        snprintf(msg, MAXMESSAGE, "%s played on indentation %d.\r\n", p->name, value);
        broadcast(msg, NULL);
        printf("%s has made a move.\n", p->name);
        if (!get_another_turn) {
            pass_turn(p->next);
        }
        display_game_state();
    } else {
        if (write(p->fd, INVALIDMOVEMSG, strlen(INVALIDMOVEMSG)) < 0) {
            perror("write");
            exit(1);
        }
    }
}

/*
 * Process the input of an in-game player p.
 * Return 1 if the player has disconnected, 0 otherwise.
 */
int read_from_player(struct player * p) {
    char buf[MAXMESSAGE + 1];
    int num_read = read(p->fd, &buf, MAXMESSAGE);
    buf[num_read] = '\0';

    if (num_read == 0 || write(p->fd, buf, strlen(buf) != strlen(buf))) {
        return 1;                                   // Player has disconnected
    } else {
        if (buf[0] != '\r' && buf[0] != '\n') {     // Ignore if only newline
            int play_value = strtol(buf, NULL, 10);
            // Process the player's move only if it is their turn
            if (p == active_player) {
                play(p, play_value);
            } else {
                if (write(p->fd, WRONGTURNMSG, strlen(WRONGTURNMSG)) < 0) {
                    perror("write");
                    exit(1);
                }
            }
        }
   }
   return 0;
}

/*
 * Initialize a new player associated with the connected client on fd.
 * Return 1 if the client has disconnected, 0 otherwise.
 */
int init_new_player(int fd) {
    struct player * new_p = find_player(fd, uninit_playerlist);
    if (!new_p) {
        new_p = malloc(sizeof(struct player));
        new_p->fd = fd;
        new_p->name_ptr = new_p->name;
        new_p->in_name = 0;
        new_p->name_room = sizeof(new_p->name);
        new_p->next = uninit_playerlist;
        uninit_playerlist = new_p;
    }

    int name_error = get_player_name(new_p);
    if (name_error == 3) {          // Client has disconnected; abort initialization 
        remove_player(new_p, &uninit_playerlist);
        free(new_p);
        return 1;
    } else if (name_error == 2) {   // Invalid name; abort initialization
        new_p->name_ptr = new_p->name;
        new_p->in_name = 0;
        new_p->name_room = sizeof(new_p->name);
        return 0;
    } else if (name_error == 1) {   // Incomplete name with no newline; abort initialization
        return 0;
    }
    remove_player(new_p, &uninit_playerlist);

    char msg[MAXNAME + 26];
    printf("%s has connected.\n", new_p->name);
    sprintf(msg, "New player %s has joined.\r\n", new_p->name);
    broadcast(msg, NULL);
    
    // Compute the number of pebbles the new player should recieve
    int num_pebbles;
    if (!playerlist) {      
        num_pebbles = NPEBBLES;
    } else { 
        num_pebbles = compute_average_pebbles();
    }
    // Insert new player into the playerlist
    new_p->next = playerlist;
    playerlist = new_p;
    if (!new_p->next) {
        pass_turn(new_p);
    }
    // Populate the pits of the new player with pebbles
    for (int i = 0; i < NPITS; i++) {
        new_p->pits[i] = num_pebbles;
    }
    new_p->pits[NPITS] = 0;

    display_game_state();
    return 0;
}

/*
 * Read in a name from player p. Note that the name may arrive over multiple
 * read calls. Return 3 if the client disconnects, 2 if the name is invalid,
 * 1 if the name is incomplete, 0 if the client finishes inputting a valid,
 * complete name.
 */
int get_player_name(struct player *p) {
    int nbytes = read(p->fd, p->name_ptr, p->name_room);
    if (nbytes == 0) {      // Player has disconnected
        return 3;
    }
    p->in_name += nbytes;

    int where;
    // Look for newline, which indicates the end of name
    if ((where = find_network_newline(p->name, p->in_name)) >= 0) {
        p->name[where] = '\0';      // Replace newline with null terminator
    } else {
        p->name_room = MAXNAME + 1 - p->in_name;
        if (p->name_room <= 0) {    // Client has entered a name that is too long, kick them
            return 3;
        }
        p->name_ptr = &(p->name[p->in_name]);
        return 1;                   // Client has entered an incomplete name
    }

    // Check if name is the empty string
    if (strlen(p->name) == 0) {
        if (write(p->fd, EMPTYNAMEMSG, strlen(EMPTYNAMEMSG)) < 0) {
            perror("write");
            exit(1);
        }
        return 2;
    }
    // Check if the name is already taken by another player
    for (struct player* temp = playerlist; temp; temp = temp->next) {
        if (strcmp(p->name, temp->name) == 0) {
            if (write(p->fd, MATCHNAMEMSG, strlen(MATCHNAMEMSG)) < 0) {
                perror("write");
                exit(1);
            }
            return 2;
        }
    }

    return 0;
}

/*
 * Find a newline (either \r or \n) in buf and return its position.
 * Return -1 if no newline character could be found.
 */
int find_network_newline(const char *buf, int n) {
    for (int x = 0; x < n; x++) {
        if (buf[x] == '\n' || buf[x] == '\r') {
            return x;
        }
    }
    return -1;
}

/*
 * Broadcast message to every player in the game, except skip.
 * If skip is NULL, broadcast to every player without exception.
 */
void broadcast(char* message, struct player *skip) {
    for (struct player* p = playerlist; p; p = p->next) {
        if (p == skip) {
            continue;
        }
        if (write(p->fd, message, strlen(message)) < 0) {
            perror("write");
            exit(1);
        }
    }
}

/* 
 * Accept a connection from a new client. Return the file descriptor to
 * be used to communicate with the new client.
 */
int accept_connection() {
    struct sockaddr_in peer;
    unsigned int peer_len = sizeof(peer);
    peer.sin_family = PF_INET;

    int client_socket = accept(listenfd, (struct sockaddr *)&peer, &peer_len);
    if (client_socket < 0) {
        perror("accept");
        exit(1);
    } else {
        return client_socket;
    }
}
/*
 * If provided, set the -p command line argument to the global variable port.
 */
void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}

/*
 * Create the server socket and set up a queue to accept connections on listenfd.
 */
void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}

/*
 * Return the average number of pebbles in non-end pits of all in-game players.
 */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}

/*
 * Return 1 if the game is over, 0 otherwise. The game is over when one in-game player
 * has all their non-end pits empty.
 */
int game_is_over() {
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}
