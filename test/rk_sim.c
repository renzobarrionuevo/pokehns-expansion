#include "global.h"
#include "test/test.h"
#include "data.h"
#include "random.h"
#include "randomizer.h"
#include "wild_encounter.h"
#include "constants/rtc.h"
#include "constants/trainers.h"
#include "rk_sim_data.h"

// RKstyle simulation. Runs the real randomizer code with RK settings and prints
// every replacement, so it can be reviewed outside the game.
// Run with: make check BUILD=hns TESTS="RKSIM"
// Output lines start with "RKSIM|". Species are printed as numeric IDs.

#define RKSIM_SEED_1 0x5A3C91E7
#define RKSIM_SEED_2 0x1B2E4F60
#define RKSIM_SEED_3 0xC07A3D19

static u32 BST(u16 species)
{
    const struct SpeciesInfo *i = &gSpeciesInfo[species];
    return i->baseHP + i->baseAttack + i->baseDefense + i->baseSpAttack + i->baseSpDefense + i->baseSpeed;
}

static void SetRKSimSettings(u32 seed)
{
    struct ChallengeSettings *cs = &gSaveBlock3Ptr->challengeSettings;

    cs->tx_Random_WildPokemon = 1;
    cs->tx_Random_MapBased = 1;
    cs->tx_Random_Trainer = 1;
    cs->tx_Random_Static = 1;
    cs->tx_Random_Starter = 1;
    cs->tx_Random_Similar = 1;
    cs->tx_Random_IncludeLegendaries = 1;
    cs->tx_Random_GenScope = 0;
    cs->tx_Random_Moves = 1;
    cs->tx_Random_Abilities = 1;
    cs->tx_Random_Chaos = 0;
    cs->tx_Challenges_Nuzlocke = 1;
    cs->tx_Nuzlocke_RKStyle = 1;

    gSaveBlock2Ptr->playerTrainerId[0] = seed;
    gSaveBlock2Ptr->playerTrainerId[1] = seed >> 8;
    gSaveBlock2Ptr->playerTrainerId[2] = seed >> 16;
    gSaveBlock2Ptr->playerTrainerId[3] = seed >> 24;
}

static const u8 sAreaCounts[] = { LAND_WILD_COUNT, WATER_WILD_COUNT, ROCK_WILD_COUNT, FISH_WILD_COUNT, HIDDEN_WILD_COUNT };

static const struct WildPokemonInfo *GetAreaInfo(const struct WildEncounterTypes *types, u32 area)
{
    switch (area)
    {
    case WILD_AREA_LAND:    return types->landMonsInfo;
    case WILD_AREA_WATER:   return types->waterMonsInfo;
    case WILD_AREA_ROCKS:   return types->rockSmashMonsInfo;
    case WILD_AREA_FISHING: return types->fishingMonsInfo;
    default:                return types->hiddenMonsInfo;
    }
}

// Wild encounters of every map, every time of day and every area (seed 1).
// Headers are split in 8 groups so each test stays short.
static void PrintWildChunk(u32 chunk)
{
    u32 i, t, area, slot, prev;

    SetRKSimSettings(RKSIM_SEED_1);
    for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        const struct WildPokemonHeader *h = &gWildMonHeaders[i];

        if (i % 8 != chunk)
            continue;
        for (area = 0; area < ARRAY_COUNT(sAreaCounts); area++)
        {
            for (t = 0; t < TIMES_OF_DAY_COUNT; t++)
            {
                const struct WildPokemonInfo *info = GetAreaInfo(&h->encounterTypes[t], area);
                bool32 repeated = FALSE;

                if (info == NULL || info->wildPokemon == NULL)
                    continue;
                for (prev = 0; prev < t; prev++)
                {
                    if (GetAreaInfo(&h->encounterTypes[prev], area) == info)
                        repeated = TRUE;
                }
                if (repeated)
                {
                    Test_MgbaPrintf("RKSIM|WSAME|%d|%d|%d|%d", h->mapGroup, h->mapNum, area, t);
                    continue;
                }
                for (slot = 0; slot < sAreaCounts[area]; slot++)
                {
                    u16 original = info->wildPokemon[slot].species;
                    u16 random = RandomizeWildEncounter(original, h->mapNum, h->mapGroup, area, slot);

                    Test_MgbaPrintf("RKSIM|W|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", h->mapGroup, h->mapNum, area, t, slot,
                                    original, BST(original), random, BST(random),
                                    info->wildPokemon[slot].minLevel, info->wildPokemon[slot].maxLevel);
                }
            }
        }
    }
}

