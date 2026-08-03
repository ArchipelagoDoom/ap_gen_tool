#include "maps.h"

#include <onut/onut.h>
#include <onut/Dialogs.h>
#include <onut/Strings.h>
#include <onut/Point.h>

#include <stdio.h>
#include <algorithm>

#include "data.h"
#include "defs.h"


// ============================================================================
// Loading WADs, and lumps from them
// ============================================================================

struct map_header_t
{
    char identification[4];
    int32_t num_lumps;
    int32_t directory_offset;
};

struct map_directory_t
{
    int32_t offset;
    int32_t size;
    char name[8];
};

struct remap_entry_t
{
    char _from[8]; // not null terminated
    char _to[8]; // not null terminated

    remap_entry_t(const char *from, const char *to)
    {
        strncpy(_from, from, 8);
        strncpy(_to, to, 8);
    }

    bool rename(map_directory_t& entry)
    {
        char copy[8]; // not null terminated

        for (int i = 0, copy_pos = 0; i < 8; ++i)
        {
            if (_from[i] == '?')
                copy[copy_pos++] = entry.name[i];
            else if (_from[i] != entry.name[i])
                return false;
            else if (_from[i] == '\0')
                break;
        }
        for (int i = 0, copy_pos = 0; i < 8; ++i)
        {
            entry.name[i] = (_to[i] == '?' ? copy[copy_pos++] : _to[i]);
            if (_to[i] == '\0')
                break;
        }
        return true;
    }
};

struct game_wad_t
{
    std::string filename;
    FILE *handle;
    std::vector<map_directory_t> directory;

    game_wad_t(std::string& fn) : filename(fn)
    {
        handle = fopen(filename.c_str(), "rb");
        if (!handle)
            handle = fopen(("wads/"+filename).c_str(), "rb");
        if (!handle)
            throw std::runtime_error(std::string("Cannot open file: ") + filename);

        // Read header
        map_header_t header;
        fread(&header, sizeof(header), 1, handle);
        if (strncmp(header.identification, "PWAD", 4) != 0 && strncmp(header.identification, "IWAD", 4) != 0)
        {
            fclose(handle);
            throw std::runtime_error(std::string("Invalid IWAD or PWAD: ") + filename);
        }

        directory.resize(header.num_lumps);
        fseek(handle, header.directory_offset, SEEK_SET);
        fread(directory.data(), sizeof(map_directory_t), header.num_lumps, handle);
    }

    void rename_lumps(std::vector<remap_entry_t>& remap_table)
    {
        if (remap_table.empty())
            return;
        for (map_directory_t &entry : directory)
        {
            for (remap_entry_t &remap : remap_table)
            {
                if (remap.rename(entry))
                    break;
            }
        }
    }
};

int find_lump_in_directory(const std::vector<map_directory_t>& directory, const char* lump_name)
{
    int len = directory.size();
    for (int i = 0; i < len; ++i)
    {
        if (!strncmp(directory[i].name, lump_name, 8))
            return i;
    }
    return -1;
}

std::vector<uint8_t> load_lump(const std::vector<game_wad_t>& wad_list, const char* lump_name)
{
    // Load from last wad to first, as that's how it's handled in game
    for (int i = wad_list.size() - 1; i >= 0; --i)
    {
        int result = find_lump_in_directory(wad_list[i].directory, lump_name);
        if (result < 0)
            continue;

        const map_directory_t &dir_entry = wad_list[i].directory[result];
        std::vector<uint8_t> ret;
        ret.resize(dir_entry.size);
        fseek(wad_list[i].handle, dir_entry.offset, SEEK_SET);
        fread(ret.data(), 1, dir_entry.size, wad_list[i].handle);
        return ret;
    }
    return {};
}

// ============================================================================

#define	NF_SUBSECTOR_VANILLA	0x8000
#define	NF_SUBSECTOR	0x80000000 // [crispy] extended nodes
#define	NO_INDEX	((unsigned short)-1) // [crispy] extended nodes


