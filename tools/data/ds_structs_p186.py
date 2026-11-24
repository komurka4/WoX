"""Phase 186: definitions of the DS structures and tables decoded from the original's readers.

Used by tools/data/extract_program_data.py (research only) to rebuild src/data/structs.json and the Phase 186
rows of tables.json from the protected original's bytes; the values always come from the original, these
definitions give only the layout (offsets, element types, counts) and its evidence. Every layout here is
established from original code (the reader's stride, index range and field offsets; PCs are analysis
addresses, image at 10000h) and compared with the native consumer. Field names describe what the readers do
with a field; where the meaning is not decoded the description says so.
"""
import struct

# ------------------------------------------------------------------ typed tables (ds_tables.json rows)
# (offset, id, type, count, description, native consumers, original readers, phase)
MONSTER_BOX = ('by a monster\'s offset (dx, dy) from the party in the 7 x 7 box around it, index (3 - dy) x 7 + '
               'dx + 3 (2A846\'s scan)')
P186_TABLES = [
    # Phase 256: the old 4C02 region (its bytes 4C02 and 4C37 stay opaque).
    (0x4C03, 'map_cache_link_slot', 'uint8', 9,
     'the 3 x 3 map cache (slots 0 the map, 1..4 its N/E/S/W neighbours, 5..8 the NE/SE/SW/NW corners): for slot i + '
     '1, the earlier slot whose map\'s link map_cache_link[i] names it (entry 8 computed, unused); the map load\'s '
     'nine-slot loop 1A0E9..1A65F (record 0C0CEh + 37Ch x slot + 2 x link)', ['src/core/resources.c xeen_cache_prepare'],
     ['1A608 [bx+4C03]']),
    (0x4C0C, 'map_cache_link', 'uint8', 9,
     'the link (0 north, 1 east, 2 south, 3 west) of map_cache_link_slot[i]\'s map that gives slot i + 1',
     ['src/core/resources.c xeen_cache_prepare'], ['1A613 [bx+4C0C]']),
    (0x4C15, 'map_cache_other_slot', 'uint8', 9,
     'when that link is 0: the slot whose map\'s link map_cache_other_link[i] gives slot i + 1 (round the corner the '
     'other way: NE is the north map\'s east link, else the east map\'s north link)',
     ['src/core/resources.c xeen_cache_prepare'], ['1A62C [bx+4C15]']),
    (0x4C1E, 'map_cache_other_link', 'uint8', 9,
     'the link of map_cache_other_slot[i]\'s map (as map_cache_link)', ['src/core/resources.c xeen_cache_prepare'],
     ['1A637 [bx+4C1E]']),
    (0x4C27, 'object_poses', 'uint8', 16,
     'a map object\'s pose 0..3 by its facing x 4 + the party\'s facing: (party - object) mod 4; read by the scene\'s '
     'object animation, no original writer', ['src/graphics/scene.c frame_base', 'src/world/entities.c '
     'xeen_entities_orientation (installs an equal literal)'],
     ['1B648 [bx+4C27]', '1B678', '1B6A8', '1B6D6']),
    (0x4C38, 'outdoor_hidden_records', 'uint16', 44,
     'the outdoor view records (record 1166h + 12 x entry) whose frame each outdoor scene sets to FFFFh first',
     ['src/graphics/scene.c xeen_scene_prepare_render'], ['1B71B mov di, 4C38 (the walk to 4C90, 1B727 [bx+1168])']),
    # Phase 255: the hit effects' tables of the old 4D28 region (4D28..4D5B has no reader found and stays opaque).
    (0x4D5C, 'hit_effect_records', 'uint16', 3,
     'indoors, by hit slot 0..2: the indoor view-record number of the slot\'s hit effects (the physical hit\'s record, '
     'the element\'s the next one), walked by DI from 4D5C in 1BBE4', ['src/graphics/scene.c effects'],
     ['1BBEB mov di, 4D5C (the walk: 2 per hit slot)']),
    (0x4D62, 'hit_effect_x', 'int16', 6,
     'a hit effect\'s view-record x by hit slot i and choice (4i + 2 x choice; choice 1 when 98B5 is nonzero and 98B6 '
     'was zero at the scene\'s start), indoors and outdoors', ['src/graphics/scene.c effects'],
     ['1B848 [bx+4D62]', '1B900', '1BC71', '1BD29']),
    (0x4D6E, 'hit_element_x_offset', 'int16', 3,
     'by hit slot: added to hit_effect_x for the element effect\'s record (the second of the slot)',
     ['src/graphics/scene.c effects'], ['1B907 [bx+4D6E]', '1BD30']),
    (0x4D76, 'outdoor_hit_effect_records', 'uint16', 3,
     'outdoors, by hit slot 0..2: the outdoor view-record number of the slot\'s hit effects (as hit_effect_records), '
     'walked by DI from 4D76 in 1B7DA', ['src/graphics/scene.c effects'], ['1B7E1 mov di, 4D76 (the walk)']),
    # Phase 255: the scene's position tables of the old 4F9D region (its bytes 4F9D and 50AA..50AB stay opaque).
    (0x4F9E, 'minimap_rows', 'uint8', 16,
     'the minimap\'s arrow row by an index from the party\'s y (15 - index): y itself when y < 8 (1DB04, 1DB3F), '
     'y - 10h when y > 18h (1DB1E reads 4F8E + y); 1E117 reads 4F9F + the arrow\'s row / 10',
     ['src/world/minimap.c (the arrow)', 'src/addon/map_movement/map_pan.c'],
     ['1DB04 [di+4F9E]', '1DB3F', '1DB1E [di+4F8E]', '1E117 [bx+4F9F]']),
    (0x4FAE, 'monster_view_y', 'int8', 4,
     'an indoor monster\'s view-record y by the depth of its view cell 0..3 (2, 22h, 35h, 3Bh), sign-extended',
     ['src/graphics/scene.c xeen_scene_monsters'],
     ['1EE6E [4FAE]', '.. 26 direct reads in 1ED46 (3, 5, 7 and 11 for depths 0..3)']),
    (0x4FB2, 'outdoor_monster_view_y', 'int16', 26,
     'an outdoor monster\'s view-record y by its position k 0..25 in outdoor_monster_records (21865\'s walk: '
     '[bp-0Eh] = 2k)', ['src/graphics/scene.c outdoor_monsters'], ['21951 [bx+4FB2]']),
    (0x4FE6, 'object_view_x', 'int16', 24,
     'an indoor map object\'s view-record x by its view position 0..11, the second row (+18h) for the object sprite '
     '71h Clouds / 2Fh Darkside', ['src/graphics/scene.c place_objects'],
     ['20144 [bx+4FE6] (bx = 18h x row)', '.. 208B3 [bx+4FFC] (12 indexed reads)']),
    (0x5016, 'object_view_y', 'int16', 24,
     'a map object\'s view-record y by its view position 0..11 (second row +18h as object_view_x), read by the '
     'indoor and the outdoor object views alike',
     ['src/graphics/scene.c place_objects'],
     ['2014D [bx+5016] .. 208BC [bx+502C] (indoor, 12 reads)', '21AE8 [bx+5016] .. 21FF5 [bx+502C] (outdoor, 12)']),
    (0x5046, 'outdoor_object_view_x', 'int16', 24,
     'an outdoor map object\'s view-record x by its view position 0..11 (second row +18h as object_view_x)',
     ['src/graphics/scene.c place_objects'],
     ['21ADF [bx+5046] .. [bx+505C] (12 indexed reads in 219E4)']),
    (0x5076, 'outdoor_monster_records', 'uint16', 26,
     'the outdoor monster view records in the order 21865 walks them (record 1166h + 12 x entry; the list pointer '
     '[bp-10h] from 5076, 26 entries to [bp-0Eh] = 34h)', ['src/graphics/scene.c outdoor_monsters'],
     ['2186C mov [bp-10h], 5076 (the walk: 21874, 21889, .. 21959 [bx])']),
    # Phase 254 (read by their native consumers here; the rest of the old 2E01 region stays opaque):
    (0x18C2, 'wall_side_masks', 'uint16', 4,
     'by a side 0..3: a cell word without that side\'s wall nibble (0FFFh, F0FFh, FF0Fh, FFF0h), before the new wall '
     'is shifted in (2DE56)', ['src/world/minimap.c xeen_maze_set_wall'], ['2DE56 [si+18C2]']),
    (0x6E32, 'darkside_guild_ranges', 'uint8', 24,
     'a Darkside guild\'s spells: the (first, end) indexes into darkside_guild_spells (DS:1FA9 + 27h x category) at '
     'category x 8 + town x 2, the town (map - 1Dh) / 2 (3EE12\'s guild list)', ['src/game/game_guild.c'],
     ['3EE28 [bx+6E32]', '3EEF8 [bx+6E33]']),
    (0x306D, 'wall_item_frames', 'uint8', 53,
     'by a wall item\'s sprite: its frame count, the scene advancing each wall item\'s frame modulo it (1B4E6); the '
     'map load replaces the 53 bytes from the map case\'s picture file, so these are their initial values',
     ['src/graphics/scene.c xeen_scene_animate', 'src/world/entities.c xeen_entities_wall_frames (the load)'],
     ['1B4E6 [bx+306D]']),
    (0x2E1C, 'location_exit_cell', 'uint8', 120,
     'the (x, y) exit cell of a town\'s location, the pair at world x 60 + location kind x 10 + town x 2 (Clouds '
     'town = map - 1Ch, Darkside (map - 1Dh) / 2): where Sign In puts the party (25FF0)',
     ['src/game/game_location.c location_sign_in'], ['2602B [bx+2E1C]', '2604C [bx+2E1D]']),
    (0x28D6, 'misc_power_spell', 'uint8', 74,
     'by a misc item\'s power 0..73: the spell it holds (76 for power 0: the class lists\' "no spell"), whose name the '
     'Identify details show (3CECA, F868[28D6[power]])', ['src/game/game_item_options.c (Identify\'s power name)'],
     ['3CECA [bx+28D6]']),
    (0x49DA, 'monster_effect_flags', 'uint16', 120,
     'by a map monster\'s colour effect 1..15 and its step 0..7: the view record\'s flag bits, the word at 49CA + 16 x '
     'effect + 2 x step (effect 0 is never read: its row overlaps the texts at 49AD and 49CD)',
     ['src/graphics/scene.c xeen_scene_monsters, the scene\'s monster records'],
     ['1EEBC [bx+49CA]', '1EF2A', '1EF94', '.. 218D8 (27 indexed reads)']),
    (0x3054, 'monster_bob_x', 'int8', 8,
     'a flying monster\'s x offset by the scene phase 0..7 (-2, -1, 0, 1, 2, 1, 0, -1): its bob',
     ['src/graphics/scene.c xeen_scene_monsters, the scene\'s view records'],
     ['1EE84 [bx+3054]', '1EEF2', '1EF5F', '.. 219A0 (27 indexed reads, 1EE84..219A0)']),
    (0x305C, 'monster_bob_y', 'int8', 8,
     'a flying monster\'s y offset by the scene phase 0..7 (-2, 0, 2, 0, -1, 0, 2, 0)',
     ['src/graphics/scene.c xeen_scene_monsters, the scene\'s view records'],
     ['1EE8C [si+305C]', '1EEFA', '1EF67', '.. (27 indexed reads)']),
    (0x56C4, 'monster_step_dx', 'int16', 49,
     '%s: the x of the first step toward the party: +1 west of its column, -1 east, 0 on it' % MONSTER_BOX,
     ['src/world/monster_update.c spatial_move'], ['2A929 [bx+56C4]', '2A959', '2AA4D']),
    (0x5726, 'monster_step_dy', 'int16', 49,
     '%s: the y of the first step: 0 off the party\'s column, on it -1 north of the party, +1 south' % MONSTER_BOX,
     ['src/world/monster_update.c spatial_move'], ['2A921 [bx+5726]', '2A951', '2AA45']),
    (0x5788, 'monster_other_step', 'int16', 49,
     '%s: the other step, one axis: its y (-1 north of the party\'s row, +1 south), on the party\'s row (index '
     '21..27) its x' % MONSTER_BOX, ['src/world/monster_update.c spatial_move'],
     ['2A978 [bx+5788]', '2A993', '2A9D7', '2A9F2', '2AA25', '2AA37']),
    (0x57EA, 'monster_step_wall', 'uint8', 49,
     '%s: the first step\'s direction for its wall test (0 north, 1 east, 2 south, 3 west)' % MONSTER_BOX,
     ['src/world/monster_update.c spatial_move'], ['2A92D [si+57EA]', '2AA51']),
    (0x581B, 'monster_other_step_wall', 'uint8', 49,
     '%s: the other step\'s direction (0 north, 1 east, 2 south, 3 west)' % MONSTER_BOX,
     ['src/world/monster_update.c spatial_move'], ['2A998 [si+581B]', '2A9F7']),
    (0x2E01, 'class_attack_divisor', 'uint8', 10,
     'by class (the record\'s +13h, DS:286A order): a member attacks level / it + 1 times a round (the Level '
     'details: "%u Attack%s/Round", 77F5)', ['src/game/game_details.c details_begin (DETAILS_LEVEL)'],
     ['42A35 [bx+2E01]']),
    (0x2E0B, 'condition_colour', 'uint8', 17,
     'the text colour of a member\'s worst condition, by condition 0..15 and 16 Good (DS:2E94 names 11..27): 9 '
     'Cursed..Drunk, 20h Asleep..Paralyzed, 6 Unconscious..Eradicated, 0Fh Good',
     ['src/game/game_view.c view_text', 'src/game/game_combat.c qref_line'],
     ['4670D [si+2E0B] (4666A, the character view)', '46C44 [di+2E0B] (46BDB, the quick reference)']),
    (0x094E, 'terrain_frame_pairs', 'uint8', 20,
     'ten [index 0, index 1] frame pairs for the terrain view slots: the frame written to a view record\'s +2 '
     'word, selected by byte F5DF (or F5E2 != 0 for surface types 0/5/0Ch indoors, 5/8 outdoors); pair k at '
     '094E + 2k',
     ['src/graphics/scene_construct.c terrain_slot_apply (slots[].frames)', 'src/graphics/outdoor_terrain_tables.inc'],
     ['2207C..22ABB [bx+094E..0960] (22034, indoor)', '24711..250F4 [bx+094E..0960] (246CD, outdoor)']),
    (0x1EC2, 'spell_sp_cost', 'int16', 77,
     'spell points by spell 0..75 (76: the "no spell" index of class_spell_lists, cost 0); a negative value is '
     'a cost per caster level', ['src/game/game_cast.c cast_sp', 'src/game/game_bank.c'],
     ['39C29 [bx+1EC2] (39C16)', '39C9F (39C8B)', '3EB93 (3EB8B)', '3EBB3 (3EBAB)']),
    (0x1F5C, 'spell_gem_cost', 'int8', 77,
     'gems by spell 0..76 (as spell_sp_cost)', ['src/game/game_cast.c cast_gems'],
     ['39C1E/39C93 [si+1F5C]', '3DA5F (3D8C1)', '3EFA5 (3EBD6)', '3F7EE (3F05D)']),
    (0x1FA9, 'darkside_guild_spells', 'uint8', 117,
     'guild spell order, 39 per spell category (0..2): 1FA9[39*category + k], read when the world selector '
     'DS:0094 is nonzero (Darkside); the Clouds guilds use per-town lists in code', ['src/game/game_bank.c guild_list'],
     ['3EE4D cmp al,[bx+1FA9] (3EBD6)']),
    (0x201E, 'class_spell_lists', 'uint8', 120,
     'spell numbers per spell category 0..2 (the class\'s casting group), 40 each: 201E[40*category + i]; '
     'index 39 (27h) is the "no spell" slot; value 76 = no spell',
     ['src/game/game_cast.c cast_ready', 'src/game/game_combat.c', 'src/game/game_bank.c', 'src/events/event_exec.c'],
     ['3DA57.. [bx+201E] (3D8C1)', '3EC62.. (3EBD6)', '3F4FA.. (3F05D)', '2C0A2 (2BF6E)', '2CAFB/2D184 (2C94C)']),
    (0x2096, 'race_creation_values', 'uint8', 30,
     'six tables of five (race 0..4): a new character\'s starting resistances, copied at creation into the '
     'record\'s resistance bases (XEEN_CHARACTER_RESISTANCE): 2096 magic (+141h), 209B fire (+137h), 20A0 '
     'electricity (+13Bh), 20A5 cold (+139h), 20AA energy (+13Fh), 20AF poison (+13Dh) (Phase 257)',
     ['src/game/game_creation.c create_named'], ['40057..400B6 [bx+2096..20AF] (3FF1C)']),
    (0x20B4, 'material_element_resistance', 'uint8', 37,
     'by item material 0..36: added to the resistance stat of the material\'s element category (2BAFA, '
     'equipped armor and accessories)', ['src/combat/combat.c item_scan', 'src/characters/items.c xeen_item_details'],
     ['2BC58/2BD71 [bx+20B4] (2BAFA)', '3CCBC/3CDEF (3C96E)']),
    (0x20D9, 'material_element_damage', 'uint8', 37,
     'by item material 0..36: extra elemental damage of a weapon (combat), and x100 in the item price',
     ['src/combat/combat.c (extra damage)', 'src/characters/items.c xeen_item_cost', 'src/characters/items.c xeen_item_details'],
     ['36FCE [bx+20D9] (36FB5)', '3CB74/3CBC6 (3C96E)', '47110/47294/4740E (46FBD)']),
    (0x20FE, 'metal_to_hit', 'int8', 22,
     'by item material 37..58 (25h..3Ah) at 20D9 + material: the weapon\'s to-hit bonus',
     ['src/combat/combat.c prepare_weapon', 'src/characters/items.c xeen_item_details'], ['389CC [bx+20D9], material 25h..3Ah (389A0/389BB) (3891F)']),
    (0x2114, 'metal_damage', 'int8', 22,
     'by item material 37..58 at 20EF + material: the weapon\'s damage bonus',
     ['src/combat/combat.c prepare_weapon', 'src/characters/items.c xeen_item_details'], ['389EC [bx+20EF] (3891F)', '3CB3F/3CB5A (3C96E)']),
    (0x212A, 'metal_armor_class', 'int8', 22,
     'by item material 37..58 at 2105 + material: added to armor class (2BAFA stat 9, armor)',
     ['src/combat/combat.c item_scan', 'src/characters/items.c xeen_item_details'], ['2BCA4 [bx+2105], material 25h..3Ah (2BC8B/2BC93) (2BAFA)', '3CDB8 (3C96E)']),
    (0x2140, 'metal_price_factor', 'int8', 22,
     'by item material 37..58 at 211B + material: the price multiplier for 41..58 (37..40 divide the price '
     'by 10, 4, 2 and 4/3 in code and do not read their entries for the price)', ['src/characters/items.c xeen_item_cost'],
     ['470DA/4713D/4725E/472C1/473D8/4743B [bx+211B] (46FBD)']),
    (0x2156, 'attribute_material_bonus', 'int8', 72,
     'by item material 59..130 at 211B + material: the bonus to the material\'s attribute category (2BAFA) '
     'and x100 in the price', ['src/combat/combat.c item_scan', 'src/characters/items.c xeen_item_cost', 'src/characters/items.c xeen_item_details'],
     ['2BB8E/2BC0E/2BD27 [bx+211B] (2BAFA)', '3CC22/3CD3C/3CE6F (3C96E)', '46FBD']),
    (0x219E, 'weapon_base_price', 'uint16', 35, 'by weapon id 0..34', ['src/characters/items.c xeen_item_cost'],
     ['4703C [bx+219E] (46FBD)']),
    (0x21E4, 'armor_base_price', 'uint16', 14, 'by armor id 0..13', ['src/characters/items.c xeen_item_cost'],
     ['471C0 [bx+21E4] (46FBD)']),
    (0x2200, 'accessory_base_price', 'uint16', 11, 'by accessory id 0..10', ['src/characters/items.c xeen_item_cost'],
     ['4733A [bx+2200] (46FBD)']),
    (0x2216, 'misc_base_price', 'uint16', 22, 'by misc item id 0..21', ['src/characters/items.c xeen_item_cost'],
     ['474BE [bx+2216] (46FBD)']),
    (0x2242, 'armor_ac', 'uint8', 14, 'by armor id 0..13: added to armor class (2BAFA stat 9)',
     ['src/combat/combat.c item_scan', 'src/characters/items.c xeen_item_details'], ['2BC79 [bx+2242] (2BAFA)', '3CD8D (3C96E)']),
    (0x2250, 'weapon_dice_count', 'uint8', 35, 'by weapon id 0..34: damage dice rolled (3891F)',
     ['src/combat/combat.c prepare_weapon', 'src/characters/items.c xeen_item_details'], ['38A35 [bx+2250] (3891F)', '3CB03/3CB0C (3C96E)']),
    (0x2273, 'weapon_dice_sides', 'uint8', 35, 'by weapon id 0..34: sides of each damage die',
     ['src/combat/combat.c prepare_weapon', 'src/characters/items.c xeen_item_details'], ['38A4F [bx+2273] (3891F)', '3CB12 (3C96E)']),
    (0x2296, 'item_class_restriction', 'uint8', 60,
     'by 2296 + item id + category offset (481B:0000 = 0, 35, 49, 60): bit (class - 2) set = the class may not '
     'use it; weapons 0..34, armor 35..48, accessories 49..59. A misc slice from 22D2 is possible (offset 60) '
     'but not established', ['src/characters/items.c xeen_item_allowed'], ['46E89 [bx+2296] (46E56)']),
    (0x22EC, 'misc_power_price', 'uint16', 76,
     'by a misc item\'s power (+1 byte, 0..75): added to its base price', ['src/characters/items.c xeen_item_cost'],
     ['474DF [bx+22EC] (46FBD), executed over 22EE..2380 (phase110 p110-smith-identify-dark)']),
    (0x893E, 'rtl_stack_length', 'uint16', 1,
     'Borland\'s stack length word (4096): the startup reads it through ES = 481Dh (DGROUP, set at 1005D), raises '
     'it to 200h if smaller (10070) and sizes the stack segment from it', ['(none: the native has no DOS startup)'],
     ['10062 mov di, es:[893E] (ES = 481Dh at 1005D)', '10070 mov es:[893E], di']),
    (0x8C26, 'rtl_float_format_hooks', 'uint16', 4,
     'four near code pointers (code segment offsets 1FEC, 1FF1 x3) jumped through by the RTL thunks 12B76, 12B7A, '
     '12B7E, 12B82: the floating-point format hooks of printf/scanf; without the floating-point library they '
     'reach 11FEC/11FF1, which print t89C2 ("print"/"scanf" floating point formats not linked)',
     ['(none: RTL only)'], ['12B76 jmp [8C26]', '12B7A jmp [8C28]', '12B7E jmp [8C2A]', '12B82 jmp [8C2C]']),
]
# tables whose element role is known but whose meaning is not decoded (the metrics' "field-decoded" excludes them)
UNDECODED_TABLES = set()
# candidate text proven by Phase 186's targeted analysis (ds_text.json "audit": "P186")
TEXT_PROMOTIONS = {'t89C2', 't6F52', 't7263', 't5BBC', 't5BC6'}
# bytes proven to be a far-pointer string although not text-like (the extractor's text test): 6F52 = 08 20 08 2A
# (backspace, space, backspace, '*'), the target of p6E26 slot 0, read by 3E39C/3E39F
# Phase 254: strings of layout codes the text test rejects, each pushed by the original and read by ds_text():
# 7263 the guild list's line numbers ("\x02\x03l\x0b015\t0011\n2..9\n0", 3F209), 5BBC/5BC6 the control panel's
# ("\x0b000\t000\x01" 298B9 2990B, "\r"); promoted with the audit marker as 6F52 is (TEXT_PROMOTIONS).
FORCED_TEXT = {0x6F52, 0x7263, 0x5BBC, 0x5BC6}
# Phase 295: the event interpreter's strings in 630E..637A (until then the opaque region b630E): seven NUL-terminated
# runs of layout codes that the text test rejects (62..65 % printable; weak_text wants 80 %), each the far pointer
# DS:xxxx its original reader pushes (Capstone, Phase 15 image; xeen_dat_evidence/phase295/a/readers.txt): 630E "\r"
# and 6310 "\r\3c", which op 50's page loop passes to 4E38C (317B6, 317BF); 6314, op 49's sprintf format (31837,
# 14BBA at 3183E: AC0F of A827 and A43F); 6326, 633C, 6350, 6364, the sign formats of ops 2, 3, 4 and 39 (3188D,
# 31897, 318A1, 318AB, 14BBA at 318B2: 98DA of A827). Promoted with an original reader as their only evidence, so
# past both the text test and weak_text; the audit marker is P295.
READER_TEXT = {0x630E, 0x6310, 0x6314, 0x6326, 0x633C, 0x6350, 0x6364}

