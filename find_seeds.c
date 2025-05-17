#include "finders.h"
#include "generator.h"
#include "quadbase.h" // who named the file this
#include "util.h"
#include <stdio.h>
#include <pthread.h>

int mc = MC_1_16_1;
uint64_t lower48;
Generator g;
StructureConfig sconf = { 30084232, 27, 23, Fortress, DIM_NETHER, 0}; // fort

FILE* fileptr;

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
    getRegPos(pos, &seed, regX, regZ, sconf); // idfk
    return nextInt(&seed, 5) < 2;
}

int check_seed(uint64_t lower48, void* ineedsomethingheretomakesearchall48happyidk)
{
    // The structure position depends only on the region coordinates and
    // the lower 48-bits of the world seed.
    Pos p;
    if (!getFortressPosOpt(lower48, 0, 0, &p))
        return 0; //next seed

    // forts within 1000 blocks of (0,0) (calculating world spawn would take too long)
    if (p.x > 62 || p.z > 62) {
        return 0;
    }

    Piece test[400]; // store fortress pieces

    // only need 48-bits of seed for structures
    int part_count = getFortressPieces(test, 400, mc, lower48, p.x, p.z);

    int spawners = 0;
    for(int n = 0; n <= part_count && spawners != 2; n++) {
        if (test[n].type == 5) {
            char buffer [54];
            sprintf(buffer, "%" PRId64 ",%d,%d,%d,%d\n",
                lower48 ,test[n].pos.x, test[n].pos.z, test[n].rot, test[n].depth);
            fputs(buffer, fileptr);
            spawners++;
        }
    }
    return 1;
}

int main()
{
    int threads = 4;

    setupGenerator(&g, mc, 0);

    // structureseed,x,z,rotation,depth
    fileptr = fopen("seeds.csv", "a");

    if (fileptr == NULL) {
        printf("file broken");
        return 0;
    }

    // check all 2^48 structure seeds (it saves progress)
    searchAll48(NULL, NULL, "progress", threads, NULL, 0, check_seed, NULL, NULL);
}