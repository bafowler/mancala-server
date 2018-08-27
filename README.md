# mancala-server
A server written in C that uses sockets to create a dynamic and flexible game of Mancala.

Note: this program is meant to be run on a linux operating system.
Set-up:
1. Compile mancsrv.c using gcc. The command should look something like 'gcc -std=gnu99 -o mancsrv mancsrv.c'.
2. Run the executable created by gcc. If you used the gcc command above, your executable will be named 'mancsrv' and the command to run it will be './mancsrv'.
3. Open any number of new terminals and use the nc command to connect to the Mancala server. Each new terminal will be a player in the game. By default, the server runs on port 3000. 
   The command will look something like 'nc 127.0.0.1 3000'.
4. If unfamiliar with the game of Mancala, look up the rules online. Go through the different players, take different moves, and have fun!