# Phase 254: far-pointer tables that begin inside a run of adjacent relocated pointers (the run holds several
# tables; without this they merge into the run's first). 63FE: ten ordinal suffixes, 360C4 reads [bx + 63FE] with
# bx = 4 x a number's last digit ("th", "st", "nd", "rd", then "th"); 6426: one pointer, the arena's speaker
# "The Warzone\n\t125Battle Master" (360C4 pushes 6428:6426 for 3000D). p63C6's 14 cutscene sprite files end at 63FD.
# In p27B6's run: 285A the four item categories ("Weapons".."Miscellaneous", [bx + 285A] at 45945, 459EC, 45A77),
# 286A the ten class names ("Knight".."Ranger", [bx + 286A] at 3CA39, 40480, 418E4, 43901, 44298, 45931, 46B18,
# 46F07, indexed by the record's class), 2892 the next table ("", then "Fire".., read at its entry 1: [bx + 2896] at
# 3CBF7, 3CD11, 3CE44), 28AE the item bonus names ("Might".., [bx + 28AE] at 3CC4A, 3CD64, 3CE97).
# In p2384's run (Phase 253 named the first four by their entries): 2410 armor, 2448 accessories, 2474 misc (each
# [bx + it] at 2EFA6.., 2F01B.., 2F08F..), 24CC the 85 quest items ("Deed to New Castle"..: 3E455, 3E4AA, by the
# counters F077..), 2620 the slayer types ("", "Dragons".. "Beasts": 3CC88), 263C ("", "Dragon Slayer"..: 4766F).
#
# Phase 259 (census: tools/refactor/ds_census.py direct/indexed/immediate; each start is the base of an original
# reader, a constant-address read of the slot or [bx + base] with bx = 4 x index; a pointer with no such reader
# stays in the table before it):
# - p27B6's run: 27B6 "Spell Points", "Gems" (39BBA reads [bx + 27B2], bx = 4 x 1..2: a virtual base); 27BE the
#   23 monster special attacks ("None".."Stone", 3A2EC); 281A "Spell Failed!" (39647, 3C58E, 3C6E9, 3C744); 281E
#   the items dialog's four buttons ("Equip", "Rem", "Disc", "Quest": each slot read, 4521A..4523F); 282E the shop's
#   four ("Buy", "Sell", "Identify", "Fix": 451D6..451EE); 283E the seven item actions ("Equip".."Gold", 3BBBA).
# - p263C's run: 263C "", "Dragon Slayer".. (4766F; [263C] itself, the "", at 3E59D, 3E6DC, 416B2); 2658 "",
#   "light", "awakening".., the misc items' powers (478BD).
# - p29A0's run (29A0 "Current Quests" has no reader found: it stays its own table): 29A4 "Party", "Bank" (29227,
#   29247); 29AC the four "Who will" titles ("Open Grate".."Select Char", 2FA49); 29BC the 32 verbs ("search"..
#   "try", 2FA22, 2FA3B); 2A3C "sky.sky", "night.sky" (1AC49, 1AD39, 2A145); 2A44 "Deposit", "Withdrawal" (26F0E,
#   27144); 2A4C the tavern's options window (25A3C, 25CE1, 25ED5, 276E1).
# - p2934's run: 2934 the number words (29F78); 295C "Remove", "Delete" (43A73); 2964 the quick-fight modes
#   "Attack".."Run" (3EA29; "Good", "Neutral", "Evil" after them have no reader found and stay in this table);
#   2980 "dark.cc" (15F52, 19201, 28FCB, 2904B), 2984 "xeen.cfg" (47C4D), 2988 "xeen.cc" (15D04, 19232, 29057,
#   47EBD), 298C "dark.cc" (47E9E).
# - p2E94's run (Phase 253's names, never split): 2EB4 sexes (405BB, 438CC, 46B44), 2EC0 conditions (42F3A..),
#   2F04 statistics (429A9..), 2F44 party values (29234, 29254), 2F54 skills (4044E..), 2F9C ratings (4298F),
#   2FFC the location file stems "bnkr".."trng" (27DA4; "eface08" after them has no reader found).
# - p56B4: 56BC "\x03l\n\t024" (29D09..) and 56C0 "\x03r\t124" (29D01..), the effect columns' open and close.
# - p6E18: three far format pointers, each read only at its own address (push seg; push off for sprintf): 6E18 the
#   Cast Spell window (3DAAF, 3F83F), 6E1C the guild member's line (2770F, 3F751), 6E20 the guild options window
#   (2774F, 3F13D, 3F780). p6E26: 6E26 (3E39F), 6E2A the Clouds heading (3E3E7, 3E5F2, 3E732), 6E2E the Darkside
#   heading (3E40F, 3E61A, 3E75A) of the quests lists.
# - p8A2E (the C runtime): 8A4A the full day names (1370F), 8A66 the short month names (133CE, 13724, 138F2), 8A96
#   the full month names (13739).
FAR_TABLE_STARTS = {0x2410, 0x2448, 0x2474, 0x24CC, 0x2620, 0x263C, 0x285A, 0x286A, 0x2892, 0x28AE, 0x63FE, 0x6426,
                    0x27BE, 0x281A, 0x281E, 0x282E, 0x283E, 0x2658,
                    0x29A4, 0x29AC, 0x29BC, 0x2A3C, 0x2A44, 0x2A4C,
                    0x295C, 0x2964, 0x2980, 0x2984, 0x2988, 0x298C,
                    0x2EB4, 0x2EC0, 0x2F04, 0x2F44, 0x2F54, 0x2F9C, 0x2FFC,
                    0x56BC, 0x56C0, 0x6E1C, 0x6E20, 0x6E2A, 0x6E2E, 0x8A4A, 0x8A66, 0x8A96,
                    0x17D2, 0x4CE8}
