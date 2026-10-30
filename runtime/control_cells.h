// Named game state cells for the control endpoint: values the game keeps in memory (the camera, later the weather, ...)
// that a command reads. Heap addresses move between runs, object
// layouts do not, so a cell is not an address but a recipe: a root object, one or more pointer chains found once with a memory search,
// and a check that tells a chain that lands on the right value from one that lands on a
// stale slot. Add a cell: write its entry in control_cells.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { CELL_F32, CELL_U32, CELL_S32 } CellType;
#define CELL_DEPTH 8

typedef struct {
    int len;                   // how many offsets
    uint32_t off[CELL_DEPTH];  // read the pointer at root+off[0], then at that+off[1], ...; the last offset is added, not read
} CellChain;

typedef struct {
    const char *name;          // "player.pos"
    const char *what;          // one line for `cell list`
    size_t root;               // offsetof(GameSites, ...): the game site that holds a pointer to the root object
    CellType type;
    int count;                 // elements of that type in a row (3: a vec3), at most 4
    const CellChain *chains;   // tried in order; the first one whose value passes `check` wins
    int nchains;
    bool (*check)(const void *value);  // does this value look right (compared to a reading we trust)? NULL: any pointer chain that ends in mapped memory
} ControlCell;

// Where the cell is right now: its address, or 0 when no chain resolves (the object does not exist yet, or another game version)
uint64_t control_cell_addr(const ControlCell *c);
// Read the cell (count elements as doubles); false when it does not resolve or the memory is not accessible
bool control_cell_get(const ControlCell *c, double *v);

// The cells themselves
extern const ControlCell control_cell_camera_eye, control_cell_camera_target, control_cell_camera_pitch;  // the orbit camera; yaw is the direction eye -> target

// Read the stored orbit camera, in any AA mode. Yaw 0 looks along +z, 90 along +x; pitch positive looks up.
bool control_cell_camera_angles(double eye[3], double target[3], double *yaw, double *pitch);
