#include "doomtype.h"
#include "p_setup.h"

void MDAT_Initialize		();
void MDAT_TextmapDefaults	();
void MDAT_LoadLinedefs	(UINT8 *data);
void MDAT_LoadMapthings	(UINT8 *data);
void MDAT_LoadSectors	(UINT8 *data);
void MDAT_LoadVertexes	(UINT8 *data);

void MDAT_LoadMapdata	(virtres_t*);
boolean MDAT_LoadNodes	(virtres_t*);

typedef enum {
	NT_BINARY,
	NT_XNOD,
	NT_ZNOD,
	NT_XGLN,
	NT_ZGLN,
	NT_XGL2,
	NT_ZGL2,
	NT_XGL3,
	NT_ZGL3,
	NT_UNSUPPORTED
} nodetype_t;