# Phase 259, second census (src/graphics/sprites.c reads these from the live DS since Phase 293): p17BE's run: 17BE the
# five Clouds songs by group (1AD90, 1ADBF), 17D2 the Darkside songs, 7 per group (1C bytes: 1AC0B, 1AC2D, 1ADFC,
# 1AE33; Clouds group 5 reads 17BE + 14h = 17D2's first). p4C90's run: 4CE8 the 16 surface sprites ("water.srf"..,
# 1ACFB, 1AF04). 4C90's eight style stems (1AE4D..1AFAA) and the outdoor wall stems, read through the virtual base
# 4CA8 (1ACAC), overlap in two entries ("", "mount"): they stay one table.

# ------------------------------------------------------------------ record types
RECORD_TYPES = {
    'view_record': {
        'size': 12,
        'about': 'one entry of a 4ADB1 draw list (4ADB1 reads [si], [si+2..A], stride 0Ch at 4AEAC)',
        'fields': [
            {'name': 'handle', 'offset': 0, 'type': 'int16', 'decoded': True,
             'role': '1-based sprite slot (4ADED dec di; shl di: the CS:03D2/05D2 tables); 0 ends the list '
                     '(4ADD6), negative skips the entry (4ADD8)'},
            {'name': 'frame', 'offset': 2, 'type': 'int16', 'decoded': True,
             'role': 'sprite frame (4AE5B: frame*4+2 into the sprite\'s frame offsets); FFFF skips (4ADDD)'},
            {'name': 'x', 'offset': 4, 'type': 'int16', 'decoded': True,
             'role': 'x, added to the window x origin CS:0BD8 (4AE23)'},
            {'name': 'y', 'offset': 6, 'type': 'int16', 'decoded': True,
             'role': 'y, added to the window y origin CS:0BDC (4AE1B)'},
            {'name': 'scale', 'offset': 8, 'type': 'int16', 'decoded': False,
             'role': 'passed in DX to the frame drawer 4AEB8; FFFF skips (4ADE2); its encoding is not decoded'},
            {'name': 'flags', 'offset': 10, 'type': 'int16', 'decoded': False,
             'role': 'passed in AX to 4AEB8 (4AE53); bits 8000h/4000h/2000h are tested there; their meaning is '
                     'not decoded'},
        ]},
    'object_animation': {
        'size': 12,
        'about': 'per object sprite id: 1B5xx..1B6xx (1B503) index 00A2 + 12*sprite + d, d = DS:4C27[4*facing + '
                 'direction] (0..3)',
        'fields': [
            {'name': 'counter_reset', 'offset': 0, 'type': 'uint8', 'count': 4, 'decoded': True,
             'role': 'the value the object\'s frame counter (+0) is reset to when it passes counter_limit, or '
                     'always while the scene is frozen (3068) (1B691 [bx+00A2])'},
            {'name': 'byte3_value', 'offset': 4, 'type': 'uint8', 'count': 4, 'decoded': False,
             'role': 'copied to the object\'s +3 byte every tick ([bx+00A6]); what +3 selects is not decoded '
                     'here'},
            {'name': 'counter_limit', 'offset': 8, 'type': 'uint8', 'count': 4, 'decoded': True,
             'role': 'the frame counter\'s upper bound (1B661 [bx+00AA]: reset when limit < counter)'},
        ]},
}


