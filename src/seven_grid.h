// Seven-String Guitar (seven.h): what the grid part shares with the others.
//
// The FretGrid's own layout for eight strings (the edits of seven_string make every FretGrid so, offsets.map names
// it): 26 cell rows at seven_fretgrid_cells, seven_fretgrid_row bytes apart, each a valid byte and then 8 cells of
// seven_fretgrid_cell bytes (a valid byte; three local points at seven_fretgrid_cell_local, three world points at
// seven_fretgrid_cell_world, 12 bytes each); the fret-wire rows at seven_fretgrid_wires, seven_fretgrid_wire_row bytes
// apart; seven_fretgrid_size bytes in all. The next-note grid's time rows: seven_nextgrid_time.
#ifndef RS_SEVEN_GRID_H
#define RS_SEVEN_GRID_H

// Cell `string` (0-7) of the fret row `row` (0-25) of a FretGrid (a host pointer to the game's grid): the cell's
// first byte is its valid flag. NULL for a row or string out of range.
void *seven_grid_cell(void *grid, unsigned row, unsigned string);

// The local points of the cells of strings 6 and 7 in the first `frets` rows (at most 26), from those of strings 0
// and 1 one and two string spacings further out: cell 6 = 2 * cell 0 - cell 1, cell 7 = 3 * cell 0 - 2 * cell 1, each
// valid when cells 0 and 1 are (else both are made invalid). The world points are the game's (local + the guitar's
// offset), from its next ApplyOffset. Returns the rows filled. rs7string's FillExtraCells, which its Rnd_FretGrid_Build
// exit (PUBGB) calls for a guitar built with 7 or 8 strings once the strings are loaded.
int seven_grid_fill_extra_cells(void *grid, unsigned frets);

// 1 when the game has a render-world getter (render_world); else the parts find an entity's root through
// entity_model_root
int seven_has_render_world(void);

#endif