struct patch_header_t
{
    uint16_t width;
    uint16_t height;
    int16_t leftoffset;
    int16_t topoffset;
};


struct post_t
{
    uint8_t topdelta;
    uint8_t length;
    uint8_t unused;
};


template<typename T>
static bool try_load_lump(const char *lump_name, 
                          FILE *f, 
                          const map_directory_t &dir_entry, 
                          std::vector<T> &elements)
{
    if (strncmp(dir_entry.name, lump_name, 8) == 0)
    {
        auto count = dir_entry.size / sizeof(T);
        elements.resize(count);
        fseek(f, dir_entry.offset, SEEK_SET);
        fread(elements.data(), sizeof(T), count, f);
        return true;
    }
    return false;
}


int FixedMul(int a, int b)
{
    return ((int64_t) a * (int64_t) b) >> 16;
}


int point_on_side(int x, int y, node_t* node)
{
    int	dx;
    int	dy;
    int	left;
    int	right;
	
    if (!node->dx)
    {
        if (x <= node->x)
            return node->dy > 0;
	
        return node->dy < 0;
    }
    if (!node->dy)
    {
        if (y <= node->y)
            return node->dx < 0;
	
        return node->dx > 0;
    }
	
    dx = (x - node->x);
    dy = (y - node->y);
	
    // Try to quickly decide by looking at sign bits.
    if ( (node->dy ^ node->dx ^ dx ^ dy)&0x80000000 )
    {
        if  ( (node->dy ^ dx) & 0x80000000 )
        {
            // (left is negative)
            return 1;
        }
        return 0;
    }

    left = FixedMul(node->dy >> 16, dx);
    right = FixedMul(dy, node->dx >> 16);
	
    if (right < left)
    {
        // front side
        return 0;
    }

    // back side
    return 1;			
}


subsector_t* point_in_subsector(int x, int y, map_t* map)
{
    node_t*	node;
    int side;
    int nodenum;

    // single subsector is a special case
    if (map->nodes.empty())
        return map->subsectors.data();
		
    nodenum = (int)map->nodes.size() - 1;

    while (!(nodenum & NF_SUBSECTOR))
    {
        node = &map->nodes[nodenum];
        side = point_on_side(x, y, node);
        nodenum = node->children[side];
    }
	
    return &map->subsectors[nodenum & ~NF_SUBSECTOR];
}


// Replace convex polygon with right side of convex polygon cut by infinite line at point with ray delta
std::vector<Vector2> cut_convex_polygon(const std::vector<Vector2>& polygon, Vector2 point, Vector2 delta)
{
  std::vector<Vector2> cut;

  for (int j = 0, len = (int)polygon.size(), i = len - 1; j < len; i = j++)
  {
    Vector2 a = polygon[i];
    Vector2 b = polygon[j];

    bool a_side = delta.Cross(a - point).z > 0.0;
    bool b_side = delta.Cross(b - point).z > 0.0;

    if (a_side)
      cut.push_back(a);

    if (a_side != b_side)
    {
      // add intersection for a-b and line
      Vector2 d = b - a;
      float t = (point - a).Cross(delta).z / d.Cross(delta).z;
      cut.push_back(a + d * t);
    }
  }

  return cut;
}


void triangulate_polygon_for_subsector(map_t* map, const std::vector<Vector2>& polygon, int subsectornum)
{
  int sectornum = map->subsectors[subsectornum].sector;
  const map_subsector_t& subsector = map->map_subsectors[subsectornum];

  // clip against each seg in this subsector
  std::vector<Vector2> clip = polygon;
  for (int i = 0; i < subsector.numsegs; ++i)
  {
    int segnum = subsector.firstseg + i;
    const map_seg_t& seg = map->map_segs[segnum];

    const map_vertex_t& v1 = map->vertexes[seg.v1];
    const map_vertex_t& v2 = map->vertexes[seg.v2];

    Vector2 a = Vector2((float)v1.x, (float)v1.y);
    Vector2 b = Vector2((float)v2.x, (float)v2.y);
    clip = cut_convex_polygon(clip, a, a - b);
  }

  // flip polygon's y coordinate for display
  for (int j = 0; j < (int)clip.size(); ++j)
  {
    clip[j].y = -clip[j].y;
  }

  // add triangle fan of polygon to sector
  sector_t& sector = map->sectors[sectornum];
  for (int j = 2; j < (int)clip.size(); ++j)
  {
    sector.triangle_vertices.push_back(clip[0]);
    sector.triangle_vertices.push_back(clip[j - 1]);
    sector.triangle_vertices.push_back(clip[j]);
  }
}