def chars(b):
    """The text token form: printable ASCII except braces literal, every other byte {XX}."""
    return ''.join(chr(c) if 0x20 <= c <= 0x7E and c not in (0x7B, 0x7D) else '{%02X}' % c for c in b)


def _rows(ds, at, rtype, n):
    rt = RECORD_TYPES[rtype]
    out = []
    for i in range(n):
        row = []
        for f in rt['fields']:
            code = {'int16': 'h', 'uint16': 'H', 'uint8': 'B', 'int8': 'b'}[f['type']]
            c = f.get('count')
            v = list(struct.unpack_from('<%d%s' % (c or 1, code), ds, at + rt['size'] * i + f['offset']))
            row.append(v if c else v[0])
        out.append(row)
    return out


def _view_list(ds, sid, name, at, end, desc, readers, consumers):
    n = (end - at) // 12
    assert (end - at) % 12 == 0 and ds[end - 12:end] == bytes(12), name
    return {'id': name, 'offset': '%04X' % at, 'size': end - at, 'audit': 'P186', 'description': desc,
            'original_readers': readers, 'consumers': consumers,
            'members': [
                {'name': 'records', 'offset': '%04X' % at, 'record': 'view_record', 'count': n - 1,
                 'values': _rows(ds, at, 'view_record', n - 1)},
                {'name': 'end', 'offset': '%04X' % (end - 12), 'record': 'view_record', 'count': 1,
                 'description': 'all zero: handle 0 ends the list (4ADD6)',
                 'values': _rows(ds, end - 12, 'view_record', 1)}]}


