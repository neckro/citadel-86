/*
 *				libroom.c
 *
 * Library for room code.
 */

/*
 *				History
 *
 * 85Nov15 HAW  Created.
 */

#include "ctdl.h"

/*
 *				Contents
 *
 *	getRoom()		load given room into RAM
 *	putRoom()		store room to given disk slot
 */

aRoom	      roomBuf;	      /* Room buffer	      */
extern rTable *roomTab;	      /* RAM index	      */
extern CONFIG cfg;
FILE	      *roomfl;	      /* Room file descriptor     */
int	      thisRoom = LOBBY;      /* Current room	      */

/*
 * getRoom()
 *
 * Gets the designated room.
 */
void getRoom(int rm)
{
    long int s;

    /* load room #rm into memory starting at buf */
    thisRoom    = rm;
    s = (long) ((long) rm * (long) RB_TOTAL_SIZE);
    fseek(roomfl, s, 0);

    if (fread(&roomBuf, RB_SIZE, 1, roomfl) != 1)   {
	crashout(" ?getRoom(): read failed//error or EOF (1)!");
    }

    crypte(&roomBuf, RB_SIZE, rm);

    if (fread(roomBuf.msg, MSG_BULK, 1, roomfl) != 1)   {
	crashout(" ?getRoom(): read failed//error or EOF (2)!");
    }
}

/*
 * putRoom()
 *
 * stores room in buf into slot rm in ctdlroom.sys.
 */
void putRoom(int rm)
{
    long int s;

    s = (long) ((long) rm * (long) RB_TOTAL_SIZE);
    if (fseek(roomfl, s, 0) != 0) 
	crashout(" ?putRoom(): fseek failure!");

    crypte(&roomBuf, RB_SIZE, rm);

    if (fwrite(&roomBuf, RB_SIZE, 1, roomfl) != 1)   {
	crashout("?putRoom() crash!//0 returned!!!(1)");
    }

    if (fwrite(roomBuf.msg, MSG_BULK, 1, roomfl) != 1)   {
	crashout("?putRoom() crash!//0 returned!!!(2)");
    }

    crypte(&roomBuf, RB_SIZE, rm);
}
