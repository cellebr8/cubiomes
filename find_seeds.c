#include "finders.h"
#include "generator.h"
#include "quadbase.h" // who named the file this
#include "util.h"
#include <stdio.h>

#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <pthread.h>
typedef pthread_t       thread_id_t;
#define IS_DIR_SEP(C)   ((C) == '/')

#include <string.h>

int mc = MC_1_16_1;
uint64_t lower48;
Generator g;
StructureConfig sconf = { 30084232, 27, 23, Fortress, DIM_NETHER, 0}; // fort

// SET THESE TO THE SAME NUMBER
int threads = 4;
FILE* fileptrs[4];

// structureseed,x,z,rotation,depth

// i don't want to do this
#define MAX_PATHLEN 4096

typedef pthread_t       thread_id_t;

STRUCT(linked_seeds_t)
{
    uint64_t seeds[100];
    size_t len;
    linked_seeds_t *next;
};

STRUCT(threadinfo_t)
{
    // seed range
    uint64_t start, end;
    const uint64_t *lowBits;
    int lowBitN;
    char skipStart;

    // testing function
    int (*check)(uint64_t, void*);
    void *data;

    // abort check
    volatile char *stop;

    // output
    char path[MAX_PATHLEN];
    FILE *fp;
    linked_seeds_t ls;
};

static void *searchAll48Thread(void *data)
{
// TODO TEST:
// lower bits with various ranges

    threadinfo_t *info = (threadinfo_t*)data;

    uint64_t seed = info->start;
    uint64_t end = info->end;
    linked_seeds_t *lp = &info->ls;
    lp->len = 0;
    lp->next = NULL;

    if (info->lowBits)
    {
        uint64_t hstep = 1ULL << info->lowBitN;
        uint64_t hmask = ~(hstep - 1);
        uint64_t mid;
        int idx, cnt;

        for (cnt = 0; info->lowBits[cnt]; cnt++);

        mid = info->start & hmask;
        for (idx = 0; (seed = mid | info->lowBits[idx]) < info->start; idx++);

        while (seed <= end)
        {
            if unlikely(info->check(seed, info->data))
            {
                if (seed == info->start && info->skipStart) {} // skip
                else if (info->fp)
                {
                    fprintf(info->fp, "%" PRId64"\n", (int64_t)seed);
                    fflush(info->fp);
                }
                else
                {
                    lp->seeds[lp->len] = seed;
                    lp->len++;
                    if (lp->len >= sizeof(lp->seeds)/sizeof(uint64_t))
                    {
                        linked_seeds_t *n =
                            (linked_seeds_t*) malloc(sizeof(linked_seeds_t));
                        if (n == NULL)
                            exit(1);
                        lp->next = n;
                        lp = n;
                        lp->len = 0;
                        lp->next = NULL;
                    }
                }
            }

            idx++;
            if (idx >= cnt)
            {
                idx = 0;
                mid += hstep;
                if (info->stop && *info->stop)
                    break;
            }

            seed = mid | info->lowBits[idx];
        }
    }
    else
    {
        while (seed <= end)
        {
            if unlikely(info->check(seed, info->data))
            {
                if (seed == info->start && info->skipStart) {} // skip
                else if (info->fp)
                {
                    fprintf(info->fp, "%" PRId64"\n", (int64_t)seed);
                    fflush(info->fp);
                }
                else
                {
                    lp->seeds[lp->len] = seed;
                    lp->len++;
                    if (lp->len >= sizeof(lp->seeds)/sizeof(uint64_t))
                    {
                        linked_seeds_t *n =
                            (linked_seeds_t*) malloc(sizeof(linked_seeds_t));
                        if (n == NULL)
                            exit(1);
                        lp->next = n;
                        lp = n;
                        lp->len = 0;
                        lp->next = NULL;
                    }
                }
            }
            seed++;
            if ((seed & 0xfff) == 0 && info->stop && *info->stop)
                break;
        }
    }

    pthread_exit(NULL);
}

static int mkdirp(char *path)
{
    int err = 0, len = strlen(path);
    char *p = path;

    while (IS_DIR_SEP(*p)) p++;

    while (!err && p < path+len)
    {
        char *q = p;
        while (*q && !IS_DIR_SEP(*q))
            q++;

        if (p != path) p[-1] = '/';
        *q = 0;

        struct stat st;
        if (stat(path, &st) == -1)
            err = mkdir(path, 0755);
        else if (!S_ISDIR(st.st_mode))
            err = 1;

        p = q+1;
    }

    return err;
}

