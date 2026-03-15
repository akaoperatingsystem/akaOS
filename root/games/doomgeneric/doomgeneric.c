#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	puts("doomgeneric_Create: started");
	// save arguments
    myargc = argc;
    myargv = argv;

	puts("doomgeneric_Create: calling M_FindResponseFile");
	M_FindResponseFile();

	puts("doomgeneric_Create: allocating screen buffer");
	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

	puts("doomgeneric_Create: calling DG_Init");
	DG_Init();

	puts("doomgeneric_Create: calling D_DoomMain");
	D_DoomMain ();
	puts("doomgeneric_Create: finished");
}