# ------------------------------------------------------------------ button sets (18D2..1EC1)
# Installed by the original into the live far pointers DS:98AC (x), 98A8 (y), 98A4 (width), 98A0 (height),
# 989C (key), 9898 (enabled); read by the hit test 18C0C and the key press 18AC5 (x words ending FFFF, one
# byte per button in the others). (x, y, w, h, key, on, install PCs, note)
BUTTON_SETS = [
    (0x18D2, 0x18E6, 0x18EE, 0x18F6, 0x18FE, 0x1902, ['169A3'], 'main menu, variant 0 (DS:0095 != 0)'),
    (0x18DC, 0x18EA, 0x18F2, 0x18FA, 0x18FE, 0x1906, ['169A3'], 'main menu, variant 1 (DS:0095 = 0): 169A3 adds '
     '10 (x) or 4 (the others) when DS:0095 is 0; the key table 18FE is shared'),
    (0x190A, 0x192C, 0x193C, 0x194C, 0x195C, 0x196C, ['2AD9D', '3F089'], None),
    (0x197C, 0x19A6, 0x19BA, 0x19CE, 0x19E2, 0x19F6, ['4328B'], 'the exploration buttons (18A48 tests 98AC = 197C)'),
    (0x197C, 0x19A6, 0x19BA, 0x19CE, 0x1A0A, 0x19F6, ['43248'], 'the same buttons with the second key table'),
    (0x1A1E, 0x1A26, 0x1A29, 0x1A2C, 0x1A2F, 0x1A32, ['27C58'], None),
    (0x1A1E, 0x1A26, 0x1A29, 0x1A2C, 0x1A38, 0x1A32, ['3D948'], None),
    (0x1A3C, 0x1A42, 0x1A44, 0x1A46, 0x1A48, 0x1A4A, ['2799D', '3A6C4', '3E9D0'], None),
    (0x1A4C, 0x1A5A, 0x1A60, 0x1A66, 0x1A6C, 0x1A72, ['27B29'], None),
    (0x1A78, 0x1A84, 0x1A89, 0x1A8E, 0x1A98, 0x1A93, ['27BD0'], None),
    (0x1A78, 0x1A84, 0x1A89, 0x1A8E, 0x1AA2, 0x1A93, ['27A24'], None),
    (0x1A78, 0x1A84, 0x1A89, 0x1A8E, 0x1AA7, 0x1A93, ['27AA1'], None),
    (0x1AAC, 0x1AB6, 0x1ABA, 0x1ABE, 0x1AC2, 0x1AC6, ['3983E'], None),
    (0x1ACA, 0x1AFC, 0x1B14, 0x1B2C, 0x1B44, 0x1B5C, ['46325'], None),
    (0x1B74, 0x1B8C, 0x1B97, 0x1BA2, 0x1BAD, 0x1BB8, ['43F27'], None),
    (0x1BC4, 0x1BC8, 0x1BC9, 0x1BCA, 0x1BCB, 0x1BCC, ['1919F'], 'the default (empty) set: 18C5D tests 98AC = 1BC4'),
    (0x1BCE, 0x1BE2, 0x1BEB, 0x1BF4, 0x1BFD, 0x1C06, ['2980F'], None),
    (0x1C10, 0x1C16, 0x1C18, 0x1C1A, 0x1C1C, 0x1C1E, ['29399'], None),
    (0x1C20, 0x1C26, 0x1C28, 0x1C2A, 0x1C2C, None, ['2F466', '3006E'], 'the yes/no confirm buttons: 9898 keeps the '
     'pushed value (src/game/game.c npc_confirm_begin)'),
    (0x1C30, 0x1C58, 0x1C6B, 0x1C7E, 0x1C91, 0x1CA4, ['450B6'], None),
    (0x1CB8, 0x1CDA, 0x1CEA, 0x1CFA, 0x1D0A, 0x1D1A, ['45078'], None),
    (0x1D2A, 0x1D58, 0x1D6E, 0x1D84, 0x1D9A, 0x1DB0, ['40721'], None),
    (0x1DC6, 0x1DD8, 0x1DE0, 0x1DE8, 0x1DF0, 0x1DF8, ['3F9E1'], None),
    (0x1E00, 0x1E06, 0x1E08, 0x1E0A, 0x1E0C, 0x1E0E, ['2B3A2'], None),
    (0x1E10, 0x1E1C, 0x1E21, 0x1E26, 0x1E2B, 0x1E30, ['2B776', '43AE3', '479BA'], None),
    (0x1E36, 0x1E44, 0x1E4A, 0x1E50, 0x1E56, 0x1E5C, ['3E26A'], None),
    (0x1E62, 0x1E6A, 0x1E6D, 0x1E70, 0x1E73, 0x1E76, ['4159C'], None),
    (0x1E7A, 0x1E90, 0x1E9A, 0x1EA4, 0x1EAE, 0x1EB8, ['3BB72'], None),
]
# installs of a key table alone (989C), the other five pointers kept
KEY_ONLY = [(0x18FE, '169F6'), (0x1A35, '26ED1'), (0x1A2F, '2717A')]
# bytes inside 18D2..1EC1 that no install reaches, with what they are next to
BUTTON_GAPS = {
    0x1A35: ('key', 3, 'a 3-key table (O, E, Esc) installed alone by 26ED1 for the 1A1E set'),
    0x1A38: ('key', 3, 'the 1A1E set\'s second key table (3D948)'),
    0x1A9D: ('key', 5, 'a fifth 5-key table between the 1A78 set\'s key tables; no install found (static census, '
                       'computed installs not excluded)'),
    0x1C2E: ('on', 2, 'two enabled flags after the yes/no keys; the yes/no installs leave 9898 unchanged, no '
                      'install of 1C2E found'),
}
BUTTONS_LO, BUTTONS_HI = 0x18D2, 0x1EC2