void triangulate_polygon_for_node(map_t* map, const std::vector<Vector2>& polygon, int nodenum)
{
  if (nodenum & NF_SUBSECTOR_VANILLA)
  {
    triangulate_polygon_for_subsector(map, polygon, nodenum & ~NF_SUBSECTOR_VANILLA);
    return;
  }

  const map_node_t& node = map->map_nodes[nodenum];

  Vector2 cut_point(node.x, node.y);
  Vector2 cut_ray(node.dx, node.dy);

  triangulate_polygon_for_node(map, cut_convex_polygon(polygon, cut_point, -cut_ray), node.children[0]);
  triangulate_polygon_for_node(map, cut_convex_polygon(polygon, cut_point, cut_ray), node.children[1]);
}


void triangulate_map(map_t* map)
{
  // clear all triangulated verts from sectors
  for (int i = 0, len = (int)map->sectors.size(); i < len; ++i)
  {
    map->sectors[i].triangle_vertices.clear();
  }

  // initial polygon is map bounding box
  std::vector<Vector2> polygon = {
    Vector2(map->bb[0], map->bb[1]),
    Vector2(map->bb[2], map->bb[1]),
    Vector2(map->bb[2], map->bb[3]),
    Vector2(map->bb[0], map->bb[3])
  };

  triangulate_polygon_for_node(map, polygon, (int)map->nodes.size() - 1);
}


OTextureRef load_sprite(const std::vector<game_wad_t>& wad_list, const char* lump_name, const uint8_t* pal)
{
    auto raw_data = load_lump(wad_list, lump_name);
    if (raw_data.empty()) return nullptr;

    patch_header_t header;
    memcpy(&header, raw_data.data(), sizeof(patch_header_t));
    uint32_t* columnofs = new uint32_t[header.width * sizeof(uint32_t)];
    memcpy(columnofs, raw_data.data() + sizeof(patch_header_t), header.width * sizeof(uint32_t));

    std::vector<uint8_t> img_data;
    img_data.resize(header.width * header.height * 4);

    for (int x = 0; x < header.width; ++x)
    {
        int offset = columnofs[x];
        while (raw_data[offset] != 0xFF)
        {
            post_t post;
            memcpy(&post, &raw_data[offset], sizeof(post_t));
            offset += 3;
            for (int j = 0; j < post.length; ++j, ++offset)
            {
                int y = post.topdelta + j;
                int idx = raw_data[offset] * 3;
                int k = y * header.width * 4 + x * 4;
                img_data[k + 0] = pal[idx + 0];
                img_data[k + 1] = pal[idx + 1];
                img_data[k + 2] = pal[idx + 2];
                img_data[k + 3] = 255;
            }
            ++offset;
        }
    }

    delete[] columnofs;
    return OTexture::createFromData(img_data.data(), {header.width, header.height}, false);
}

Color get_color_for_arrow_type(arrowtype_t type)
{
    switch (type)
    {
        case ARROW_DOOR_SR:  return Color(1, 0, 1);
        case ARROW_DOOR_WR:  return Color(1, 0.5f, 1);
        case ARROW_LIFT_SR:  return Color(0, 1, 1);
        case ARROW_LIFT_WR:  return Color(0.5f, 1, 1);
        case ARROW_CRUSHER:  return Color(1, 0, 0);
        case ARROW_STAIR:    return Color(0, 0, 1);
        case ARROW_TELEPORT: return Color(1, 1, 0.5f);
        default:             return Color(1, 1, 1);
    }
}

