/* town_icons (Phase 207 add-on, NOT original behavior): the icons, 7x5 pixel
 * art in game pixels (the owner-approved mockups, /tmp/city_icons of the
 * design stage). overlay.c adds a one-pixel black outline (9x7, the inside of
 * a 10x8 map cell). '.' is transparent; every other code is a palette index
 * that is identical in mm4.pal (Clouds) and dark.pal (Darkside), so an icon
 * looks the same in both worlds and follows the game's fades. */
static uint8_t ti_colour(char c)
{
    switch(c){
    case 'W':return 1;   /* white */
    case 'w':return 9;   /* light grey */
    case 'm':return 16;  /* grey */
    case 'd':return 23;  /* dark grey */
    case 'Y':return 243; /* gold */
    case 'y':return 203; /* dark gold */
    case 'o':return 167; /* brown */
    case 'R':return 184; /* red */
    case 'B':return 38;  /* blue */
    case 'b':return 42;  /* dark blue */
    case 'c':return 65;  /* light blue */
    case 'P':return 117; /* purple */
    case 'p':return 124; /* dark purple */
    case 'T':return 105; /* teal */
    case 't':return 110; /* dark teal */
    case 'N':return 164; /* tent brown */
    case 'n':return 173; /* dark tent brown */
    case 'K':return 0;   /* black */
    default:return 0;
    }
}
/* Indexed by the TI_ kinds (overlay.c). */
static const char *const ti_art[TI_KINDS][5]={
    {".YYYYY.",          /* TI_BANK: a gold coin */
     "YYyyyYY",
     "YyYYYyY",
     "YYyyyYo",
     ".oyyyo."},
    {"wWWWWWw",          /* TI_BLACKSMITH: an anvil */
     "..wWWWm",
     "...mm..",
     "..mmmm.",
     ".dddddd"},
    {"...P...",          /* TI_GUILD: a wizard's hat */
     "..PYP..",
     "..PPP..",
     ".PPPPP.",
     "ppppppp"},
    {"WWWWW..",          /* TI_TAVERN: a mug (the inn is the tavern's Sign In) */
     "YYYYYYy",
     "YyYyY.y",
     "YyYyYYy",
     "yyyyy.."},
    {"...Y...",          /* TI_TEMPLE: a gold cross */
     ".YYYYY.",
     "...Y...",
     "...Y...",
     "...y..."},
    {"...W...",          /* TI_TRAINING: a sword */
     "...W...",
     "...w...",
     ".RRRRR.",
     "...o..."},
    {"..YYY..",          /* TI_MIRROR: a gold-framed mirror */
     ".YBcBY.",
     ".YBBBY.",
     ".YbBbY.",
     "..YyY.."},
    {"...R...",          /* TI_TENT_CLOUDS: the red and white tent */
     "..RWR..",
     ".RWRWR.",
     "RWRKRWR",
     "RWRKRWR"},
    {"...Y...",          /* TI_TENT_DARKSIDE: the teal and brown tent */
     "..TNT..",
     ".TNTNT.",
     "TNTKTNT",
     "tntKtnt"},
    {"...Y...",          /* TI_PYRAMID */
     "..YYo..",
     ".YYYoo.",
     "YYYYooo",
     "yyyyooo"},
    {".w.w.w.",          /* TI_TOWER: a grey stone tower (Phase 309, owner-approved art) */
     ".wwwww.",
     "..mKm..",
     "..mmm..",
     ".ddKdd."},
};
