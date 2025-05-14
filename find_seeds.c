#include "finders.h"
#include <stdio.h>

void getRegPos(Pos *p, uint64_t *s, int rx, int rz, StructureConfig sc)
{
    setSeed(s, rx*341873128712ULL + rz*132897987541ULL + *s + sc.salt);
    p->x = ((uint64_t)rx * sc.regionSize + nextInt(s, sc.chunkRange)) << 4;
    p->z = ((uint64_t)rz * sc.regionSize + nextInt(s, sc.chunkRange)) << 4;
}

// I think the compiler is smart enough for this to not matter but whatever
// gotta save those like 3 cpu cycles :P
int getFortressPosOpt(uint64_t seed, int regX, int regZ, Pos *pos)
{
    StructureConfig sconf = { 30084232, 27, 23, Fortress, DIM_NETHER, 0}; // fort

    getRegPos(pos, &seed, regX, regZ, sconf); // idfk
    return nextInt(&seed, 5) < 2;
}

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
        if (!getFortressPosOpt(lower48, 0, 0, &p))
            continue;

        // forts within 1000 blocks of (0,0) (calculating world spawn would take too long)
        if (p.x > 62 || p.z > 62) {
            continue;
        }

        printf("structure seed %" PRId64 "\n", (int64_t) lower48);

        Piece test[400]; // store fortress pieces

        // only need 48-bits of seed for structures
        int part_count = getFortressPieces(test, 400, mc, lower48, p.x, p.z);

        printf("part count %d \n", part_count);
        
        int spawners = 0;
        for(int n = 0; n <= part_count && spawners != 2; n++) {
            if (test[n].type == 5) {
                printf("found spawner: x:%d z:%d rot:%d depth:%d\n",
                    test[n].pos.x, test[n].pos.z, test[n].rot, test[n].depth);
                spawners++;
            }
        }
    }
}