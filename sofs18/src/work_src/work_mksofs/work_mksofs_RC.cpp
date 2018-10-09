#include "work_mksofs.h"

#include "rawdisk.h"
#include "core.h"
#include "bin_mksofs.h"

#include <string.h>
#include <inttypes.h>

namespace sofs18
{
    namespace work
    {

        void resetBlocks(uint32_t first_block, uint32_t cnt)
        {
            soProbe(607, "%s(%u, %u)\n", __FUNCTION__, first_block, cnt);
            
            /* change the following line by your code */
            //bin::resetBlocks(first_block, cnt);

            // solution by Luis Moura, student 83808 DETI - UA
            
            uint32_t reset [cnt*(512/32)];
            for (int i = 0; i < sizeof(reset); i++){
                reset[i] = NullReference;
            }
            soWriteRawBlock(first_block, &reset);
        }

    };

};