def button_struct(ds):
    arrays = {}                                   # address -> (field, count)

    def count(x):
        n = 0
        while struct.unpack_from('<H', ds, x + 2 * n)[0] != 0xFFFF:
            n += 1
        return n
    sets = []
    for x, y, w, h, k, on, pcs, note in BUTTON_SETS:
        n = count(x)
        for f, a in (('x', x), ('y', y), ('w', w), ('h', h), ('key', k), ('on', on)):
            if a is not None:
                prev = arrays.get(a)
                if prev and prev != (f, n):
                    raise SystemExit(f'button array {a:04X}: {prev} vs {(f, n)}')
                arrays[a] = (f, n)
        s = {'id': 'set%04X' % x if note is None or 'variant' not in note else 'main%d' % (x != 0x18D2),
             'buttons': n, 'x': '%04X' % x, 'y': '%04X' % y, 'w': '%04X' % w, 'h': '%04X' % h, 'key': '%04X' % k,
             'on': '%04X' % on if on is not None else None, 'install_pcs': pcs}
        if note:
            s['note'] = note
        sets.append(s)
    xs = [x['x'] for x in sets]
    for x in sets:                                # sets sharing an x array differ by their key table
        if xs.count(x['x']) > 1 and x['id'].startswith('set'):
            x['id'] = 'set%s_key%s' % (x['x'], x['key'])
    for a, (f, n, why) in BUTTON_GAPS.items():
        arrays[a] = (f, n)
    names = {'x': 'x', 'y': 'y', 'w': 'width', 'h': 'height', 'key': 'keys', 'on': 'enabled'}
    members, at = [], BUTTONS_LO
    for a in sorted(arrays):
        if a > at:                                # an unreached byte before an even x array
            pad = ds[at:a]
            if not (a - at == 1 and pad == b'\0' and arrays[a][0] == 'x' and a % 2 == 0):
                raise SystemExit(f'unexplained button bytes {at:04X}..{a:04X}')
            members.append({'name': 'pad%04X' % at, 'offset': '%04X' % at, 'type': 'bytes', 'size': 1, 'hex': '00',
                            'description': 'one 00 before a word array at an even address (alignment; no reader)'})
            at = a
        if a < at:
            raise SystemExit(f'button arrays overlap at {a:04X}')
        f, n = arrays[a]
        m = {'name': '%s%04X' % (names[f], a), 'offset': '%04X' % a}
        if f == 'x':
            m.update({'type': 'int16', 'count': n + 1,
                      'values': list(struct.unpack_from('<%dh' % (n + 1), ds, a)),
                      'description': 'x per button, then FFFF (-1) ends the set'})
            at = a + 2 * n + 2
        elif f == 'key':
            m.update({'type': 'chars', 'length': n, 'text': chars(ds[a:a + n]),
                      'description': 'the key code per button (989C)'})
            at = a + n
        else:
            m.update({'type': 'uint8', 'count': n, 'values': list(ds[a:a + n])})
            at = a + n
        if a in BUTTON_GAPS:
            m['description'] = BUTTON_GAPS[a][2]
        members.append(m)
    if at != BUTTONS_HI:
        raise SystemExit(f'button arrays end at {at:04X}, expected {BUTTONS_HI:04X}')
    return {'id': 'button_sets', 'offset': '%04X' % BUTTONS_LO, 'size': BUTTONS_HI - BUTTONS_LO, 'audit': 'P186',
            'description': 'the button sets: six parallel arrays per set (x words ending FFFF; y, width, height, '
                           'key and enabled bytes, one per button), installed as far pointers into DS:98AC, '
                           '98A8, 98A4, 98A0, 989C and 9898 and read by the hit test 18C0C (a click inside '
                           'x..x+width, y..y+height gives the key; enabled draws the press) and the key press '
                           '18AC5 (a key found in the key table presses an enabled button)',
            'original_readers': ['18C0C (hit test)', '18AC5 (key press)', '16417..1645D (main menu draw: '
                                 '[bx+1902], [bx+18E6], [bx+18D2])'],
            'consumers': ['src/ui/input_poll.c clicked_control/key_buttons/button_feedback', 'src/game/game_bank.c menu_far', 'src/game/game_view.c menu_far'],
            'sets': sets, 'key_only_installs': [{'key': '%04X' % a, 'install_pc': pc} for a, pc in KEY_ONLY],
            'members': members}


def object_animation_struct(ds):
    at, n = 0x00A2, 121
    return {'id': 'object_animation', 'offset': '%04X' % at, 'size': 12 * n, 'audit': 'P186',
            'description': 'the object sprite animation records, 12 bytes per object sprite id (121 fit before '
                           'DS:064E): the map objects\' frame counters advance each scene tick (1B503) within '
                           'these bounds. Records of zero bytes are sprite ids without an animation',
            'original_readers': ['1B661 [bx+00AA], 1B691 [bx+00A2], [bx+00A6] (1B503 object tick; executed over '
                                 '00A2..0647)'],
            'consumers': ['src/graphics/scene.c frame_base/xeen_scene_after_timer'],
            'members': [{'name': 'records', 'offset': '%04X' % at, 'record': 'object_animation', 'count': n,
                         'values': _rows(ds, at, 'object_animation', n)}]}