arrowtype_t get_arrow_type(int special)
{
    switch (special)
    {
        case LT_DR_DOOR_OPEN_WAIT_CLOSE_ALSO_MONSTERS:
        case LT_DR_DOOR_OPEN_WAIT_CLOSE_FAST:
        case LT_SR_DOOR_OPEN_WAIT_CLOSE:
        case LT_SR_DOOR_OPEN_WAIT_CLOSE_FAST:
        case LT_S1_DOOR_OPEN_WAIT_CLOSE:
        case LT_S1_DOOR_OPEN_WAIT_CLOSE_FAST:
        case LT_SR_DOOR_OPEN_STAY:
        case LT_SR_DOOR_OPEN_STAY_FAST:
        case LT_S1_DOOR_OPEN_STAY:
        case LT_S1_DOOR_OPEN_STAY_FAST:
        case LT_SR_DOOR_CLOSE_STAY:
        case LT_SR_DOOR_CLOSE_STAY_FAST:
        case LT_S1_DOOR_CLOSE_STAY:
        case LT_S1_DOOR_CLOSE_STAY_FAST:
        case LT_GR_DOOR_OPEN_STAY:
        case LT_D1_DOOR_OPEN_STAY:
        case LT_D1_DOOR_OPEN_STAY_FAST:
        case LT_DR_DOOR_BLUE_OPEN_WAIT_CLOSE:
        case LT_DR_DOOR_RED_OPEN_WAIT_CLOSE:
        case LT_DR_DOOR_YELLOW_OPEN_WAIT_CLOSE:
        case LT_D1_DOOR_BLUE_OPEN_STAY:
        case LT_D1_DOOR_RED_OPEN_STAY:
        case LT_D1_DOOR_YELLOW_OPEN_STAY:
        case LT_SR_DOOR_BLUE_OPEN_STAY_FAST:
        case LT_SR_DOOR_RED_OPEN_STAY_FAST:
        case LT_SR_DOOR_YELLOW_OPEN_STAY_FAST:
        case LT_S1_DOOR_BLUE_OPEN_STAY_FAST:
        case LT_S1_DOOR_RED_OPEN_STAY_FAST:
        case LT_S1_DOOR_YELLOW_OPEN_STAY_FAST:
            return ARROW_DOOR_SR;

        case LT_WR_DOOR_OPEN_WAIT_CLOSE:
        case LT_WR_DOOR_OPEN_WAIT_CLOSE_FAST:
        case LT_W1_DOOR_OPEN_WAIT_CLOSE_ALSO_MONSTERS:
        case LT_W1_DOOR_OPEN_WAIT_CLOSE_FAST:
        case LT_WR_DOOR_OPEN_STAY:
        case LT_WR_DOOR_OPEN_STAY_FAST:
        case LT_W1_DOOR_OPEN_STAY:
        case LT_W1_DOOR_OPEN_STAY_FAST:
        case LT_WR_DOOR_CLOSE_STAY:
        case LT_WR_DOOR_CLOSE_STAY_FAST:
        case LT_W1_DOOR_CLOSE_STAY:
        case LT_W1_DOOR_CLOSE_FAST:
        case LT_WR_DOOR_CLOSE_STAY_OPEN:
        case LT_W1_DOOR_CLOSE_WAIT_OPEN:
            return ARROW_DOOR_WR;

        case LT_SR_FLOOR_LOWER_TO_LOWEST_FLOOR:
        case LT_S1_FLOOR_LOWER_TO_LOWEST_FLOOR:
        case LT_SR_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR:
        case LT_S1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR:
        case LT_S1_FLOOR_RAISE_BY_512:
        case LT_SR_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_FAST:
        case LT_S1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_FAST:
        case LT_SR_FLOOR_RAISE_TO_LOWEST_CEILING:
        case LT_S1_FLOOR_RAISE_TO_LOWEST_CEILING:
        case LT_SR_FLOOR_LOWER_TO_8_ABOVE_HIGHEST_FLOOR:
        case LT_S1_FLOOR_LOWER_TO_8_ABOVE_HIGHEST_FLOOR:
        case LT_G1_FLOOR_RAISE_TO_LOWEST_CEILING:
        case LT_SR_FLOOR_RAISE_TO_8_BELLOW_LOWEST_CEILING_CRUSHES:
        case LT_S1_FLOOR_RAISE_TO_8_BELLOW_LOWEST_CEILING_CRUSHES:
        case LT_SR_FLOOR_LOWER_TO_HIGHEST_FLOOR:
        case LT_S1_FLOOR_LOWER_TO_HIGHEST_FLOOR:
        case LT_SR_CEILING_LOWER_TO_FLOOR:
        case LT_S1_CEILING_LOWER_TO_FLOOR:

        case LT_SR_FLOOR_RAISE_BY_24_CHANGES_TEXTURE:
        case LT_S1_FLOOR_RAISE_BY_24_CHANGES_TEXTURE:
        case LT_SR_FLOOR_RAISE_BY_32_CHANGES_TEXTURE:
        case LT_S1_FLOOR_RAISE_BY_32_CHANGES_TEXTURE:
        case LT_SR_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_CHANGES_TEXTURE:
        case LT_S1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_CHANGES_TEXTURE:
        case LT_SR_LIFT_LOWER_WAIT_RAISE_FAST:
        case LT_S1_LIFT_LOWER_WAIT_RAISE_FAST:
        case LT_G1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_CHANGES_TEXTURE:
        case LT_SR_LIFT_LOWER_WAIT_RAISE:
        case LT_S1_LIFT_LOWER_WAIT_RAISE:
            return ARROW_LIFT_SR;

        case LT_WR_FLOOR_RAISE_TO_8_BELLOW_LOWEST_CEILING_CRUSHES:
        case LT_W1_FLOOR_RAISE_TO_8_BELLOW_LOWEST_CEILING_CRUSHES:
        case LT_WR_FLOOR_LOWER_TO_HIGHEST_FLOOR:
        case LT_W1_FLOOR_LOWER_TO_HIGHEST_FLOOR:
        case LT_WR_FLOOR_LOWER_TO_8_ABOVE_HIGHEST_FLOOR:
        case LT_W1_FLOOR_LOWER_BY_8_ABOVE_HIGHEST_FLOOR:
        case LT_WR_FLOOR_RAISE_BY_24:
        case LT_W1_FLOOR_RAISE_BY_24:
        case LT_WR_FLOOR_RAISE_BY_24_CHANGES_TEXTURE:
        case LT_W1_FLOOR_RAISE_BY_24_CHANGES_TEXTURE:
        case LT_WR_FLOOR_RAISE_BY_SHORTEST_LOWER_TEXTURE:
        case LT_W1_FLOOR_RAISE_BY_SHORTEST_LOWER_TEXTURE:
        case LT_WR_FLOOR_LOWER_TO_LOWEST_FLOOR:
        case LT_W1_FLOOR_LOWER_TO_LOWEST_FLOOR:
        case LT_WR_FLOOR_LOWER_TO_LOWEST_FLOOR_CHANGES_TEXTURE:
        case LT_W1_FLOOR_LOWER_BY_LOWEST_FLOOR_CHANGES_TEXTURE:
        case LT_WR_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR:
        case LT_W1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR:
        case LT_WR_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_FAST:
        case LT_W1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_FAST:
        case LT_WR_FLOOR_RAISE_TO_LOWEST_CEILING:
        case LT_W1_FLOOR_RAISE_TO_LOWEST_CEILING:
        case LT_W1_CEILING_RAISE_TO_HIGHEST_CEILING:
        case LT_WR_CEILING_LOWER_TO_8_ABOVE_FLOOR:
        case LT_W1_CEILING_LOWER_TO_8_ABOVE_FLOOR:

        case LT_WR_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_CHANGES_TEXTURE:
        case LT_W1_FLOOR_RAISE_TO_NEXT_HIGHER_FLOOR_CHANGES_TEXTURE:
        case LT_WR_FLOOR_START_MOVING_UP_AND_DOWN:
        case LT_W1_FLOOR_START_MOVING_UP_AND_DOWN:
        case LT_WR_FLOOR_STOP_MOVING:
        case LT_W1_FLOOR_STOP_MOVING:
        case LT_WR_LIFT_LOWER_WAIT_RAISE_ALSO_MONSTERS:
        case LT_W1_LIFT_LOWER_WAIT_RAISE:
        case LT_WR_LIFT_LOWER_WAIT_RAISE_FAST:
        case LT_W1_LIFT_LOWER_WAIT_RAISE_FAST:
            return ARROW_LIFT_WR;

        // Crusher types
        case LT_S1_CEILING_LOWER_TO_8_ABOVE_FLOOR_PERPETUAL_SLOW_CHRUSHER_DAMAGE:
        case LT_WR_CRUSHER_START_WITH_SLOW_DAMAGE:
        case LT_W1_CRUSHER_START_WITH_SLOW_DAMAGE:
        case LT_WR_CRUSHER_START_WITH_FAST_DAMAGE:
        case LT_W1_CRUSHER_START_WITH_FAST_DAMAGE:
        case LT_W1_CRUSHER_START_WITH_SLOW_DAMAGE_SILENT:
        case LT_WR_CRUSHER_STOP:
        case LT_W1_CRUSHER_STOP:
            return ARROW_CRUSHER;

        // Stairs types
        case LT_S1_STAIRS_RAISE_BY_8:
        case LT_S1_STAIRS_RAISE_BY_16_FAST:
        case LT_W1_STAIRS_RAISE_BY_8:
        case LT_W1_STAIRS_RAISE_BY_16_FAST:
            return ARROW_STAIR;

        // Teleporter types
        case LT_WR_TELEPORT_ALSO_MONSTERS:
        case LT_W1_TELEPORT_ALSO_MONSTERS:
        case LT_WR_TELEPORT_MONSTERS_ONLY:
        case LT_W1_TELEPORT_MONSTERS_ONLY:
            return ARROW_TELEPORT;
    }

    return ARROW_OTHER;
}

