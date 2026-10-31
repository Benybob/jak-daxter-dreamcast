/* Instance records resident by XZ cell, in a fixed buffer (AI-assisted), ISC. */
#ifndef DC_INSTANCE_CELLS_H
#define DC_INSTANCE_CELLS_H
#include "instance_pass.h"

enum {DC_INSTANCE_CELLS_MAX=256};
typedef struct {int32_t ix,iz;uint32_t count;} dc_instance_cell;
typedef struct {
 dc_instance *records;
 uint32_t capacity,count,cell_count;
 double cell;
 char root[192];
 dc_instance_cell cells[DC_INSTANCE_CELLS_MAX];
} dc_instance_cells;
/* Zeroed/unowned output. capacity records, allocated once. root holds
 * cells/IX_IZ.dci, written by instance_cells.py: 52-byte records whose pad is the
 * original instance index, ascending. A cell whose file does not exist is empty;
 * any other open failure is an error, and the cell is tried again next time. */
const char *dc_instance_cells_make(dc_instance_cells *,const char *root,double cell,uint32_t capacity);
void dc_instance_cells_free(dc_instance_cells *);
/* Hold exactly the cells a disc of radius draw (plus a small margin, for the
 * renderer's rounding) around (x, z) touches, in original record order, and point
 * pkg at them. Cells that left go first, so capacity need
 * only hold one position's cells. Failure keeps pkg on a consistent subset: what
 * was resident less the cells that left, with no partly read cell. */
const char *dc_instance_cells_update(dc_instance_cells *,dc_instance_package *pkg,double x,double z,double draw);
#endif