def _rows_struct(ds, name, at, n, tail, desc, readers, consumers):
    """n view_record rows at 'at' and a 12-byte zero tail: 'end' (4ADB1's terminator) or 'unread' (a fixed
    loop never reaches it)."""
    assert ds[at + 12 * n:at + 12 * n + 12] == bytes(12), name
    m = [{'name': 'records', 'offset': '%04X' % at, 'record': 'view_record', 'count': n,
          'values': _rows(ds, at, 'view_record', n)}]
    if tail == 'end':
        m.append({'name': 'end', 'offset': '%04X' % (at + 12 * n), 'record': 'view_record', 'count': 1,
                  'description': 'all zero: handle 0 ends the list (4ADD6)', 'values': [[0] * 6]})
    else:
        m.append({'name': 'unread', 'offset': '%04X' % (at + 12 * n), 'type': 'bytes', 'size': 12,
                  'hex': '00' * 12, 'description': 'a zero record after the rows; the fixed-count loop does '
                                                   'not read it (no reader found)'})
    return {'id': name, 'offset': '%04X' % at, 'size': 12 * n + 12, 'audit': 'P186', 'description': desc,
            'original_readers': readers, 'consumers': consumers, 'members': m}


WINDOW_LISTS = [
    (0x2A5C, 'command_icon_list', 16, 'end',
     'the command icons\' 4ADB1 draw list of 16 records (window 0): record 0 the icon box\'s background, records 1..15 '
     'the icons; drawn at 28D80 (push ds; push 2A5C, 28D93), 33FCB, 343F6 and 35992; 1791F writes its handles '
     '(the icon sprites). x/y are never written (the initial bytes are drawn). Phase 264: face_draw_list before',
     ['28D93 push 2A5C; lcall 4ADB1 (28D80)', '343C4 (33FCB)', '34F48 (343F6)', '3607E (35992)', '17A44.. (1791F)'],
     ['src/game/game.c (28D80; the 1791F handles)', 'src/game/game_cutscene.c 4ADB1(0, DS:2A5C)',
      'src/game/game_event_forms.c (28D96)']),
    (0x2B28, 'panel_draw_list', 9, 'end',
     'the control panel\'s 4ADB1 draw list (window 17h): 297D9 pushes 2B28 (2989D, 298EF); 296DE writes the '
     'frames; x/y are never written (the initial bytes are drawn)',
     ['2989D push 2B28; lcall 4ADB1 (297D9)', '298EF', '29724.. writes +2 (296DE)'],
     ['src/game/game_panel.c panel_text 4ADB1(2B28, 17h)']),
    (0x2BA0, 'item_icon_rows', 9, 'unread',
     'nine rows in the view_record layout drawn one by one by the item dialog 44F92 (45B28..45B52: 4AC92 with '
     'handle +0, frame +2, x 8, y +6 + 8, and +A); +4 and +8 are not read here',
     ['45B2F [bx+2BAA]', '45B3A [bx+2BA6]', '45B49 [bx+2BA2]', '45B52 [bx+2BA0] (44F92)', '454D1.. writes'],
     ['src/game/game_shop.c items_text (45B28)']),
    (0x2C18, 'inn_draw_list', 4, 'end',
     'a 4ADB1 draw list of four records in a 2x2 grid (window 0Bh): 43C37 pushes 2C18 (43FB2, 4412E); 4377A '
     'writes the handles; x/y are never written',
     ['43FB2 push 2C18; lcall 4ADB1 (43C37)', '4412E', '439A9.. writes (4377A)'],
     ['src/game/game_inn.c inn_rows']),
    (0x2C54, 'icon_draw_list', 24, 'end',
     'the 24-icon 4ADB1 draw list (window 18h): 44AE6 pushes 2C54 (44BF1); 44B75 writes the handles; x/y are '
     'never written',
     ['44BF1 push 2C54; lcall 4ADB1 (44AE6)', '44B75 [bx+2C54] writes'],
     ['src/game/game_view.c (the 24 icons, window 18h)']),
]


def cd_play_struct(ds):
    """Phase 332: the PlayCD tuple, three words. Its writer is event operation 60 (33480: CBW of the record's byte
    +6 to 84E0, the words +7 and +9 to 84E2 and 84E4); its reader the CD voice 483C7 (the ledger's ten references,
    all in 483C7: 483D8..483DF the three words or'ed (all zero: no play), 483FC/48403 a track above 99 less 100,
    48408/4841A start/end below 60000 then +65536, 48431../4848E.. each split by 1074B into minutes (/ 10000),
    seconds (/ 100) and frames for 48167(track, ...)), which 17E10 (F8) and the map loads' 188E8.. replay. The
    disc swap 47F7F (19D3C, 47B8E) stores 0 into all three at its end (4813A..48146), after reading the new disc's
    label into F61A (4810A); the native drive never swaps (game_panel.c cd_side_check)."""
    at = 0x84E0
    t, a, b = struct.unpack_from('<hHH', ds, at)
    desc = {'track': 'the CD track: 33489 stores CBW of the record\'s byte +6; 483FC: above 99, 48403 subtracts 100 '
                     'and the positions below 60000 take 65536 more',
            'start': 'the start position as a decimal MMSSFF number (minutes x 10000 + seconds x 100 + frames): '
                     '33490 stores the record\'s word +7; 48431.. splits it for 48167',
            'end': 'the end position, MMSSFF as start: 33497 stores the record\'s word +9; 4848E.. splits it'}
    members = [{'name': n, 'offset': '%04X' % (at + 2 * i), 'type': ty, 'count': 1, 'values': [v],
                'description': desc[n]}
               for i, (n, ty, v) in enumerate((('track', 'int16', t), ('start', 'uint16', a), ('end', 'uint16', b)))]
    return {'id': 'cd_play', 'offset': '%04X' % at, 'size': 6, 'audit': 'P186',
            'description': 'the PlayCD tuple: the CD voice segment an NPC speaks (event operation 60 writes it before '
                           'the NPC, 483C7 plays it; all zero: no play)',
            'original_readers': ['483D8 [84E2], 483DB [84E4], 483DF [84E0] (483C7)', '483FC/48403 [84E0]',
                                 '48408 [84E2], 4841A [84E4]', '48431/4844A/48466 [84E2], 4848E/484A7/484C3 [84E4]',
                                 '48472/484CF push [84E0]', '33489/33490/33497 writes (operation 60)',
                                 '4813A/48140/48146 write 0 (the disc swap 47F7F, at its end)'],
            'consumers': ['src/events/event_exec.c op_play_cd', 'src/audio/audio.c xeen_native_audio_cd_voice'],
            'members': members}


def structs(ds):
    """The Phase 186 structures over the original DS bytes, in address order."""
    return [
        object_animation_struct(ds),
        _view_list(ds, 's0962', 'indoor_view_list', 0x0962, 0x1166,
                   'the indoor scene\'s draw list (170 view records and the end record), written each frame by the '
                   'indoor stages (22034 terrain, 1ED46, 20011, 208FB) and drawn by 4ADB1(DS:0962, window 3) at '
                   '2469A; F608 = 0 (1B70F) selects these stages. The initial handles are FFFF (skipped until '
                   'written)', ['2469A push 0962; lcall 4ADB1', '22034', '1ED46', '20011'],
                   ['src/graphics/scene_construct.c xeen_scene_construct_terrain/geometry', 'src/combat/combat.c present_view']),
        _view_list(ds, 's1166', 'outdoor_view_list', 0x1166, 0x17A2,
                   'the outdoor scene\'s draw list (132 view records and the end record), written by the outdoor '
                   'stages (2126C, 219E4, 246CD) and drawn by 4ADB1(DS:1166, window 3) at 2538B; F608 != 0. '
                   'Unlike the indoor list, many initial handles are 0 (an unwritten list ends at its first '
                   'record): the stages write them before 2538B draws',
                   ['2538B push 1166; lcall 4ADB1', '246CD', '219E4', '2126C'],
                   ['src/graphics/scene_construct.c xeen_scene_construct_outdoor', 'src/graphics/sprites.c', 'src/combat/combat.c present_view']),
        button_struct(ds),
    ] + [_rows_struct(ds, name, at, n, tail, desc, rd, cons) for at, name, n, tail, desc, rd, cons in WINDOW_LISTS] + [
        cd_play_struct(ds)]


