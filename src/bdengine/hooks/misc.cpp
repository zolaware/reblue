#include "generated/reblue_init.h"

#include <rex/types.h>
#include <rex/ppc/function.h>

/**
 * @brief Multi-DVD removal.
 *
 *        All three DVD contents are merged into one folder, so no disc swap
 *        is ever needed. bdGetDiscForStage(a1, a2) is called from four sites:
 *
 *        - a1=0 (Script constructor): result used as a boolean; non-zero
 *          requests a disc change.
 *        - a1=1 (bdGameTaskConstruct): result is compared against the current
 *          disc and must match.
 *        - a1=1 (save slot builders): result is stored as disc metadata for
 *          display.
 *
 *        Returning 0 when a1=0 prevents GameDiscChange from being created.
 *        Returning 1 when a1=1 makes saves show "Disc 1" and satisfies
 *        bdGameTaskConstruct (which also runs with current disc = 1).
 */
bool bdSkipDiscNumberStore() {
    return true;
}

u32 bdGetDiscForStage_hook(u32 a1) {
    return (a1 == 0) ? 0 : 1;
}

PPC_HOOK(bdGetDiscForStage, bdGetDiscForStage_hook);