bool init_maps(game_t& game)
{
    std::vector<game_wad_t> wad_list;

    try
    {
        // Load IWAD first, then any required PWADs in order
        wad_list.push_back(game.iwad_name);
        for (std::string &pwad : game.required_wads)
        {
            // Do not attempt to load non-WADs! (e.g. STRAIN.DEH)
            if (onut::toLower(pwad.substr(pwad.size() - 4)) == ".wad")
                wad_list.push_back(pwad);
        }
    }
    catch (const std::runtime_error& e)
    {
        for (game_wad_t &wad : wad_list)
        {
            if (wad.handle)
                fclose(wad.handle);
        }
        return false;
    }

    if (game.json_rename_lumps.isObject())
    {
        const auto &rename_lumps_wad_list = game.json_rename_lumps.getMemberNames();
        for (const std::string& remap_wad_name : rename_lumps_wad_list)
        {
            Json::Value& json_remap_table = game.json_rename_lumps[remap_wad_name];
            std::vector<remap_entry_t> remap_table;

            for (game_wad_t &wad : wad_list)
            {
                if (wad.filename != remap_wad_name)
                    continue;

                const auto &remap_table_list = json_remap_table.getMemberNames();
                for (const std::string& remap_key : remap_table_list)
                {
                    std::string remap_value = json_remap_table[remap_key].asString();
                    remap_table.push_back({remap_key.c_str(), remap_value.c_str()});
                }
                wad.rename_lumps(remap_table);
                break;
            }            
        }

    }

    for (auto& episode : game.episodes)
    {
        for (auto& level : episode)
        {
            map_t *map = &level.map;
            const std::vector<map_directory_t> *d_ptr;
            FILE *f;

            int dir_ent_num = -1;
            for (game_wad_t &wad : wad_list)
            {
                if (wad.filename == level.wad_name)
                {
                    d_ptr = &wad.directory;
                    f = wad.handle;
                    dir_ent_num = find_lump_in_directory(wad.directory, level.lump_name.c_str());
                    break;
                }
            }

            if (dir_ent_num < 0)
                continue;
            const std::vector<map_directory_t> &directory = *d_ptr;

            int i = dir_ent_num + 1;
            int len = directory.size();

            for (; i < len; ++i)
            {
                const auto &dir_entry = directory[i];
                try_load_lump("THINGS", f, dir_entry, map->things);
                try_load_lump("LINEDEFS", f, dir_entry, map->linedefs);
                try_load_lump("SIDEDEFS", f, dir_entry, map->sidedefs);
                try_load_lump("VERTEXES", f, dir_entry, map->vertexes);
                try_load_lump("SECTORS", f, dir_entry, map->map_sectors);
                try_load_lump("SSECTORS", f, dir_entry, map->map_subsectors);
                try_load_lump("NODES", f, dir_entry, map->map_nodes);
                try_load_lump("SEGS", f, dir_entry, map->map_segs);
                if (strncmp(dir_entry.name, "BLOCKMAP", 8) == 0)
                {
                    break;
                }
            }

            // Adjust things based on map tweaks. Only things matter enough to do this for
            const Json::Value &tweaks = game.json_map_tweaks.get(level.lump_name, {});
            const auto& tweak_thing_ids = tweaks.get("things", {}).getMemberNames();
            for (const auto& tweak_id : tweak_thing_ids)
            {
                map_thing_t &mt = map->things[std::stoi(tweak_id)];
                const Json::Value tweak = tweaks["things"][tweak_id];

                mt.x = tweak.get("x", mt.x).asInt();
                mt.y = tweak.get("y", mt.y).asInt();
                mt.type = tweak.get("type", mt.type).asInt();
                mt.direction = tweak.get("angle", mt.direction).asInt();
                mt.flags = tweak.get("flags", mt.flags).asInt();

                // We interpret the "dont_randomize" flag differently here (just as another MP only flag)
                // from how we do in the actual game (completely skip the item spawning code)
                if (tweak.get("dont_randomize", false).asBool())
                    mt.flags |= THING_FLAG_MP_ONLY;
            }

            map->sectors.resize(map->map_sectors.size());
            map->subsectors.resize(map->map_subsectors.size());
            map->nodes.resize(map->map_nodes.size());
            for (int j = 0, lenj = (int)map->map_nodes.size(); j < lenj; ++j)
            {
                map->nodes[j].x = (int16_t)map->map_nodes[j].x << 16;
                map->nodes[j].y = (int16_t)map->map_nodes[j].y << 16;
                map->nodes[j].dx = (int16_t)map->map_nodes[j].dx << 16;
                map->nodes[j].dy = (int16_t)map->map_nodes[j].dy << 16;
                for (int jj = 0; jj < 2; ++jj)
                {
                    map->nodes[j].children[jj] = (uint16_t)(int16_t)map->map_nodes[j].children[jj];
                    if (map->nodes[j].children[jj] == NO_INDEX)
                        map->nodes[j].children[jj] = -1;
                    else if (map->nodes[j].children[jj] & NF_SUBSECTOR_VANILLA)
                    {
                        map->nodes[j].children[jj] &= ~NF_SUBSECTOR_VANILLA;
                        if (map->nodes[j].children[jj] >= (int)map->map_subsectors.size())
                            map->nodes[j].children[jj] = 0;
                        map->nodes[j].children[jj] |= NF_SUBSECTOR;
                    }
                    for (int k = 0; k < 4; ++k)
                        map->nodes[j].bbox[jj][k] = (int16_t)map->map_nodes[j].bbox[jj][k] << 16;
                }
            }

            map->segs.resize(map->map_segs.size());
            for (int j = 0, lenj = (int)map->map_segs.size(); j < lenj; ++j)
            {
                const auto& map_seg = map->map_segs[j];
                auto& seg = map->segs[j];
                int side = map_seg.side;
                seg.sidedef = (&(map->linedefs[map_seg.linedef].front_sidedef))[side];
                seg.front_sector = map->sidedefs[seg.sidedef].sector;
            }

            // Assign sector to subsector
            for (int j = 0, lenj = (int)map->map_subsectors.size(); j < lenj; ++j)
            {
                const auto& seg = map->segs[map->map_subsectors[j].firstseg];
                //const auto& map_sidedef = map->sidedefs[seg.sidedef];
                map->subsectors[j].sector = seg.front_sector;
            }

            map->bb[0] = map->vertexes[0].x;
            map->bb[1] = map->vertexes[0].y;
            map->bb[2] = map->vertexes[0].x;
            map->bb[3] = map->vertexes[0].y;
            for (int v = 1, vlen = (int)map->vertexes.size(); v < vlen; ++v)
            {
                map->bb[0] = std::min(map->bb[0], map->vertexes[v].x);
                map->bb[1] = std::min(map->bb[1], map->vertexes[v].y);
                map->bb[2] = std::max(map->bb[2], map->vertexes[v].x);
                map->bb[3] = std::max(map->bb[3], map->vertexes[v].y);
            }

            // Triangulate
            triangulate_map(map);

            // Create arrows
            for (int j = 0; j < (int)map->linedefs.size(); ++j)
            {
                const auto& line_def = map->linedefs[j];
                if (line_def.special_type != 0 && line_def.sector_tag != 0)
                {
                    arrow_t arrow;
                    arrow.type = get_arrow_type(line_def.special_type);
                    arrow.color = get_color_for_arrow_type(arrow.type);
                    const auto& v1 = map->vertexes[line_def.start_vertex];
                    const auto& v2 = map->vertexes[line_def.end_vertex];
                    arrow.from = {
                        (float)(v1.x + v2.x) * 0.5f,
                        -(float)(v1.y + v2.y) * 0.5f
                    };
                    for (int k = 0; k < (int)map->map_sectors.size(); ++k)
                    {
                        const auto& map_sector = map->map_sectors[k];
                        if (map_sector.tag == line_def.sector_tag)
                        {
                            Vector2 bbmin, bbmax;
                            const auto& sector = map->sectors[k];
                            if (sector.triangle_vertices.empty()) continue;
                            bbmin = sector.triangle_vertices[0];
                            bbmax = bbmin;
                            for (int l = 1; l < (int)sector.triangle_vertices.size(); ++l)
                            {
                                Vector2 pt = sector.triangle_vertices[l];
                                bbmin = onut::min(bbmin, pt);
                                bbmax = onut::max(bbmax, pt);
                            }
                            arrow.to = (bbmin + bbmax) * 0.5f;
                            map->arrows.push_back(arrow);
                        }
                    }
                }
            }

            // Count checks
            map->check_count = 0;
            for (int j = 0, len = (int)map->things.size(); j < len; ++j)
            {
                const auto& thing = map->things[j];

                // Count total thing count (Consider UV difficulty)
                if (thing.flags & THING_FLAG_HARD)
                    game.total_doom_types[thing.type]++;

                if (thing.flags & THING_FLAG_MP_ONLY) continue; // Thing is not in single player
                auto it = game.location_doom_types.find(thing.type);
                if (it == game.location_doom_types.end()) continue;
                map->check_count++;
            }
        }
    }

    // Load palette
    auto pal = load_lump(wad_list, "PLAYPAL");

    // Load sprites for item requirements
    for (auto& item_requirement : game.item_requirements)
    {
        if (item_requirement.sprite != "")
        {
            item_requirement.icon = load_sprite(wad_list, item_requirement.sprite.c_str(), pal.data());
        }
    }

    // close file
    for (game_wad_t &wad : wad_list)
    {
        if (wad.handle)
            fclose(wad.handle);
    }
    return true;
}


int sector_at(int x, int y, map_t* map)
{
    x = (int)((int16_t)x << 16);
    y = (int)((int16_t)y << 16);

    auto subsector = point_in_subsector(x, y, map);
    return subsector->sector;
}