# ------------------------------------------------------------------ Phase 186A: the split of ds_text.json
# (start, end, file, about): address ranges of the strings, in order (Phase 300: src/data/texts/*.tsv). Borland places each module's literals
# together, so the ranges mostly follow purpose; "about" describes what each range mainly holds (by reading
# the strings), it is not a claim about every entry.
TEXT_PARTS = [
    (0x0000, 0x3274, 'texts/0000_runtime_and_music.tsv',
     'the Borland banner and run-time error messages; the map music files (*.m)'),
    (0x3274, 0x3B61, 'texts/3274_item_names.tsv', 'item names: weapons, armor, accessories, misc and quest items'),
    (0x3B61, 0x4603, 'texts/3B61_game_words.tsv',
     'game words: monster/attack words, skill and spell names, elements, conditions, classes, races, stats'),
    (0x4603, 0x4F00, 'texts/4603_resources_and_options.tsv',
     'resource, sound and save file names; the options window; map surface and maze file patterns'),
    (0x4F00, 0x5BD1, 'texts/4F00_towns_and_services.tsv',
     'tile/maze file patterns; town services (training, bank, tavern, temple, guild) texts and their sounds'),
    (0x5BD1, 0x6887, 'texts/5BD1_menus_events_cutscenes.tsv',
     'cursor and menu texts, game preferences, event and endgame texts, cutscene pictures and voices'),
    (0x6887, 0x75C2, 'texts/6887_magic_characters_quests.tsv',
     'casting and spell messages, character and party texts, quests and the quest window'),
    (0x75C2, 0x8C4C, 'texts/75C2_creation_shops_runtime.tsv',
     'character creation, the inn, item and shop dialogs, the CD prompt; the run-time library\'s strings and '
     'date/time names'),
]


# ------------------------------------------------------------------ Phase 291: reader-established table shapes
# id -> (shape, dims, basis). A typed table is shaped only where its original reader's index arithmetic gives the
# dimensions (C order: the first dimension has the largest stride; product(shape) == count). Phase 300: the shape is
# metadata (tools/data/meta/tables.json, with dims and basis); src/data/tables.json holds the values flat, so the
# bytes are unchanged. Every other table stays flat (e.g. town_portal_maps: "5 per world" is the Phase 184 reading,
# but 3B0D2 reads 67C3[FB00 x 10 + n], so its second five are not Darkside's; portrait_x: whether 17B8[slot] is a
# second row of six is not established).
_STEP = ('the monster step tables\' index (3 - dy) x 7 + dx + 3 (2A846\'s scan of the 7 x 7 box)', ['3 - dy (0 = 3 north '
         'of the party .. 6 = 3 south)', 'dx + 3 (0 = 3 west .. 6 = 3 east)'])
_VIEW = ('the object view positions: [bx + base] with bx = 18h x row (20144) plus 2 x position (12 indexed reads)',
         ['row (1: the object sprite 71h Clouds / 2Fh Darkside)', 'view position 0..11'])
TABLE_SHAPES = {
    'race_sp_bonus': ([5, 2], ['race 0..4', 'casting stat (Intellect, Personality)'],
                      '46500 [bx + 298F] with bx = 2 x race (+11h) + stat (1 Intellect, 2 Personality: 4649E/464A8)'),
    'movement_offset_x': ([4, 48], ['direction (N, E, S, W)', 'view/probe slot'],
                          '1C1D6..1C1E5: bx = 30h x direction + slot, [bx + 064E]; 0655 + 30h x facing (1EFC4, 3A0D3, '
                          '3B30F, 3D786)'),
    'movement_offset_y': ([4, 48], ['direction (N, E, S, W)', 'view/probe slot'],
                          '1C1F7 [di + 070E] with di = 30h x direction + slot (1C1D6..1C1E1); 0715 + 30h x facing '
                          '(24632..2463C, 3A0EE, 3B301, 3D777)'),
    'movement_wall_shift': ([4, 48], ['direction (N, E, S, W)', 'view/probe slot'],
                            '1C48B..1C499: bx = 60h x direction + 2 x slot, [bx + 07CE] (words)'),
    'terrain_frame_pairs': ([10, 2], ['terrain view slot pair k', 'index 0/1'], 'pair k at 094E + 2k: [bx + 094E..0960] '
                            '(2207C..22ABB indoors, 24711..250F4 outdoors)'),
    'darkside_guild_spells': ([3, 39], ['spell category 0..2', 'k'], '3EE4D cmp al, [bx + 1FA9]: 1FA9[39 x category + k]'),
    'class_spell_lists': ([3, 40], ['spell category 0..2', 'i (39: the "no spell" slot)'],
                          '201E[40 x category + i] (3DA57.., 3EC62.., 3F4FA..)'),
    'race_creation_values': ([6, 5], ['resistance (magic, fire, electricity, cold, energy, poison)', 'race 0..4'],
                             '40057..400B6 copy [bx + 2096], [bx + 209B] .. [bx + 20AF] by race (3FF1C): six tables '
                             'of five'),
    'location_exit_cell': ([2, 6, 5, 2], ['world', 'location kind', 'town', '(x, y)'],
                           '2602B [bx + 2E1C] and 2604C [bx + 2E1D] with bx = world x 60 + kind x 10 + town x 2'),
    'location_animation_files': ([2, 7], ['world', 'location kind'], '27DE5 [bx + 3018], bx = world x 7 + kind'),
    'location_animation_frames': ([2, 11], ['world', 'location kind'], '27818 [bx + 3026], bx = world x 11 + kind'),
    'monster_effect_flags': ([15, 8], ['colour effect 1..15', 'step 0..7'],
                             '1EEBC [bx + 49CA] .. (27 indexed reads), bx = 16 x effect + 2 x step, effect 1..15'),
    'object_poses': ([4, 4], ['object facing', 'party facing'], '1B648 [bx + 4C27] .., bx = object facing x 4 + party '
                     'facing'),
    'hit_effect_x': ([3, 2], ['hit slot 0..2', 'choice 0/1'], '1B848 [bx + 4D62] .., bx = 4 x slot + 2 x choice'),
    'object_view_x': ([2, 12], _VIEW[1], _VIEW[0] + ', 4FE6..'),
    'object_view_y': ([2, 12], _VIEW[1], _VIEW[0] + ', 5016.. (indoor and outdoor)'),
    'outdoor_object_view_x': ([2, 12], _VIEW[1], _VIEW[0] + ', 5046.. (219E4)'),
    'monster_step_dx': ([7, 7], _STEP[1], _STEP[0] + ': 2A929 [bx + 56C4]'),
    'monster_step_dy': ([7, 7], _STEP[1], _STEP[0] + ': 2A921 [bx + 5726]'),
    'monster_other_step': ([7, 7], _STEP[1], _STEP[0] + ': 2A978 [bx + 5788]'),
    'monster_step_wall': ([7, 7], _STEP[1], _STEP[0] + ': 2A92D [si + 57EA]'),
    'monster_other_step_wall': ([7, 7], _STEP[1], _STEP[0] + ': 2A998 [si + 581B]'),
    'darkside_guild_ranges': ([3, 4, 2], ['spell category 0..2', 'town (map - 1Dh) / 2', '(first, end)'],
                              '3EE28 [bx + 6E32] and 3EEF8 [bx + 6E33], bx = category x 8 + town x 2'),
}