TEST("RKSIM wild 0") { PrintWildChunk(0); }
TEST("RKSIM wild 1") { PrintWildChunk(1); }
TEST("RKSIM wild 2") { PrintWildChunk(2); }
TEST("RKSIM wild 3") { PrintWildChunk(3); }
TEST("RKSIM wild 4") { PrintWildChunk(4); }
TEST("RKSIM wild 5") { PrintWildChunk(5); }
TEST("RKSIM wild 6") { PrintWildChunk(6); }
TEST("RKSIM wild 7") { PrintWildChunk(7); }

// Fixed encounters are re-rolled every time the battle starts (the game seeds them
// with Random32), so each one is rolled 6 times, exactly like the game does.
TEST("RKSIM statics")
{
    u32 i, roll;

    SetRKSimSettings(RKSIM_SEED_1);
    for (i = 0; i < ARRAY_COUNT(sRKSimStatics); i++)
    {
        u16 original = sRKSimStatics[i].species;

        for (roll = 0; roll < 6; roll++)
        {
            u16 random = RandomizeMon(RANDOMIZER_REASON_FIXED_ENCOUNTER, GetRandomizerOption(RANDOMIZER_OPTION_SPECIES_MODE), Random32(), original);
            Test_MgbaPrintf("RKSIM|S|%d|%s|%d|%d|%d|%d|%d", i, sRKSimStatics[i].map, sRKSimStatics[i].level,
                            original, BST(original), random, BST(random));
        }
    }
}

// Gym leaders, Elite Four, champions and Red, in three different games (seeds).
// Same code path as the game: RandomizeTrainerMon(class, slot, partySize, species),
// plus RK's padding of gym leaders to at least 4 Pokemon.
static void PrintBosses(u32 seedIndex, u32 seed)
{
    u32 b, i;

    SetRKSimSettings(seed);
    for (b = 0; b < ARRAY_COUNT(sRKSimBosses); b++)
    {
        u32 count = sRKSimBosses[b].count, minLevel = 100;
        u16 cls = sRKSimBosses[b].cls;

        for (i = 0; i < count; i++)
        {
            u16 original = sRKSimBosses[b].species[i];
            u16 random = RandomizeTrainerMon(cls, i, count, original);

            if (sRKSimBosses[b].lvl[i] < minLevel)
                minLevel = sRKSimBosses[b].lvl[i];
            Test_MgbaPrintf("RKSIM|B|%d|%s|%d|%d|%d|%d|%d|%d", seedIndex, sRKSimBosses[b].name, i,
                            sRKSimBosses[b].lvl[i], original, BST(original), random, BST(random));
        }
        if (cls == TRAINER_CLASS_LEADER_HNS || cls == TRAINER_CLASS_LEADER_KANTO_HNS)
        {
            for (i = count; i < 4; i++)
            {
                u16 random = RandomizeTrainerMon(cls, i, 4 + count, sRKSimBosses[b].species[i % count]);
                Test_MgbaPrintf("RKSIM|B|%d|%s|%d|%d|%d|%d|%d|%d", seedIndex, sRKSimBosses[b].name, i,
                                minLevel, 0, 0, random, BST(random));
            }
        }
    }
}

TEST("RKSIM bosses 1") { PrintBosses(1, RKSIM_SEED_1); }
TEST("RKSIM bosses 2") { PrintBosses(2, RKSIM_SEED_2); }
TEST("RKSIM bosses 3") { PrintBosses(3, RKSIM_SEED_3); }

// Starters in 12 different games
static void PrintStarters(u32 first, u32 last)
{
    u32 g, i;

    for (g = first; g < last; g++)
    {
        SetRKSimSettings(RKSIM_SEED_1 + g * 0x9E3779B1);
        for (i = 0; i < 3; i++)
        {
            u16 random = RandomizeStarterAndGiftMon(i, gStarterAndGiftMonTable);
            Test_MgbaPrintf("RKSIM|ST|%d|%d|%d|%d|%d", g + 1, gStarterAndGiftMonTable[i], BST(gStarterAndGiftMonTable[i]), random, BST(random));
        }
    }
}

TEST("RKSIM starters 1") { PrintStarters(0, 3); }
TEST("RKSIM starters 2") { PrintStarters(3, 6); }
TEST("RKSIM starters 3") { PrintStarters(6, 9); }
TEST("RKSIM starters 4") { PrintStarters(9, 12); }

// Size of the replacement pool by BST, to see how many options the top bands have.
TEST("RKSIM pool")
{
    u32 s, kind;
    SetRKSimSettings(RKSIM_SEED_1);
    for (s = 1; s < NUM_SPECIES; s++)
    {
        for (kind = 0; kind < 2; kind++)
        {
            if (RKStyle_IsReplacementCandidate(s, kind) && BST(s) >= 540)
                Test_MgbaPrintf("RKSIM|POOL|%d|%d|%d|%d|%d", kind, s, BST(s), gSpeciesInfo[s].isSubLegendary, gSpeciesInfo[s].isRestrictedLegendary || gSpeciesInfo[s].isMythical);
        }
    }
}