int searchAll48b(
        uint64_t **         seedbuf,
        uint64_t *          buflen,
        const char *        path,
        int                 threads,
        const uint64_t *    lowBits,
        int                 lowBitN,
        int (*check)(uint64_t s48, void *data),
        void *              data,
        volatile char *     stop
        )
{
    threadinfo_t *info = (threadinfo_t*) malloc(threads* sizeof(*info));
    thread_id_t *tids = (thread_id_t*) malloc(threads* sizeof(*tids));
    int i, t;
    int err = 0;

    if (path)
    {
        size_t pathlen = strlen(path);
        char dpath[MAX_PATHLEN];

        // split path into directory and file and create missing directories
        if (pathlen + 8 >= sizeof(dpath))
            goto L_err;
        strcpy(dpath, path);

        for (i = pathlen-1; i >= 0; i--)
        {
            if (IS_DIR_SEP(dpath[i]))
            {
                dpath[i] = 0;
                if (mkdirp(dpath))
                    goto L_err;
                break;
            }
        }
    }
    else if (seedbuf == NULL || buflen == NULL)
    {
        // no file and no buffer return: no output possible
        goto L_err;
    }

    // prepare the thread info and load progress if present
    for (t = 0; t < threads; t++)
    {
        info[t].start = (t * (MASK48+1) / threads);
        info[t].end = ((t+1) * (MASK48+1) / threads - 1);
        info[t].lowBits = lowBits;
        info[t].lowBitN = lowBitN;
        info[t].skipStart = 0;
        info[t].check = check;
        info[t].data = (void*)(long int)t;
        info[t].stop = stop;

        if (path)
        {
            // progress file of this thread
            snprintf(info[t].path, sizeof(info[t].path), "%s.part%d", path, t);
            FILE *fp = fopen(info[t].path, "a+");
            if (fp == NULL)
                goto L_err;

            int c, nnl = 0;
            char buf[32];

            // find the last newline
            for (i = 1; i < 32; i++)
            {
                if (fseek(fp, -i, SEEK_END)) break;
                c = fgetc(fp);
                if (c <= 0 || (nnl && c == '\n')) break;
                nnl |= (c != '\n');
            }

            if (i < 32 && !fseek(fp, 1-i, SEEK_END) && fread(buf, i-1, 1, fp) > 0)
            {
                // read the last entry, and replace the start seed accordingly
                int64_t lentry;
                if (sscanf(buf, "%" PRId64, &lentry) == 1)
                {
                    info[t].start = lentry;
                    info[t].skipStart = 1;
                    printf("Continuing thread %d at seed %" PRId64 "\n",
                        t, lentry);
                }
            }

            fseek(fp, 0, SEEK_END);
            info[t].fp = fp;
        }
        else
        {
            info[t].path[0] = 0;
            info[t].fp = NULL;
        }
    }


    // run the threads
    for (t = 0; t < threads; t++)
    {
        pthread_create(&tids[t], NULL, searchAll48Thread, (void*)&info[t]);
    }

    for (t = 0; t < threads; t++)
    {
        pthread_join(tids[t], NULL);
    }

    if (stop && *stop)
        goto L_err;

    if (path)
    {
        // merge partial files
        FILE *fp = fopen(path, "w");
        if (fp == NULL)
            goto L_err;

        for (t = 0; t < threads; t++)
        {
            rewind(info[t].fp);

            char buffer[4097];
            size_t n;
            while ((n = fread(buffer, sizeof(char), 4096, info[t].fp)))
            {
                if (!fwrite(buffer, sizeof(char), n, fp))
                {
                    fclose(fp);
                    goto L_err;
                }
            }

            fclose(info[t].fp);
            remove(info[t].path);
        }

        fclose(fp);

        if (seedbuf && buflen)
        {
            *seedbuf = loadSavedSeeds(path, buflen);
        }
    }
    else
    {
        // merge linked seed buffers
        *buflen = 0;

        for (t = 0; t < threads; t++)
        {
            linked_seeds_t *lp = &info[t].ls;
            do
            {
                *buflen += lp->len;
                lp = lp->next;
            }
            while (lp);
        }

        *seedbuf = (uint64_t*) malloc((*buflen) * sizeof(uint64_t));
        if (*seedbuf == NULL)
            exit(1);

        i = 0;
        for (t = 0; t < threads; t++)
        {
            linked_seeds_t *lp = &info[t].ls;
            do
            {
                memcpy(*seedbuf + i, lp->seeds, lp->len * sizeof(uint64_t));
                i += lp->len;
                linked_seeds_t *tmp = lp;
                lp = lp->next;
                if (tmp != &info[t].ls)
                    free(tmp);
            }
            while (lp);
        }
    }

    if (0)
L_err:
        err = 1;

    free(tids);
    free(info);

    return err;
}

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

int check_seed(uint64_t lower48, void* file)
{
    // The structure position depends only on the region coordinates and
    // the lower 48-bits of the world seed.
    Pos p;
    if (!getFortressPosOpt(lower48, 0, 0, &p))
        return 0; // next seed

    // forts within first quadrants around spawn
    if (p.x > 23 || p.z > 23 || p.x < -27 || p.z < -27) {
        return 0; // next seed
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
            fputs(buffer, fileptrs[(long int)file]);
            spawners++;
        }
    }
    return 1;
}

int main()
{
    setupGenerator(&g, mc, 0);

    for (int i = 0; i < threads; i++) {
        char filename[16];
        sprintf(filename, "seeds%d.csv", i);
        fileptrs[i] = fopen(filename, "a");
    }

    // check all 2^48 structure seeds (it saves progress)
    searchAll48b(NULL, NULL, "progress", threads, NULL, 0, check_seed, NULL, NULL);
}