// find a seed with a certain structure at the origin chunk
#include "finders.h"
#include <stdio.h>

int main()
{
    int structType = Fortress;
    int mc = MC_1_16_1;

    Generator g;
    setupGenerator(&g, mc, 0);

    uint64_t lower48;
    for (lower48 = 0; ; lower48++)
    {
        // The structure position depends only on the region coordinates and
        // the lower 48-bits of the world seed.
        Pos p;
        if (!getStructurePos(structType, mc, lower48, 0, 0, &p))
            continue;

        // Look for a full 64-bit seed with viable biomes, to check spawner location
        uint64_t seed;
        uint64_t upper16;
        printf("seed %" PRId64 "\n", (int64_t) lower48);

        // for (upper16 = 0; upper16 < 0x10000; upper16++)
        // {
        //     seed = lower48 | (upper16 << 48);
        //     applySeed(&g, DIM_NETHER, seed);
        //     if (isViableStructurePos(structType, &g, p.x, p.z, 0))
        //     {
        //         printf("Structure seed %" PRId64 " has a fort at chunk (%d, %d).\n",
        //             (int64_t) lower48, p.x, p.z);
        //         break;
        //     }
        // }

        Piece test[400];
        int part_count = getFortressPieces(test, 400, mc, seed, p.x, p.z);

        printf("part count %d \n", part_count);
        
        for(int n = 0; test[n].pos.z != 0; n++) {
            if (test[n].type == 5) {
                printf("found spawner: x:%d z:%d rot:%d depth:%d\n",
                    test[n].pos.x, test[n].pos.z, test[n].rot, test[n].depth);
            }
        }
    }
}