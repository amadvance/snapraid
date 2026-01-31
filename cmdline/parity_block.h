

#ifndef __PARITY_BLOCK
#define __PARITY_BLOCK

/****************************************************************************/
/* snapraid */

#include "state.h"

void dump_parity_files_for_shrink(struct snapraid_state* state, data_off_t parityToFreeInMegaBytes);

#endif