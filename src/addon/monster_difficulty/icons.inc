/* monster_difficulty (Phase 201 add-on, NOT original behavior): the icons,
 * pixel art in game pixels (the owner-approved variant D mockups). Codes:
 * L light, M mid, D dark, W white, K black, '.' transparent. overlay.c adds
 * a one-pixel black outline. Index [tier - 1]: Easy, Moderate, Hard, Deadly.
 *
 * Colours: fixed palette indices. Every map palette keeps these entries' hue
 * (only the brightness follows the map and the fades; 425 captured palettes
 * of Clouds and Darkside checked in the design stage), as the game's own text
 * colours rely on. */
static const uint8_t md_ramp[4][3]={    /* light, mid, dark */
    { 83, 87,220},                      /* Easy: green */
    { 51,242,237},                      /* Moderate: gold */
    {196,200,205},                      /* Hard: orange */
    {180,184,189}};                     /* Deadly: red */
#define MD_WHITE 1u
#define MD_BLACK 0u

static const char *const md_icon9[4][10]={
    {".......WL",   /* sword */
     "......WLM",
     ".....WLM.",
     "....WLM..",
     ".D.WLM...",
     "..DLM....",
     "..DD.....",
     ".D..D....",
     "D........",NULL},
    {"L.......L",   /* crossed swords */
     "WL.....LW",
     ".WL...LW.",
     "..WL.LW..",
     "...WLW...",
     "..DLWLD..",
     ".DD...DD.",
     "D.D...D.D",NULL},
    {"LLLLLLLLD",   /* shield with ! */
     "LMMMWMMMD",
     "LMMMWMMMD",
     "LMMMWMMMD",
     "LMMMWMMMD",
     ".LMMMMMD.",
     ".LMMWMMD.",
     "..LMMMD..",
     "...LMD...",
     "....D...."},
    {"..LLLLL..",   /* skull */
     ".LMMMMMD.",
     "LMMMMMMMD",
     "LMKKMKKMD",
     "LMKKMKKMD",
     ".LMMKMMD.",
     "..MMMMM..",
     "..MKMKM..",
     "...DDD...",NULL}};
static const char *const md_icon7[4][8]={
    {".....WL","....WL.","...WL..",".DWL...","..DD...",".D..D..","D......",NULL},
    {"W.....W","LW...WL",".LW.WL.","..LWL..",".DLWLD.",".D...D.","D.....D",NULL},
    {"LLLLLLD","LMMWMMD","LMMWMMD","LMMMMMD",".LMWMD.","..LMD..","...D...",NULL},
    {".LLLLL.","LMMMMMD","MKKMKKD","MKKMKKD",".LMKMD.",".MKMKM.","..DDD..",NULL}};
static const char *const md_dot[4]={".M.","MLM",".M.",NULL};
