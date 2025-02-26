// 3D World - Building Interior Room Geometry Drawing
// by Frank Gennari 7/30/2020

#include "3DWorld.h"
#include "function_registry.h"
#include "buildings.h"
#include "scenery.h" // for s_plant
#include "shaders.h"

using std::swap;

bool const ADD_BOOK_COVERS = 1; // cover pictures
bool const ADD_BOOK_TITLES = 1;
colorRGBA const STAIRS_COLOR_TOP(0.7, 0.7, 0.7);
colorRGBA const STAIRS_COLOR_BOT(0.9, 0.9, 0.9);

vect_cube_t temp_cubes;
vect_room_object_t temp_objects;
vect_cube_t &get_temp_cubes() {temp_cubes.clear(); return temp_cubes;}
vect_room_object_t &get_temp_objects() {temp_objects.clear(); return temp_objects;}

extern int display_mode, player_in_closet;

int get_rand_screenshot_texture(unsigned rand_ix);
unsigned get_num_screenshot_tids();
string gen_random_full_name (rand_gen_t &rgen);

void gen_text_verts(vector<vert_tc_t> &verts, point const &pos, string const &text, float tsize, vector3d const &column_dir, vector3d const &line_dir, bool use_quads=0);
string const &gen_book_title(unsigned rand_id, string *author, unsigned split_len);
void add_floor_number(unsigned floor_ix, unsigned floor_offset, bool has_parking_garage, ostringstream &oss);
unsigned get_rgeom_sphere_ndiv(bool low_detail);

unsigned get_face_mask(unsigned dim, bool dir) {return ~(1 << (2*(2-dim) + dir));} // draw only these faces: 1=Z1, 2=Z2, 4=Y1, 8=Y2, 16=X1, 32=X2
unsigned get_skip_mask_for_xy(bool dim) {return (dim ? EF_Y12 : EF_X12);} // skip these faces
tid_nm_pair_t get_tex_auto_nm(int tid, float tscale=1.0, bool shadowed=1) {return tid_nm_pair_t(tid, get_normal_map_for_bldg_tid(tid), tscale, tscale, 0.0, 0.0, shadowed);}
int get_counter_tid    () {return get_texture_by_name("marble2.jpg");}
int get_paneling_nm_tid() {return get_texture_by_name("normal_maps/paneling_NRM.jpg", 1);}
int get_blinds_tid     () {return get_texture_by_name("interiors/blinds.jpg", 0, 0, 1, 8.0);} // use high aniso
int get_money_tid      () {return get_texture_by_name("interiors/dollar20.jpg");}

int get_crack_tid(room_object_t const &obj, bool alpha=0) {
	return get_texture_by_name(((5*obj.obj_id + 7*obj.room_id) & 1) ?
		(alpha ? "interiors/cracked_glass2_alpha.jpg" : "interiors/cracked_glass2.jpg") :
		(alpha ? "interiors/cracked_glass_alpha.jpg" : "interiors/cracked_glass.jpg"), 0, 0, 1, 0.0, 1, 1, (alpha ? 4 : 3));
}
int get_box_tid() {return get_texture_by_name("interiors/box.jpg");}
int get_crate_tid(room_object_t const &c) {return get_texture_by_name((c.obj_id & 1) ? "interiors/crate2.jpg" : "interiors/crate.jpg");}
int get_plywood_tid   () {return get_texture_by_name("interiors/plywood.jpg");}
int get_insulation_tid() {return get_texture_by_name("interiors/insulation.jpg");}
int get_cube_duct_tid () {return get_texture_by_name("interiors/duct.jpg");}
int get_cylin_duct_tid() {return get_texture_by_name("buildings/metal_roof.jpg");} // metal roof is close enough

colorRGBA get_textured_wood_color() {return WOOD_COLOR.modulate_with(texture_color(WOOD2_TEX));} // Note: uses default WOOD_COLOR, not the per-building random variant
colorRGBA get_counter_color      () {return (get_textured_wood_color()*0.75 + texture_color(get_counter_tid())*0.25);}

bool is_known_metal_color(colorRGBA const &c) {return (c == COPPER_C || c == BRASS_C || c == BRONZE_C || c == GOLD);}

rgeom_mat_t &building_room_geom_t::get_wood_material(float tscale, bool inc_shadows, bool dynamic, unsigned small) {
	return get_material(tid_nm_pair_t(WOOD2_TEX, get_texture_by_name("normal_maps/wood_NRM.jpg", 1),
		3.0*tscale, 3.0*tscale, 0.0, 0.0, inc_shadows), inc_shadows, dynamic, small); // hard-coded for common material
}

void get_tc_leg_cubes_abs_width(cube_t const &c, float leg_width, cube_t cubes[4]) {
	for (unsigned y = 0; y < 2; ++y) {
		for (unsigned x = 0; x < 2; ++x) {
			cube_t leg(c);
			leg.d[0][x] += (x ? -1.0f : 1.0f)*(c.dx() - leg_width);
			leg.d[1][y] += (y ? -1.0f : 1.0f)*(c.dy() - leg_width);
			cubes[2*y+x] = leg;
		}
	}
}
void get_tc_leg_cubes(cube_t const &c, float width, cube_t cubes[4]) {
	get_tc_leg_cubes_abs_width(c, get_tc_leg_width(c, width), cubes);
}
void building_room_geom_t::add_tc_legs(cube_t const &c, colorRGBA const &color, float width, float tscale, bool use_metal_mat, bool draw_tops, float frame_height) {
	rgeom_mat_t &mat(use_metal_mat ? get_metal_material(1) : get_wood_material(tscale)); // shadowed=1, dynamic=0, small=0
	cube_t cubes[4];
	get_tc_leg_cubes(c, width, cubes);
	point const llc(c.get_llc());
	for (unsigned i = 0; i < 4; ++i) {mat.add_cube_to_verts(cubes[i], color, llc, (draw_tops ? EF_Z1 : EF_Z12));} // skip top and bottom faces

	if (frame_height > 0.0) {
		float const leg_width(get_tc_leg_width(c, width));

		for (unsigned dim = 0; dim < 2; ++dim) {
			for (unsigned dir = 0; dir < 2; ++dir) {
				cube_t frame(c);
				frame.z1() = c.z2() - frame_height;
				frame.expand_in_dim(!dim, -leg_width); // inside the legs
				frame.d[dim][!dir] = frame.d[dim][dir] + (dir ? -1.0 : 1.0)*leg_width; // shrink to leg width
				mat.add_cube_to_verts(frame, color, llc, ((draw_tops ? 0 : EF_Z2) | get_skip_mask_for_xy(!dim)));
			} // for dir
		} // for dim
	}
}

colorRGBA apply_light_color(room_object_t const &o, colorRGBA const &c) {
	if (enable_building_indir_lighting()) return c; // disable this when using indir lighting
	return c * (0.5f + 0.5f*min(sqrt(o.light_amt), 1.5f)); // use c.light_amt as an approximation for ambient lighting due to sun/moon
}
colorRGBA building_room_geom_t::apply_wood_light_color(room_object_t const &o) const {return apply_light_color(o, wood_color);}
colorRGBA apply_light_color(room_object_t const &o) {return apply_light_color(o, o.color);} // use object color

// actually applies to tables, desks, dressers, and nightstands
void get_table_cubes(room_object_t const &c, cube_t cubes[5]) {
	assert(c.shape != SHAPE_CYLIN); // can't call this on cylindrical table
	bool const is_desk(c.type == TYPE_DESK), is_dns(c.type == TYPE_DRESSER || c.type == TYPE_NIGHTSTAND);
	cube_t top(c), legs_bcube(c);
	// Note: default table with top_dz=0.12, leg_width=0.08; desk is 0.15/0.06; dresser is 0.88/0.10
	top.z1() += (is_desk ? 0.85 : (is_dns ? 0.12 : 0.88))*c.dz();
	legs_bcube.z2() = top.z1();
	cubes[0] = top;
	get_tc_leg_cubes(legs_bcube, (is_desk ? 0.06 : (is_dns ? 0.10 : 0.08)), (cubes+1));
}

colorRGBA const table_glass_color(0.7, 1.0, 0.85, 0.25); // greenish tint, semi transparent

colorRGBA get_table_color(room_object_t const &c) {
	if (c.shape == SHAPE_CYLIN) { // round table
		bool const marble(c.obj_id & 1);
		return (marble ? texture_color(MARBLE_TEX) : get_textured_wood_color()); // ignore the black legs of marble tables
	}
	else { // rectangular or short table
		bool const glass((c.flags & RO_FLAG_IS_HOUSE) && (c.obj_id & 1));
		return (glass ? table_glass_color : get_textured_wood_color()); // ignore the black legs of glass tables
	}
}
void building_room_geom_t::add_table(room_object_t const &c, float tscale, float top_dz, float leg_width) { // 6 quads for top + 4 quads per leg = 22 quads = 88 verts
	cube_t top(c), legs_bcube(c);

	if (c.shape == SHAPE_CYLIN) { // round table
		bool const marble(c.obj_id & 1); // 50% marble top with metal base; else wood
		vector3d const size(c.get_size());
		top.z1()       += (1.0 - top_dz)*c.dz();
		legs_bcube.z2() = top.z1();
		legs_bcube.expand_by_xy(-0.46*size);
		colorRGBA const top_color(marble ? apply_light_color(c, LT_GRAY) : apply_wood_light_color(c)), base_color(marble ? BLACK : top_color);
		rgeom_mat_t &top_mat(marble ? get_material(tid_nm_pair_t(MARBLE_TEX/*get_counter_tid()*/, 1.2*tscale), 1) : get_wood_material(tscale));
		top_mat.add_vcylin_to_verts(top, top_color, 1, 1, 0, 0, 1.0, 1.0, 16.0, 2.0, 0, 64); // draw top and bottom with scaled side texture coords; ndiv=64
		rgeom_mat_t &base_mat(marble ? get_metal_material(1) : get_wood_material(tscale)); // shadowed=1, dynamic=0, small=0
		base_mat.add_vcylin_to_verts(legs_bcube, base_color, 1, 1, 0, 0, 1.0, 1.0, 1.0); // support
		cube_t feet(c);
		feet.z2() = c.z1() + 0.1*c.dz();
		feet.expand_by_xy(-0.2*size);

		for (unsigned d = 0; d < 2; ++d) { // add crossed feet
			cube_t foot(feet);
			foot.expand_in_dim(d, -0.27*size[d]);
			base_mat.add_cube_to_verts(foot, base_color, c.get_llc(), EF_Z1); // skip bottom surface
		}
	}
	else { // rectangular or short table
		assert(c.shape == SHAPE_CUBE || c.shape == SHAPE_SHORT);
		// Note: glass table top and legs won't quite match the geometry used for collision detection and queries, but it's probably close enough
		bool const glass(c.is_glass_table()); // 50% glass top with metal base; only in houses; not dressers/desks
		top.z1()       += (1.0 - (glass ? 0.25 : 1.0)*top_dz)*c.dz(); // glass tables have a thinner top
		legs_bcube.z2() = top.z1();

		if (glass) {
			legs_bcube.expand_by_xy(-0.05*min(c.dx(), c.dy())); // inset the legs
			colorRGBA const top_color(apply_light_color(c, table_glass_color));
			rgeom_mat_t &mat(get_untextured_material(0, 0, 0, 1)); // no shadows + transparent
			mat.add_cube_to_verts(top, top_color, c.get_llc()); // all faces drawn
			add_tc_legs(legs_bcube, BLACK, 0.5*leg_width, tscale, glass, glass, 1.0*top.dz()); // use_metal_mat=1, draw_tops=1, frame_height=nonzero
		}
		else { // wood
			colorRGBA const color(apply_wood_light_color(c));
			rgeom_mat_t &mat(get_wood_material(tscale));
			mat.add_cube_to_verts(top, color, c.get_llc()); // all faces drawn
			add_tc_legs(legs_bcube, color, leg_width, tscale);
		}
	}
}

void get_chair_cubes(room_object_t const &c, cube_t cubes[3]) {
	float const height(c.dz()*((c.shape == SHAPE_SHORT) ? 1.333 : 1.0)); // effective height if the chair wasn't short
	cube_t seat(c), back(c), legs_bcube(c);
	seat.z1() += 0.32*height;
	seat.z2()  = back.z1() = seat.z1() + 0.07*height;
	legs_bcube.z2() = seat.z1();
	back.d[c.dim][c.dir] += 0.88f*(c.dir ? -1.0f : 1.0f)*c.get_depth();
	cubes[0] = seat; cubes[1] = back; cubes[2] = legs_bcube;
}
void building_room_geom_t::add_chair(room_object_t const &c, float tscale) { // 6 quads for seat + 5 quads for back + 4 quads per leg = 27 quads = 108 verts
	cube_t cubes[3]; // seat, back, legs_bcube
	get_chair_cubes(c, cubes);
	get_material(tid_nm_pair_t(MARBLE_TEX, 1.2*tscale), 1).add_cube_to_verts(cubes[0], apply_light_color(c), c.get_llc()); // seat; all faces drawn
	colorRGBA const color(apply_wood_light_color(c));
	get_wood_material(tscale).add_cube_to_verts(cubes[1], color, c.get_llc(), EF_Z1); // back; skip bottom face
	add_tc_legs(cubes[2], color, 0.15, tscale); // legs
}

room_object_t get_dresser_middle(room_object_t const &c) {
	float const shrink_val(min(0.25*min(c.dx(), c.dy()), 0.5*get_tc_leg_width(c, 0.10))); // shrink by half leg width, clamped to ensure strictly normalized
	room_object_t middle(c);
	middle.z1() += 0.14*c.dz(); // bottom of drawers section
	middle.z2() -= 0.06*c.dz(); // at bottom of top surface
	middle.expand_by_xy(-shrink_val);
	assert(middle.is_strictly_normalized());
	return middle;
}
void building_room_geom_t::add_dresser(room_object_t const &c, float tscale, bool inc_lg, bool inc_sm) { // or nightstand
	if (inc_lg) {
		assert(c.shape == SHAPE_CUBE); // cubes only for now
		add_table(c, tscale, 0.06, 0.10); // Note: legs extend across middle to the top surface
		get_wood_material(tscale).add_cube_to_verts(get_dresser_middle(c), apply_wood_light_color(c), c.get_llc()); // all faces drawn
	}
	if (inc_sm) { // add drawers
		room_object_t middle(get_dresser_middle(c));
		middle.expand_in_dim(!c.dim, -0.5*get_tc_leg_width(c, 0.10));
		add_dresser_drawers(middle, tscale);
	}
}

float get_drawer_cubes(room_object_t const &c, vect_cube_t &drawers, bool front_only, bool inside_only) {
	assert(!(front_only && inside_only)); // these options only apply to open drawers
	assert(c.is_strictly_normalized());
	drawers.clear();
	rand_gen_t rgen;
	c.set_rand_gen_state(rgen);
	rgen.rand_mix();
	float const width(c.get_width()), depth(c.get_depth()), height(c.dz());
	bool is_lg(width > 2.0*height);
	unsigned const num_rows(2 + (rgen.rand() & 1)); // 2-3
	float const row_spacing(height/num_rows), drawer_thick(min(0.05f*height, 0.25f*depth)), border(0.1*row_spacing), dir_sign(c.dir ? 1.0 : -1.0);
	float const sd_thick(dir_sign*drawer_thick), drawer_extend(((c.type == TYPE_DESK) ? 0.5 : 0.8)*dir_sign*depth);
	cube_t d_row(c);
	d_row.d[c.dim][!c.dir]  = c.d[c.dim][c.dir];
	d_row.d[c.dim][ c.dir] += sd_thick; // expand out a bit
	unsigned num_cols(1); // 1 for nightstand
	float vpos(c.z1());

	for (unsigned n = 0; n < num_rows; ++n) { // at most 12 drawers
		if (is_lg && (num_cols == 1 || rgen.rand_bool())) {num_cols = 2 + (rgen.rand() % 3);} // 2-4, 50% of the time keep same as prev row
		float const col_spacing(width/num_cols);
		float hpos(c.d[!c.dim][0]);
		set_cube_zvals(d_row, (vpos + border), (vpos + row_spacing - border));

		for (unsigned m = 0; m < num_cols; ++m) {
			cube_t drawer(d_row); // front part of the drawer
			drawer.d[!c.dim][0] = hpos + border;
			drawer.d[!c.dim][1] = hpos + col_spacing - border;

			if (c.drawer_flags & (1 << drawers.size())) { // make a drawer open
				drawer.d[c.dim][c.dir] += drawer_extend;
				if (front_only) {drawer.d[c.dim][!c.dir] += drawer_extend;} // translate the other side as well
				else if (inside_only) { // adjust to return interior part of the door for interaction
					drawer.d[c.dim][ c.dir] -= sd_thick; // flush with object
					drawer.d[c.dim][!c.dir] += 0.25f*sd_thick;
					drawer.expand_in_dim(!c.dim, -0.08*drawer.get_sz_dim(!c.dim)); // subtract off width of sides
					drawer.z1() += 0.2*drawer.dz();
				}
			}
			assert(drawer.is_strictly_normalized());
			drawers.push_back(drawer);
			hpos += col_spacing;
		} // for m
		vpos += row_spacing;
	} // for n
	return drawer_extend; // signed
}

void building_room_geom_t::add_dresser_drawers(room_object_t const &c, float tscale) { // or nightstand
	vect_cube_t &drawers(get_temp_cubes());
	get_drawer_cubes(c, drawers, 1, 0); // front_only=1, inside_only=0
	assert(drawers.size() <= 16); // we only have 16 bits to store drawer flags
	float const height(c.dz()), drawer_thick(0.05*height), handle_thick(0.75*drawer_thick), dir_sign(c.dir ? 1.0 : -1.0), handle_width(0.07*height);
	get_metal_material(0, 0, 1); // ensure material exists so that door_mat reference is not invalidated
	rgeom_mat_t &drawer_mat(get_wood_material(1.5*tscale, 1, 0, 1)); // shadowed, small=1
	rgeom_mat_t &handle_mat(get_metal_material(0, 0, 1)); // untextured, unshadowed, small=1
	colorRGBA const drawer_color(apply_light_color(c, WHITE)); // lighter color than dresser
	colorRGBA const handle_color(apply_light_color(c, GRAY_BLACK));
	unsigned const door_skip_faces(~get_face_mask(c.dim, !c.dir));
	vect_room_object_t &objects(get_temp_objects());
	point const tex_orig(c.get_llc()); // relative to the dresser so that textures don't slide when it's moved

	for (auto i = drawers.begin(); i != drawers.end(); ++i) {
		float const dwidth(i->get_sz_dim(!c.dim)), handle_shrink(0.5*dwidth - handle_width);
		unsigned door_skip_faces_mod(door_skip_faces);

		if (i->d[c.dim][!c.dir] != c.d[c.dim][c.dir]) { // drawer is not flush with front face, so it's open
			float const dheight(i->dz());
			cube_t drawer_body(*i);
			drawer_body.d[c.dim][!c.dir] = c. d[c.dim][ c.dir]; // flush with front
			drawer_body.d[c.dim][ c.dir] = i->d[c.dim][!c.dir]; // inside of drawer face
			drawer_body.expand_in_dim(!c.dim, -0.05*dwidth);
			drawer_body.expand_in_dim(2,      -0.05*dheight);
			cube_t bottom(drawer_body), left(drawer_body), right(drawer_body), back(drawer_body);
			left.z1() = right.z1() = bottom.z2() = drawer_body.z2() - 0.8*dheight;
			left.z2() = right.z2() = drawer_body.z2() - 0.1*dheight; // sides slightly shorter than the front and back
			left .d[!c.dim][1]    -= 0.87*dwidth; // set width of left  side
			right.d[!c.dim][0]    += 0.87*dwidth; // set width of right side
			back.d[c.dim][c.dir]   = c.d[c.dim][c.dir] + 0.25f*dir_sign*drawer_thick; // flush with front face and narrow
			unsigned const skip_mask_front_back(get_skip_mask_for_xy(c.dim));
			colorRGBA const blr_color(drawer_color*0.4 + apply_wood_light_color(c)*0.4); // halfway between base and drawer colors, but slightly darker
			// swap the texture orientation of drawers to make them stand out more
			drawer_mat.add_cube_to_verts(bottom, blr_color, tex_orig,  skip_mask_front_back, 1);
			drawer_mat.add_cube_to_verts(left,   blr_color, tex_orig, (skip_mask_front_back | EF_Z1), 1);
			drawer_mat.add_cube_to_verts(right,  blr_color, tex_orig, (skip_mask_front_back | EF_Z1), 1);
			// draw inside face of back of drawer;
			// normally this wouldn't be visible here, but it's easier to drawn than holes for the drawers and it doesn't look as bad as doing nothing;
			// it would be better to cut a hole into the front of the desk for the drawer to slide into, but that seems to be difficult
			drawer_mat.add_cube_to_verts(back, drawer_color, tex_orig, get_face_mask(c.dim,c.dir), 1);
			door_skip_faces_mod = 0; // need to draw interior face
			cube_t interior(drawer_body);
			interior.z1() = bottom.z2();
			interior.d[!c.dim][0] = left .d[!c.dim][1];
			interior.d[!c.dim][1] = right.d[!c.dim][0];
			interior.d[ c.dim][!c.dir] = back.d[c.dim][c.dir];
			room_object_t const obj(get_item_in_drawer(c, interior, (i - drawers.begin())));
			if (obj.type != TYPE_NONE) {objects.push_back(obj);}
		}
		drawer_mat.add_cube_to_verts(*i, drawer_color, tex_orig, door_skip_faces_mod, 1); // swap the texture orientation of drawers to make them stand out more
		// add door handle
		cube_t handle(*i);
		handle.d[c.dim][!c.dir]  = i->d[c.dim][c.dir];
		handle.d[c.dim][ c.dir] += dir_sign*handle_thick; // expand out a bit
		handle.d[!c.dim][0] = i->d[!c.dim][0] + handle_shrink;
		handle.d[!c.dim][1] = i->d[!c.dim][1] - handle_shrink;
		handle.z1() = i->z1() + 0.8*i->dz();
		handle.z2() = handle.z1() + 0.1*i->dz();
		handle_mat.add_cube_to_verts_untextured(handle, handle_color, door_skip_faces); // same skip_faces
	} // for i
	add_nested_objs_to_verts(objects); // add any objects that were found in open drawers; must be small static objects
}

void building_room_geom_t::draw_mirror_surface(room_object_t const &c, cube_t const &mirror, bool dim, bool dir, bool shadowed) {
	tid_nm_pair_t tp(REFLECTION_TEXTURE_ID, 0.0, shadowed);
	if (ENABLE_MIRROR_REFLECTIONS) {tp.emissive = 1.0;}
	unsigned const skip_faces(get_face_mask(dim, dir));
	get_material(tp, shadowed).add_cube_to_verts(mirror, WHITE, zero_vector, skip_faces, !dim); // draw only the front face; use dim/dir rather than from c

	if (c.is_broken()) {
		cube_t crack(mirror);
		crack.d[dim][dir] += (dir ? 1.0 : -1.0)*0.01*mirror.get_sz_dim(dim); // move out to prevent z-fighting
		rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_crack_tid(c, 1), 0.0, 0, true), 0, 0, 0, 1)); // alpha=1, unshadowed, transparent=1
		mat.add_cube_to_verts(crack, apply_light_color(c, WHITE), all_zeros, skip_faces, !dim, (c.obj_id&1), (c.obj_id&2)); // X/Y mirror based on obj_id
	}
}
void building_room_geom_t::add_dresser_mirror(room_object_t const &c, float tscale) {
	float const frame_thick(0.04*min(c.get_width(), c.get_height()));
	// draw the mirror
	cube_t mirror(c);
	mirror.d[c.dim][c.dir] -= (c.dir ? 1.0 : -1.0)*0.6*c.get_length();
	mirror.expand_in_dim(!c.dim, -frame_thick);
	mirror.expand_in_dim(2,      -frame_thick); // z
	draw_mirror_surface(c, mirror, c.dim, c.dir, 1); // shadowed=1

	// draw the wood frame
	rgeom_mat_t &frame_mat(get_wood_material(tscale));
	colorRGBA const frame_color(apply_wood_light_color(c));
	point const llc(c.get_llc());

	for (unsigned d = 0; d < 2; ++d) { // {bottom, top} and {left, right}
		cube_t tb(c), lr(c);
		tb.d[     2][!d] = mirror.d[     2][d];
		lr.d[!c.dim][!d] = mirror.d[!c.dim][d];
		frame_mat.add_cube_to_verts(tb, frame_color, llc, (d ? 0 : EF_Z1)); // top/bot; draw back face in case player pulls it from the wall
		frame_mat.add_cube_to_verts(lr, frame_color, llc, EF_Z12); // left/right
	}
	frame_mat.add_cube_to_verts(mirror, frame_color, llc, get_face_mask(c.dim, !c.dir), !c.dim); // draw back of mirror in case dresser is pushed away from the wall
}

tid_nm_pair_t get_scaled_wall_tex(tid_nm_pair_t const &wall_tex) {
	tid_nm_pair_t wall_tex_scaled(wall_tex);
	wall_tex_scaled.tscale_x *= 2.0;
	wall_tex_scaled.tscale_y *= 2.0;
	wall_tex_scaled.shadowed  = 1;
	return wall_tex_scaled;
}
float get_closet_wall_thickness(room_object_t const &c) {
	return (WALL_THICK_VAL*(1.0f - FLOOR_THICK_VAL_HOUSE)*c.dz()); // use c.dz() as floor_spacing
}

// cubes: front left, left side, front right, right side, door
void get_closet_cubes(room_object_t const &c, cube_t cubes[5], bool for_collision) {
	float const width(c.get_width()), depth(c.get_depth()), height(c.dz());
	bool const use_small_door(c.is_small_closet()), doors_fold(!use_small_door && c.is_hanging());
	// small closets: door does not collide when open; large closets: edges of door still collide even when open
	float const wall_width(use_small_door ? 0.5*(width - 0.5*height) : ((for_collision && c.is_open()) ? (doors_fold ? 0.2 : 0.25) : 0.05)*width);
	float const wall_thick(get_closet_wall_thickness(c)), wall_shift(width - wall_width);
	assert(wall_shift > 0.0);
	cube_t doors(c), walls[2] = {c, c}; // left, right
	walls[0].d[!c.dim][1] -= wall_shift;
	walls[1].d[!c.dim][0] += wall_shift;

	for (unsigned d = 0; d < 2; ++d) {
		cube_t front(walls[d]), side(walls[d]);
		front.d[c.dim][!c.dir] = side.d[c.dim][c.dir] = front.d[c.dim][c.dir] - (c.dir ? 1.0 : -1.0)*wall_thick; // set front wall/door thickness

		if (!(c.flags & (d ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO))) { // only need to draw side wall when not adjacent to room wall
			side.d[!c.dim][!d] += (d ? 1.0f : -1.0f)*(wall_width - wall_thick);
			cubes[2*d+1] = side; // left or right side
		}
		cubes[2*d] = front; // front left or front right
		doors.d[!c.dim][d] = walls[d].d[!c.dim][!d]; // clip door to space between walls
	} // for d
	doors.d[c.dim][ c.dir] -= (c.dir ? 1.0 : -1.0)*0.2*wall_thick; // shift in slightly
	doors.d[c.dim][!c.dir] += (c.dir ? 1.0 : -1.0)*(depth - 0.8*wall_thick); // make it narrow
	if (for_collision && c.is_open() && use_small_door) {cubes[4] = get_open_closet_door(c, doors);} // include open doors for small closets
	else {cubes[4] = doors;} // return closed door cube; caller must handle open door
}

void add_quad_to_mat(rgeom_mat_t &mat, point const pts[4], float const ts[4], float const tt[4], color_wrapper const &cw) {
	norm_comp normal(get_poly_norm(pts));
	for (unsigned n = 0; n < 4; ++n) {mat.quad_verts.emplace_back(pts[n], normal, ts[n], tt[n], cw);}
}

cube_t get_open_closet_door(room_object_t const &c, cube_t const &closed_door) {
	assert(c.is_small_closet()); // only small closets are supported for now
	float const door_width(closed_door.get_sz_dim(!c.dim)), door_thickness(closed_door.get_sz_dim(c.dim));
	cube_t door(closed_door);
	door.d[ c.dim][c.dir] += (c.dir ? 1.0 : -1.0)*door_width;
	door.d[!c.dim][1    ] -= (door_width - door_thickness);
	return door;
}
void building_room_geom_t::add_closet(room_object_t const &c, tid_nm_pair_t const &wall_tex, bool inc_lg, bool inc_sm) { // no lighting scale, houses only
	bool const open(c.is_open()), use_small_door(c.is_small_closet()), draw_interior(open || player_in_closet);
	cube_t cubes[5];
	get_closet_cubes(c, cubes);

	if (inc_lg) { // draw closet walls and doors
		rgeom_mat_t &wall_mat(get_material(get_scaled_wall_tex(wall_tex), 1));
		// need to draw the face that's against the wall for the shadow pass if the closet light is on, if the player is in the closet, or if the doors are open
		unsigned const skip_faces(EF_Z12); // skip top and bottom

		for (unsigned d = 0; d < 2; ++d) {
			bool const adj_room_wall(c.flags & (d ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO));
			unsigned const extra_skip_faces(adj_room_wall ? ~get_face_mask(!c.dim, d) : 0); // adjacent to room wall, skip that face
			// only need to draw side wall when not adjacent to room wall; skip front face of side wall
			if (!adj_room_wall) {wall_mat.add_cube_to_verts(cubes[2*d+1], c.color, tex_origin, (skip_faces | extra_skip_faces | get_skip_mask_for_xy(c.dim)));}
			unsigned const front_wall_skip_flags((draw_interior ? EF_Z12 : skip_faces) | extra_skip_faces);
			wall_mat.add_cube_to_verts(cubes[2*d], c.color, tex_origin, front_wall_skip_flags); // Note: c.color should be wall color
		} // for d
		cube_t const &doors(cubes[4]);
		point const llc(doors.get_llc());
		float const out_sign(c.dir ? 1.0 : -1.0);

		if (use_small_door) { // small house closet door
			tid_nm_pair_t const tp(get_int_door_tid(), 0.0);

			if (open) {
				cube_t const door(get_open_closet_door(c, doors));
				get_material(tp, 1).add_cube_to_verts(door, WHITE, llc, ~get_skip_mask_for_xy(!c.dim), c.dim, !c.dir); // draw front and back faces
				get_untextured_material(1).add_cube_to_verts_untextured(door, WHITE, ~get_skip_mask_for_xy( c.dim)); // draw edges untextured
			}
			else {
				unsigned const door_skip_faces(~get_skip_mask_for_xy(c.dim)); // draw both front and back faces so that shadows are cast correctly / light doesn't leak
				get_material(tp, 1).add_cube_to_verts(doors, WHITE, llc, door_skip_faces, !c.dim);
			}
		}
		else { // 4 panel folding door
			float const doors_width(doors.get_sz_dim(!c.dim)), door_thickness(doors.get_sz_dim(c.dim));
			float const door_spacing(0.25*doors_width), door_gap(0.01*door_spacing);
			int const tid(get_rect_panel_tid());
			float tx(1.0/doors_width), ty(0.25/doors.dz());
			if (!c.dim) {swap(tx, ty);} // swap so that ty is always in Z
			tid_nm_pair_t const door_tex(tid, get_normal_map_for_bldg_tid(tid), tx, ty); // 4x1 panels
			rgeom_mat_t &door_mat(get_material(door_tex, 1));
			bool const doors_fold(c.is_hanging()); // else doors slide

			if (doors_fold && open) { // draw open bifold doors open on both
				// Note: this doesn't always look correct because doors can intersect other objects such as lights and dressers, and they have no edge quads
				float const panel_len(0.2*doors.get_sz_dim(!c.dim) - 2.0*door_gap), open_amt(0.5*panel_len), extend(sqrt(panel_len*panel_len - open_amt*open_amt));
				float const nom_pos(doors.d[c.dim][!c.dir]), front_pos(nom_pos + out_sign*extend), z1(doors.z1()), z2(doors.z2());
				float const ts[4] = {0.0, 0.25, 0.25, 0.0}, tt[4] = {0.0, 0.0, 0.25, 0.25};
				color_wrapper const cw(WHITE);
				point side_pt, out_pt, inner_pt; // left side door points in this order from left to right, forming a V-shape pointing outward
				side_pt[c.dim] = inner_pt[c.dim] = nom_pos;
				out_pt [c.dim] = front_pos;

				for (unsigned side = 0; side < 2; ++side) {
					float const open_sign(side ? -1.0 : 1.0), side_pos(doors.d[!c.dim][side]);
					side_pt [!c.dim] = side_pos;
					out_pt  [!c.dim] = side_pos + open_sign*open_amt;
					inner_pt[!c.dim] = side_pos + 2*open_sign*open_amt;
					// outside faces
					point pts1o[4] = {point(side_pt.x, side_pt.y, z1), point(out_pt.x,   out_pt.y,   z1), point(out_pt.x,   out_pt.y,   z2), point(side_pt.x, side_pt.y, z2)};
					point pts2o[4] = {point(out_pt.x,  out_pt.y,  z1), point(inner_pt.x, inner_pt.y, z1), point(inner_pt.x, inner_pt.y, z2), point(out_pt.x,  out_pt.y,  z2)};
					if (!c.dim) {std::reverse(pts1o, pts1o+4); std::reverse(pts2o, pts2o+4);} // reverse the winding order
					add_quad_to_mat(door_mat, pts1o, ts, tt, cw);
					add_quad_to_mat(door_mat, pts2o, ts, tt, cw);
					// inside faces
					point pts1i[4], pts2i[4];
					
					for (unsigned n = 0; n < 4; ++n) { // create inside surfaces of doors with inverted winding order and normal
						pts1i[n] = pts1o[3-n]; pts1i[n][c.dim] += open_sign*door_thickness;
						pts2i[n] = pts2o[3-n]; pts2i[n][c.dim] += open_sign*door_thickness;
					}
					add_quad_to_mat(door_mat, pts1i, ts, tt, cw);
					add_quad_to_mat(door_mat, pts2i, ts, tt, cw);
					point edge[4] = {pts2i[2], pts2i[1], pts2o[2], pts2o[1]}; // the 4 inner points for the two sides of the door; independent of reverse
					float const tc_zeros[4] = {0,0,0,0};
					add_quad_to_mat(door_mat, edge, tc_zeros, tc_zeros, cw); // Note: edge is at an odd angle, not perpendicular to the door; is this good enough?
				} // for side
			}
			else { // draw closet door in 4 cube panels
				float const mid_gap(doors_fold ? door_gap : 0.0), gaps[5] = {door_gap, mid_gap, door_gap, mid_gap, door_gap};
				unsigned const open_n[4] = {0, 0, 3, 3};

				for (unsigned n = 0; n < 4; ++n) {
					unsigned const N(open ? open_n[n] : n);
					cube_t door(doors);
					door.d[!c.dim][0] = doors.d[!c.dim][0] +  N   *door_spacing + gaps[N  ]; // left  edge
					door.d[!c.dim][1] = doors.d[!c.dim][0] + (N+1)*door_spacing - gaps[N+1]; // right edge
					if (!doors_fold && (n == 1 || n == 2)) {door.translate_dim(c.dim, -1.1*out_sign*door_thickness);} // inset the inner sliding doors
					door_mat.add_cube_to_verts(door, WHITE, llc, skip_faces);
				}
			}
		}
	} // end inc_lg
	if (inc_sm) { // add wall trim for each side of the closet door
		float const height(c.dz()), window_vspacing(height*(1.0 + FLOOR_THICK_VAL_HOUSE));
		float const trim_height(0.04*window_vspacing), trim_thickness(0.1*WALL_THICK_VAL*window_vspacing);
		float const wall_thick(get_closet_wall_thickness(c)), trim_plus_wall_thick(trim_thickness + wall_thick);
		colorRGBA const trim_color(WHITE); // assume trim is white
		bool const draw_interior_trim(1 || draw_interior); // always enable so that we don't have to regenerate small geom when closet doors are opened or closed

		for (unsigned is_side = 0; is_side < 2; ++is_side) { // front wall, side wall
			for (unsigned d = 0; d < 2; ++d) {
				unsigned skip_faces((draw_interior_trim ? 0 : ~get_face_mask(c.dim, !c.dir)) | EF_Z1), trim_flags(0);
				cube_t trim;
				
				if (is_side) { // sides of closet
					if (c.flags & (d ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO)) continue; // adjacent to room wall, skip that wall
					trim = c;
					trim.d[!c.dim][!d]     = trim.d[!c.dim][d];
					trim.d[!c.dim][ d]    += (d     ? 1.0 : -1.0)*trim_thickness; // expand away from wall
					trim.d[ c.dim][c.dir] += (c.dir ? 1.0 : -1.0)*trim_thickness; // expand to cover the outside corner gap; doesn't really line up properly for angled ceiling trim though
					trim_flags |= (c.dir ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO); // shift trim bottom edge to prevent intersection with trim in other dim at outside corner
				}
				else { // front of closet on sides of door
					trim = cubes[2*d]; // start with front wall on this side
					trim.d[!c.dim][!d    ] -= (d ? -1.0 : 1.0)*0.1*trim_thickness; // shrink slightly to avoid z-fighting with closet wall/door frame when the door is open
					trim.d[ c.dim][!c.dir]  = trim.d[c.dim][c.dir];
					trim.d[ c.dim][ c.dir] += (c.dir ? 1.0 : -1.0)*trim_thickness; // expand away from wall
					
					// expand to cover the outside corner gap if not along room wall, otherwise hide the face; doesn't really line up properly for angled ceiling trim though
					if (c.flags & (d ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO)) {
						skip_faces |= ~get_face_mask(!c.dim, d); // disable hidden faces
					}
					else {
						trim.d[!c.dim][d] += (d ? 1.0 : -1.0)*trim_thickness;
						trim_flags |= (d ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO); // shift trim bottom edge to prevent intersection with trim in other dim at outside corner
					}
					trim_flags |= (d ? RO_FLAG_ADJ_BOT : RO_FLAG_ADJ_TOP); // flag ends that meet the door, which need to be capped
				}
				bool const trim_dim(c.dim ^ bool(is_side)), trim_dir(is_side ? d : c.dir);
				cube_t btrim(trim); // bottom trim
				if (is_side) {btrim.d[!c.dim][!d    ] -= (d     ? 1.0 : -1.0)*trim_plus_wall_thick;} // expand on the inside of the closet
				else         {btrim.d[ c.dim][!c.dir] -= (c.dir ? 1.0 : -1.0)*trim_plus_wall_thick;} // expand on the inside of the closet
				btrim.z2() = c.z1() + trim_height;
				get_untextured_material(0, 0, 1).add_cube_to_verts_untextured(btrim, trim_color, skip_faces); // is_small, untextured, no shadows; both interior and exterior
				set_cube_zvals(trim, c.z2()-trim_height, c.z2());
				// ceiling trim, missing end caps; exterior only
				add_wall_trim(room_object_t(trim, TYPE_WALL_TRIM, c.room_id, trim_dim, trim_dir, trim_flags, 1.0, SHAPE_ANGLED, trim_color), 1);
			} // for d
		} // for is_side
		// Note: always drawn to avoid recreating all small objects when the player opens/closes a closet door, and so that objects can be seen through the cracks in the doors
		if (!c.obj_expanded()) { // add boxes if not expanded
			vect_room_object_t &objects(get_temp_objects());
			add_closet_objects(c, objects);
			add_nested_objs_to_verts(objects);
		}
	} // end inc_sm
}

void building_room_geom_t::add_hanger_rod(room_object_t const &c) { // is_small=1
	get_wood_material(1.0, 1, 0, 1).add_ortho_cylin_to_verts(c, LT_GRAY, !c.dim, 0, 0, 0, 0, 1.0, 1.0, 0.25, 1.0, 0, 16, 0.0, 1); // 16 sides, swap_txy=1
}

void building_room_geom_t::add_drain_pipe(room_object_t const &c) { // is_small=1
	rgeom_mat_t &mat(get_untextured_material(0, 0, 1)); // unshadowed, small
	mat.add_vcylin_to_verts(c, apply_light_color(c), 0, 0); // draw sides only
	mat.add_disk_to_verts(point(c.xc(), c.yc(), c.z2()), 0.5*c.dx(), 0, BLACK); // draw top as black
}

void building_room_geom_t::add_key(room_object_t const &c) { // is_small=1
	rgeom_mat_t &key_mat(get_metal_material(0, 0, 1)); // untextured, unshadowed, small=1
	key_mat.add_cube_to_verts_untextured(c, apply_light_color(c)); // placeholder for case where there's no key 3D model
}

void building_room_geom_t::add_money(room_object_t const &c) { // is_small=1
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_money_tid(), 0.0), 0, 0, 1));
	mat.add_cube_to_verts(c, apply_light_color(c), zero_vector, ~EF_Z2, c.dim, (c.dim ^ c.dir ^ 1), c.dir); // top face only, no shadows
	unsigned const verts_start(mat.quad_verts.size());
	mat.add_cube_to_verts(c, apply_light_color(c), zero_vector, EF_Z12); // sides, no shadows; should this be untextured?
	for (auto i = mat.quad_verts.begin() + verts_start; i != mat.quad_verts.end(); ++i) {i->t[0] = i->t[1] = 0.0;} // set tex coords to 0 for sides to use border texture color
}

tid_nm_pair_t get_phone_tex(room_object_t const &c) {
	bool const is_ringing(c.flags & RO_FLAG_EMISSIVE), is_locked(c.flags & RO_FLAG_OPEN);
	if (!is_ringing && !is_locked) {return tid_nm_pair_t();} // off - untextured black
	// disable texture compression since it looks bad
	tid_nm_pair_t tp(get_texture_by_name((is_ringing ? "interiors/phone_ring_screen.jpg" : "interiors/phone_lock_screen.jpg"), 0, 0, 0, 0.0, 0), 0.0);
	tp.emissive = 1.0; // lit
	return tp;
}
void building_room_geom_t::add_phone(room_object_t const &c) { // is_small=1
	rgeom_mat_t &mat(get_untextured_material(0, 0, 1));
	mat.add_cube_to_verts_untextured(c, apply_light_color(c), EF_Z12); // sides, no shadows

	if (c.flags & (RO_FLAG_EMISSIVE | RO_FLAG_OPEN)) { // ringing or locked screen: top, no shadows, lit
		get_material(get_phone_tex(c), 0, 0, 1).add_cube_to_verts(c, WHITE, zero_vector, ~EF_Z2, c.dim, (c.dim ^ c.dir ^ 1), c.dir);
	}
	else {mat.add_cube_to_verts_untextured(c, apply_light_color(c, BLACK), ~EF_Z2);} // top, no shadows, unlit
}

void add_tproll_to_material(room_object_t const &c, rgeom_mat_t &mat) {
	colorRGBA const tp_color(apply_light_color(c));
	float const radius(0.5*c.dz()), rod_shrink(-0.7*radius), roll_shrink(0.75*rod_shrink*fract(123.456*c.obj_id)); // randomly partially empty (25-100%)
	cube_t roll(c);
	roll.expand_in_dim(c.dim, roll_shrink);
	roll.expand_in_dim(2,     roll_shrink); // z
	mat.add_ortho_cylin_to_verts(roll, tp_color, !c.dim, 1, 1); // c.dim applies to the wall; the roll is oriented perpendicular
	cube_t square(roll); // hanging square of TP
	set_cube_zvals(square, c.z1(), c.zc()); // starts at the centerline (tangent) and extends to the bottom
	if (c.is_hanging()) {square.z1() -= 3.0*c.dz();} // player has pulled it down lower
	square.d[c.dim][c.dir] = square.d[c.dim][!c.dir]; // shrink to zero thickness at outer edge
	mat.add_cube_to_verts_untextured(square, tp_color, ~get_skip_mask_for_xy(c.dim)); // only draw front/back faces
}
void building_room_geom_t::add_vert_roll_to_material(room_object_t const &c, rgeom_mat_t &mat, float sz_ratio, bool player_held) { // TP and tape
	bool const is_tape(c.type == TYPE_TAPE);
	float const hole_shrink(is_tape ? 0.24 : 0.3);
	cube_t hole(c);
	hole.expand_by_xy(-hole_shrink*c.dx());
	cube_t tube(hole);
	mat.add_vcylin_to_verts(tube, apply_light_color(c, LT_BROWN), 0, 0, 1); // tube, sides only, two sided (only need inside)
	if (sz_ratio == 0.0) return; // empty, tube only, don't need to draw the rest of the roll
	cube_t roll(c);
	if (sz_ratio < 1.0) {roll.expand_by_xy(-hole_shrink*(1.0 - sz_ratio)*c.dx());} // partially used
	hole.expand_in_dim(2, 0.0025*c.dz()); // expand slightly to avoid z-fighting
	// draw top/bottom surface only to mask off the outer part of the roll when held by the player; when resting on an object, draw the top surface only
	mat.add_vcylin_to_verts(hole, ALPHA0, player_held, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 1); // hole
	mat.add_vcylin_to_verts(roll, apply_light_color(c), 1, 1); // paper/plastic roll
}
void building_room_geom_t::add_tproll(room_object_t const &c) { // is_small=1
	if (c.was_expanded()) { // bare TP roll from a box
		add_vert_roll_to_material(c, get_untextured_material(1, 0, 1)); // shadowed, small
		return;
	}
	if (c.taken_level == 0) {add_tproll_to_material(c, get_untextured_material(1, 0, 1));} // draw the roll if not taken
	float const radius(0.5*c.dz()), rod_shrink(-0.7*radius), length(c.get_width());
	// draw the holder attached to the wall
	rgeom_mat_t &holder_mat(get_metal_material(1, 0, 1)); // untextured, shadowed, small=1
	colorRGBA const holder_color(apply_light_color(c, GRAY));
	cube_t rod(c), plate(c);
	rod.expand_in_dim( c.dim, rod_shrink);
	rod.expand_in_dim( 2,     rod_shrink); // z
	rod.expand_in_dim(!c.dim, 0.05*length); // will go slightly outside the bounds of c
	float const rod_end(rod.d[!c.dim][0]); // arbitrarily choose lower end
	plate.expand_in_dim(2, -0.65*radius); // z
	plate.d[c.dim][!c.dir] -= (c.dir ? -1.0 : 1.0)*0.6*radius;
	plate.d[!c.dim][0] = rod_end - 0.08*length; // set thickness; will go slightly outside the bounds of c
	plate.d[!c.dim][1] = rod_end;
	holder_mat.add_ortho_cylin_to_verts(rod, holder_color, !c.dim, 0, 1);
	holder_mat.add_cube_to_verts_untextured(plate, holder_color, ~get_face_mask(c.dim, c.dir)); // skip the face against the wall
}
void building_room_geom_t::add_tape(room_object_t const &c) { // is_small=1
	rgeom_mat_t &mat(get_untextured_material(1, 0, 1));
	
	if (c.was_expanded()) {
		// if tape was in a drawer, then the hole won't properly blend with the wood under it, making the floor visible; this applies to some boxes as well;
		// draw a cardboard colored circle under the hole before drawing the roll to sort of cover this up (though it's not textured);
		cube_t bot_fill(c);
		bot_fill.expand_by_xy(-0.1*c.dx());
		bot_fill.z2() -= 0.95*c.dz();
		mat.add_vcylin_to_verts(bot_fill, apply_light_color(c, texture_color(get_box_tid())), 0, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 1); // top side only
	}
	add_vert_roll_to_material(c, mat); // shadowed, small
}

void building_room_geom_t::add_spraycan_to_material(room_object_t const &c, rgeom_mat_t &mat) {
	unsigned const dim(get_max_dim(c.get_size()));
	bool const add_bottom(dim != 2); // if on its side
	cube_t can(c), cap(c);
	can.d[dim][!c.dir] = cap.d[dim][c.dir] = (c.d[dim][c.dir] + 0.7*c.get_sz_dim(dim)*(c.dir ? -1.0 : 1.0)); // point between top of can and bottom of cap
	mat.add_ortho_cylin_to_verts(can, apply_light_color(c, DK_GRAY), dim, (add_bottom && !c.dir), (add_bottom && c.dir)); // sides + bottom (if on side)
	mat.add_ortho_cylin_to_verts(cap, apply_light_color(c), dim, c.dir, !c.dir); // sides + top
}
void building_room_geom_t::add_spraycan(room_object_t const &c) { // is_small=1
	add_spraycan_to_material(c, get_untextured_material(1, 0, 1));
}

void building_room_geom_t::add_button(room_object_t const &c) {
	bool const in_elevator(c.in_elevator());
	tid_nm_pair_t tp; // unshadowed
	if (c.is_active()) {tp.emissive = 1.0;} // make it lit when active
	colorRGBA const color(apply_light_color(c));
	rgeom_mat_t &mat(get_material(tp, 0, in_elevator, !in_elevator)); // (in_elevator ? dynamic : small)
	if      (c.shape == SHAPE_CUBE ) {mat.add_cube_to_verts_untextured(c, color, ~get_face_mask(c.dim, !c.dir));} // square button
	else if (c.shape == SHAPE_CYLIN) {mat.add_ortho_cylin_to_verts(c, color, c.dim, !c.dir, c.dir);} // round button
	else {assert(0);}
	
	if (!in_elevator) { // add the frame for better color contrast
		float const expand(0.7*c.dz());
		cube_t frame(c);
		frame.d[c.dim][c.dir] -= 0.9*(c.dir ? 1.0 : -1.0)*c.get_depth(); // shorten to a sliver against the elevator wall
		frame.expand_in_dim(!c.dim, expand);
		frame.expand_in_dim(2,      expand); // Z
		get_untextured_material(0, 0, 1).add_cube_to_verts_untextured(frame, apply_light_color(c, DK_GRAY), ~get_face_mask(c.dim, !c.dir)); // small
	}
}

void building_room_geom_t::add_crate(room_object_t const &c) { // is_small=1
	// Note: draw as "small", not because crates are small, but because they're only added to windowless rooms and can't be easily seen from outside a building
	get_material(tid_nm_pair_t(get_crate_tid(c), 0.0), 1, 0, 1).add_cube_to_verts(c, apply_light_color(c), zero_vector, EF_Z1); // skip bottom face (even for stacked crate?)
}

void building_room_geom_t::add_box(room_object_t const &c) { // is_small=1
	// Note: draw as "small", not because boxes are small, but because they're only added to windowless rooms and can't be easily seen from outside a building
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_box_tid(), get_texture_by_name("interiors/box_normal.jpg", 1), 0.0, 0.0), 1, 0, 1)); // is_small=1
	float const sz(2048), x1(12/sz), x2(576/sz), x3(1458/sz), y1(1-1667/sz), y2(1-1263/sz), y3(1-535/sz); //, x4(2032/sz), y4(1-128/sz); // Note: we don't use all parts of the texture
	unsigned verts_start(mat.quad_verts.size());
	colorRGBA const color(apply_light_color(c));
	mat.add_cube_to_verts(c, color, zero_vector, (c.is_open() ? EF_Z2 : EF_Z1)); // skip bottom face (even for stacked box?)
	assert(mat.quad_verts.size() == verts_start + 20); // there should be 5 quads (+z -x +x -y +y) / 20 verts (no -z)
	mat.quad_verts[verts_start+0].set_tc(x1, y2); // z (top or inside bottom)
	mat.quad_verts[verts_start+1].set_tc(x2, y2);
	mat.quad_verts[verts_start+2].set_tc(x2, y3);
	mat.quad_verts[verts_start+3].set_tc(x1, y3);

	for (unsigned d = 0; d < 2; ++d) { // for each end
		unsigned const ix_shift((1 + 2*d)*c.dim); // needed to make sure the up icon actually faces up
		unsigned ix(verts_start + 4*d + 4);

		for (unsigned e = 0; e < 2; ++e) { // x, y
			bool const f(c.dim ^ bool(e));
			mat.quad_verts[ix+((0+ix_shift)&3)].set_tc(x2, (f ? y1 : y2));
			mat.quad_verts[ix+((1+ix_shift)&3)].set_tc(x3, (f ? y1 : y2));
			mat.quad_verts[ix+((2+ix_shift)&3)].set_tc(x3, (f ? y2 : y3));
			mat.quad_verts[ix+((3+ix_shift)&3)].set_tc(x2, (f ? y2 : y3));
			ix += 8; // skip the other face
		} // for e
	} // for d
	if (c.is_open()) {
		// draw the inside of the box
		verts_start = mat.quad_verts.size(); // update
		cube_t box(c);
		box.expand_by_xy(-0.001*c.get_size()); // slight shrink of inside of box to prevent z-fighting
		box.z1() += 0.01*c.dz(); // shrink a bit more in Z
		mat.add_cube_to_verts(box, color, zero_vector, EF_Z2, 0, 0, 0, 1); // skip top face; draw inverted
		assert(mat.quad_verts.size() == verts_start + 20); // there should be 5 quads (+z -x +x -y +y) / 20 verts (no +z)
		float const ts[4] = {x2, x3, x3, x2}, tt[4] = {y1, y1, y2, y2};

		for (unsigned side = 0; side < 5; ++side) { // make all sides use a subset of the texture that has no markings
			unsigned ix(verts_start + 4*side);
			for (unsigned n = 0; n < 4; ++n) {mat.quad_verts[ix+n].set_tc(ts[n], tt[n]);}
		}
		if (!c.was_expanded()) { // draw open box flaps, but not if box is in drawer/shelf/closet because there may not be space for flaps
			vector3d const box_sz(c.get_size());
			float const flap_len(0.485*min(box_sz.x, box_sz.y)); // same length in both dims; slightly less than half-width because this is the base of the triangle
			color_wrapper const cw(color);
			unsigned const up_verts[2][4] = {{0,1,0,2}, {3,2,1,3}}; // vertex indices on upward pointing outside flap edges

			for (unsigned d = 0; d < 2; ++d) { // x/y
				for (unsigned e = 0; e < 2; ++e) { // side dir
					unsigned const side_ix(2*d+e);
					bool const against_wall(c.flags & (RO_FLAG_ADJ_LO << side_ix)); // encoded in adj flags
					cube_t C(box);
					C.d[d][!e] = C.d[d][e];
					C.d[d][ e] = C.d[d][e] + (e ? 1.0 : -1.0)*(against_wall ? 0.05 : 1.0)*flap_len;
					float const zbot(C.z2()), dz(against_wall ? flap_len : 0.25*min(flap_len, box_sz.z)); // tilted somewhat upward; pointing up if against wall
					point const pts[4] = {point(C.x1(), C.y1(), zbot), point(C.x2(), C.y1(), zbot), point(C.x2(), C.y2(), zbot), point(C.x1(), C.y2(), zbot)};
					unsigned const ix(mat.quad_verts.size());
					add_quad_to_mat(mat, pts, ts, tt, cw);
					for (unsigned n = 0; n < 2; ++n) {mat.quad_verts[ix + up_verts[n][side_ix]].v.z += dz;}
					
					// add bottom surface with inverted normal in reverse order
					for (unsigned n = 0; n < 4; ++n) {
						mat.quad_verts.push_back(mat.quad_verts[ix+3-n]);
						mat.quad_verts.back().invert_normal();
					}
				} // for e
			} // for d
		}
	}
}

void building_room_geom_t::add_paint_can(room_object_t const &c) {
	float const side_tscale_add(fract(11111*c.x1() + 22222*c.y1() + 33333*c.z1())); // somewhat random
	rgeom_mat_t &side_mat(get_material(tid_nm_pair_t(get_texture_by_name("interiors/paint_can_label.png")), 1, 0, 1)); // shadows, small
	side_mat.add_vcylin_to_verts(c, apply_light_color(c), 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 24, side_tscale_add); // draw sides only; random texture rotation
	point top(c.get_cube_center());
	top.z = c.z2();
	get_metal_material(1, 0, 1).add_disk_to_verts(top, 0.5*min(c.dx(), c.dy()), 0, apply_light_color(c, LT_GRAY)); // shadowed, specular metal; small=1
}

void building_room_geom_t::add_shelves(room_object_t const &c, float tscale) {
	// Note: draw as "small", not because shelves are small, but because they're only added to windowless rooms and can't be easily seen from outside a building
	// draw back in case it's against a window, even though that shouldn't happen
	bool const is_house(c.is_house());
	cube_t shelves[4]; // max number of shelves
	unsigned const num_shelves(get_shelves_for_object(c, shelves));
	// add wooden shelves
	unsigned const skip_faces(is_house ? 0 : ~get_face_mask(c.dim, c.dir)); // skip back face at wall if it's a house because it could be against a window (though it really shouldn't be)
	rgeom_mat_t &wood_mat(get_wood_material(tscale, 1, 0, 1)); // inc_shadows=1, dynamic=0, small=1
	colorRGBA const shelf_color(apply_light_color(c));

	for (unsigned s = 0; s < num_shelves; ++s) {
		wood_mat.add_cube_to_verts(shelves[s], shelf_color, c.get_llc(), skip_faces, !c.dim); // make wood grain horizontal
	}
	if (c.flags & RO_FLAG_INTERIOR) { // add support brackets to interior shelves; skip them if against an exterior wall in case they intersect a window
		vector3d const c_sz(c.get_size());
		float const dz(c_sz.z), length(c_sz[!c.dim]), width(c_sz[c.dim]), thickness(0.02*dz), bracket_thickness(0.8*thickness);
		unsigned const num_brackets(2 + round_fp(0.5*length/dz));
		float const b_offset(0.05*dz), b_step((length - 2*b_offset)/(num_brackets-1)), bracket_width(1.8*thickness);
		rgeom_mat_t &metal_mat(get_metal_material(1, 0, 1)); // shadowed, specular metal; small=1
		colorRGBA const bracket_color(apply_light_color(c, LT_GRAY));

		for (unsigned s = 0; s < num_shelves; ++s) {
			cube_t bracket(shelves[s]);
			bracket.z2()  = bracket.z1(); // below the shelf
			bracket.z1() -= bracket_thickness;
			bracket.d[c.dim][!c.dir] -= (c.dir ? -1.0 : 1.0)*0.1*width; // shorten slightly
			bracket.d[!c.dim][1] = bracket.d[!c.dim][0] + bracket_width; // set width
			bracket.translate_dim(!c.dim, b_offset);

			for (unsigned b = 0; b < num_brackets; ++b) {
				metal_mat.add_cube_to_verts_untextured(bracket, bracket_color, (skip_faces | EF_Z2)); // skip top faces, maybe back

				if (s == 0) { // add vertical brackets on first shelf
					cube_t vbracket(bracket);
					set_cube_zvals(vbracket, c.z1(), c.z2());
					vbracket.d[c.dim][ c.dir] = c         .d[c.dim][c.dir] + (c.dir ? -1.0 : 1.0)*0.1*bracket_thickness; // nearly against the wall
					vbracket.d[c.dim][!c.dir] = shelves[s].d[c.dim][c.dir]; // against the shelf
					metal_mat.add_cube_to_verts_untextured(vbracket, bracket_color, (skip_faces | EF_Z12)); // skip top/bottom faces, maybe back
				}
				bracket.translate_dim(!c.dim, b_step);
			} // for b
		} // for s
	}
	// add objects to the shelves
	if (c.obj_expanded()) return; // shelves have already been expanded, don't need to create contained objects below
	vect_room_object_t &objects(get_temp_objects());
	get_shelf_objects(c, shelves, num_shelves, objects);
	add_small_static_objs_to_verts(objects, 1); // is_nested=1
}

void building_room_geom_t::add_obj_with_top_texture(room_object_t const &c, string const &texture_name, colorRGBA const &sides_color, bool is_small) {
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_texture_by_name(texture_name), 0.0), 1, 0, is_small)); // shadows
	mat.add_cube_to_verts(c, apply_light_color(c), zero_vector, ~EF_Z2, c.dim, (c.dim ^ c.dir ^ 1), c.dir); // top face only
	get_untextured_material(1, 0, is_small).add_cube_to_verts_untextured(c, apply_light_color(c, sides_color), EF_Z12); // sides, no shadows, small
}
void building_room_geom_t::add_obj_with_front_texture(room_object_t const &c, string const &texture_name, colorRGBA const &sides_color, bool is_small) {
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_texture_by_name(texture_name), 0.0), 1, 0, is_small)); // shadows
	unsigned const front_mask(get_face_mask(c.dim, c.dir));
	mat.add_cube_to_verts(c, apply_light_color(c), zero_vector, front_mask, !c.dim, (c.dim ^ c.dir ^ 1), 0); // front face only
	get_untextured_material(1, 0, is_small).add_cube_to_verts_untextured(c, apply_light_color(c, sides_color), ~front_mask); // sides, shadows
}

void building_room_geom_t::add_keyboard(room_object_t const &c) {add_obj_with_top_texture  (c, "interiors/keyboard.jpg",  BKGRAY, 1);} // is_small=1
void building_room_geom_t::add_laptop  (room_object_t const &c) {add_obj_with_top_texture  (c, "interiors/laptop.jpg",    BKGRAY, 1);} // is_small=1
void building_room_geom_t::add_computer(room_object_t const &c) {add_obj_with_front_texture(c, "interiors/computer.jpg",  BKGRAY, 1);} // is_small=1

cube_t get_mwave_panel_bcube(room_object_t const &c) {
	cube_t panel(c);
	bool const open_dir(c.dim ^ c.dir ^ 1);
	panel.d[!c.dim][open_dir] -= (open_dir ? 1.0 : -1.0)*0.8*c.get_width(); // right 25%
	return panel;
}
void building_room_geom_t::add_mwave(room_object_t const &c) {
	string const texture_name(c.is_powered() ? "interiors/microwave.jpg" : "interiors/microwave_off.jpg");

	if (c.is_open()) { // door is open
		bool const open_dir(c.dim ^ c.dir ^ 1);
		cube_t const panel(get_mwave_panel_bcube(c));
		cube_t body(c);
		body.d[!c.dim][!open_dir] = panel.d[!c.dim][open_dir]; // the other half
		// draw the sides/top/back
		unsigned const front_mask(get_face_mask(c.dim, c.dir));
		get_untextured_material(1).add_cube_to_verts_untextured(c, apply_light_color(c, GRAY), ~front_mask); // sides, shadows, is_small=0
		// draw the interior
		colorRGBA const interior_color(apply_light_color(c, WHITE));
		float const wall_width(0.25*panel.get_sz_dim(!c.dim));
		cube_t interior(body);
		interior.expand_by(-wall_width);
		// make interior flush with body; there should be a door width gap, but then the front won't line up with the panel
		interior.d[c.dim][c.dir] = body.d[c.dim][c.dir];
		rgeom_mat_t &untex_mat(get_untextured_material(1)); // shadowed
		untex_mat.add_cube_to_verts(interior, interior_color, all_zeros, ~front_mask, 0, 0, 0, 1); // skip the front face, inverted=1

		for (unsigned d = 0; d < 2; ++d) { // left/right, bottom/top
			cube_t side(body), tb(body);
			side.d[!c.dim][d] = interior.d[!c.dim][!d];
			tb.d[2][d] = interior.d[2][!d];
			untex_mat.add_cube_to_verts_untextured(side, interior_color, front_mask); // only the front face
			untex_mat.add_cube_to_verts_untextured(tb,   interior_color, front_mask); // only the front face
		} // for d
		unsigned const door_front_mask(get_face_mask(!c.dim, open_dir));
		colorRGBA const color(apply_light_color(c));
		float const door_length(body.get_sz_dim(!c.dim)), tscale(1.0/c.get_width());
		cube_t door(body);
		door.d[ c.dim][!c.dir]    = door.d[ c.dim][c.dir]; // shrink to zero area at front face
		door.d[ c.dim][ c.dir]   += (c.dir ? 1.0 : -1.0)*door_length; // extend outward to door length
		door.d[!c.dim][!open_dir] = door.d[!c.dim][open_dir] + (open_dir ? -1.0 : 1.0)*0.8*wall_width; // set door width = 80% of wall width
		untex_mat.add_cube_to_verts_untextured(door, BLACK, ~door_front_mask); // interior and sides drawn in black; shadowed
		int const tid(get_texture_by_name(texture_name));
		// draw the open door
		bool const panel_mx(open_dir), door_mx(panel_mx ^ c.dim);
		tid_nm_pair_t tex(tid, -1, (c.dim ? 0.0 : tscale), (c.dim ? tscale : 0.0));
		get_material(tex, 1, 0, 0).add_cube_to_verts(door, color, (door_mx ? door.get_urc() : door.get_llc()), door_front_mask, c.dim, door_mx, 0); // shadows, is_small=0
		// draw the front panel, front face only
		swap(tex.tscale_x, tex.tscale_y); // clipped in other dim
		get_material(tex, 1, 0, 0).add_cube_to_verts(panel, color, (panel_mx ? c.get_urc() : c.get_llc()), front_mask, !c.dim, panel_mx, 0); // shadows, is_small=0
	}
	else { // closed
		add_obj_with_front_texture(c, texture_name, GRAY, 0); // is_small=0
	}
}

float get_med_cab_wall_thickness(room_object_t const &c) {return 0.15*c.get_length();}

cube_t get_mirror_surface(room_object_t const &c) {
	if (!c.is_open()) return c;
	float const thickness(get_med_cab_wall_thickness(c));
	cube_t mirror(c);
	mirror.d[!c.dim][0]       = mirror.d[!c.dim][1]; // shrink to zero area
	mirror.d[!c.dim][1]      += thickness; // shift outward by thickness
	mirror.d[ c.dim][!c.dir] -= (c.dir ? 1.0 : -1.0)*thickness; // move to front
	mirror.d[ c.dim][ c.dir] += (c.dir ? 1.0 : -1.0)*(c.get_width() - thickness); // expand outward
	return mirror;
}
void building_room_geom_t::add_mirror(room_object_t const &c) {
	bool const shadowed(c.is_house()); // medicine cabinets in houses cast shadows
	colorRGBA const side_color(apply_light_color(c));

	if (c.is_open()) { // open medicine cabinet
		cube_t const mirror(get_mirror_surface(c));
		float const wall_thickness(get_med_cab_wall_thickness(c));
		cube_t outside(c), inside(c);
		inside.expand_by(-wall_thickness); // shrink sides by wall thickness
		outside.d[c.dim][c.dir] = inside.d[c.dim][c.dir]; // shift front side in slightly
		unsigned const mirror_face_mask(get_face_mask(!c.dim, 1)); // always +dir
		draw_mirror_surface(c, mirror, !c.dim, 1, shadowed); // always +dir
		vect_cube_t &cubes(get_temp_cubes());
		subtract_cube_from_cube(outside, inside, cubes); // should always be 3 cubes (sides + back) since this subtract is XY only
		cubes.push_back(inside);
		set_cube_zvals(cubes.back(), outside.z1(), inside.z1()); // bottom
		cubes.push_back(inside);
		set_cube_zvals(cubes.back(), inside.z2(), outside.z2()); // top
		cubes.push_back(inside);
		set_wall_width(cubes.back(), inside.zc(), 0.5*wall_thickness, 2); // middle shelf
		rgeom_mat_t &mat(get_untextured_material(shadowed));
		mat.add_cube_to_verts(mirror, side_color, zero_vector, ~mirror_face_mask); // non-front sides of mirror
		
		for (auto i = cubes.begin(); i != cubes.end(); ++i) {
			unsigned sf(~get_face_mask(c.dim, !c.dir));
			if (i - cubes.begin() > 3) {sf |= get_skip_mask_for_xy(!c.dim);} // sip side faces
			mat.add_cube_to_verts(*i, side_color, zero_vector, sf); // skip back face
		}
	}
	else { // closed
		draw_mirror_surface(c, c, c.dim, c.dir, shadowed);
		get_untextured_material(shadowed).add_cube_to_verts_untextured(c, side_color, get_skip_mask_for_xy(c.dim)); // draw only the exterior sides, untextured
	}
}

void building_room_geom_t::add_shower(room_object_t const &c, float tscale) {
	bool const xdir(c.dim), ydir(c.dir), dirs[2] = {xdir, ydir}; // placed in this corner
	bool const tile_type2(c.obj_id & 1);
	vector3d const sz(c.get_size());
	float const signs[2] = {(xdir ? -1.0f : 1.0f), (ydir ? -1.0f : 1.0f)};
	colorRGBA tile_color(apply_light_color(c));
	if (!tile_type2) {tile_color = tile_color.modulate_with(colorRGBA(0.8, 0.6, 0.4));} // darker/browner
	// add tile material along walls and floor
	int const skip_faces[2] = {(EF_Z1 | (xdir ? EF_X2 : EF_X1)), (EF_Z1 | (ydir ? EF_Y2 : EF_Y1))};
	rgeom_mat_t &tile_mat(get_material(tid_nm_pair_t((tile_type2 ? TILE_TEX : get_texture_by_name("bathroom_tile.jpg")), 2.5*tscale), 0)); // no shadows
	cube_t bottom(c), sides[2] = {c, c};
	bottom.z2() = c.z1() + 0.025*sz.z;
	tile_mat.add_cube_to_verts(bottom, tile_color, zero_vector, (skip_faces[0] | skip_faces[1]));

	for (unsigned d = 0; d < 2; ++d) {
		sides[d].d[d][!dirs[d]] -= signs[d]*0.98*sz[d];
		sides[d].z1() = bottom.z2();
		tile_mat.add_cube_to_verts(sides[d], tile_color, zero_vector, skip_faces[d]);
	}
	// add metal frame around glass
	colorRGBA const metal_color(apply_light_color(c, GRAY));
	rgeom_mat_t &metal_mat(get_metal_material(1)); // shadowed, specular metal
	cube_t fc(c); // corner frame
	set_cube_zvals(fc, bottom.z2(), (c.z2() - 0.05*sz.z)); // slightly shorter than tile
	cube_t fxy[2] = {fc, fc};
	float const glass_bot(fc.z1() + 0.02*sz.z), glass_top(fc.z2() - 0.02*sz.z);

	for (unsigned d = 0; d < 2; ++d) {
		cube_t &f(fxy[d]);
		f.d[ d][ dirs[ d]]  = sides[d].d[d][!dirs[d]];
		f.d[ d][!dirs[ d]]  = sides[d].d[d][!dirs[d]] + signs[d]*0.04*sz[d];
		f.d[!d][ dirs[!d]] += signs[!d]*0.94*sz[!d];
		f.d[!d][!dirs[!d]] -= signs[!d]*0.02*sz[!d];
		metal_mat.add_cube_to_verts_untextured(f, metal_color, skip_faces[d]);
		fc.d[!d][0] = f.d[!d][0]; fc.d[!d][1] = f.d[!d][1];
	}
	metal_mat.add_cube_to_verts_untextured(fc, metal_color, EF_Z1);

	for (unsigned d = 0; d < 2; ++d) { // add top and bottom bars; these overlap with vertical frame cubes, but it should be okay and simplifies the math
		unsigned const bar_skip_faces(get_skip_mask_for_xy(d));
		cube_t tb_bars(fxy[d]);
		tb_bars.union_with_cube(fc);
		cube_t bot_bar(tb_bars), top_bar(tb_bars);
		bot_bar.z2() = glass_bot;
		top_bar.z1() = glass_top;
		metal_mat.add_cube_to_verts_untextured(bot_bar, metal_color, bar_skip_faces); // the track
		metal_mat.add_cube_to_verts_untextured(top_bar, metal_color, bar_skip_faces);
	}
	// add shower head
	float const radius(0.5f*(sz.x + sz.y));
	bool const head_dim(sz.y < sz.x);
	point start_pos, end_pos, base_pos, head_pos;
	start_pos.z = c.z1() + 0.75*sz.z;
	start_pos[ head_dim] = sides[head_dim].d[head_dim][!dirs[head_dim]];
	start_pos[!head_dim] = c.get_center_dim(!head_dim);
	base_pos = start_pos;
	base_pos[head_dim] += signs[head_dim]*0.06*sz[head_dim]; // move out from the wall
	end_pos  = base_pos;
	end_pos[head_dim] += signs[head_dim]*0.02*sz[head_dim]; // move out from the wall a bit more
	head_pos = base_pos;
	head_pos.z -= 0.05*sz.z;
	head_pos[ head_dim] += signs[head_dim]*0.09*sz[head_dim];
	metal_mat.add_cylin_to_verts(base_pos,  head_pos, 0.02*radius, 0.10*radius, metal_color, 0, 1); // draw top/wide end only
	metal_mat.add_cylin_to_verts(start_pos, end_pos,  0.02*radius, 0.02*radius, metal_color, 0, 0); // no ends
	// add door handle
	bool const hdim(c.dx() < c.dy()), hdir(dirs[hdim]), hside(!dirs[!hdim]); // hdim is the larger dim
	float const frame_width(fc.dx()), door_width(sz[!hdim]);
	float const wall_dist(0.77*door_width), handle_thickness(0.8*frame_width), hdir_sign(hdir ? -1.0 : 1.0), wall_pos(c.d[hdim][!hdir]);
	cube_t handle(c);
	handle.z1() += 0.48*sz.z;
	handle.z2() -= 0.42*sz.z;

	if (c.is_open()) { // move it into the open position; the math for this is pretty complex, so it's somewhat of a trial-and error with the constants
		bool const odir(dirs[hdim]);
		float const odir_sign(odir ? -1.0 : 1.0), hside_sign(hside ? 1.0 : -1.0), inner_extend(wall_pos + odir_sign*(wall_dist - 2.0*frame_width));
		float const open_glass_pos(c.d[!hdim][!hside] + hside_sign*0.39*frame_width);
		handle.d[!hdim][ hside] = open_glass_pos;
		handle.d[!hdim][!hside] = open_glass_pos + hside_sign*handle_thickness; // outer edge
		handle.d[ hdim][!odir ] = inner_extend;
		handle.d[ hdim][ odir ] = inner_extend + odir_sign*0.03*door_width;
	}
	else { // closed
		float const hside_sign(hside ? -1.0 : 1.0), glass_pos(wall_pos - hdir_sign*(0.19*frame_width + 0.02*sz[hdir])); // place on the glass but slightly offset
		handle.d[ hdim][ hdir ]  = glass_pos;
		handle.d[ hdim][!hdir ]  = glass_pos + hdir_sign*handle_thickness; // outer edge
		handle.d[!hdim][ hside] += hside_sign*0.20*door_width; // distance to outer wall/corner
		handle.d[!hdim][!hside] -= hside_sign*wall_dist;
	}
	metal_mat.add_cube_to_verts_untextured(handle, metal_color); // draw all faces
	// add drain
	cube_t drain;
	drain.set_from_point(bottom.get_cube_center());
	set_cube_zvals(drain, bottom.z2(), (bottom.z2() + 0.05*bottom.dz())); // very small height
	drain.expand_by_xy(0.06*radius); // set radius
	get_material(tid_nm_pair_t(MANHOLE_TEX, 0.0), 0).add_vcylin_to_verts(drain, metal_color, 0, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 1); // draw top only, not sides, unshadowed
	// add transparent glass
	colorRGBA const glass_color(apply_light_color(c, colorRGBA(1.0, 1.0, 1.0, 0.25)));
	rgeom_mat_t &glass_mat(get_untextured_material(0, 0, 0, 1)); // no shadows; transparent=1

	for (unsigned d = 0; d < 2; ++d) { // for each dim
		bool const dir(dirs[d]);
		cube_t glass(fc); // start from the frame at the corner
		glass.z1()  = glass_bot;
		glass.z2()  = glass_top;
		glass.z1() += 0.002*sz.z; // to prevent z-fighting
		glass.z2() -= 0.002*sz.z; // to prevent z-fighting
		glass.d[d][!dir] = glass. d[d][ dir]; // corner point; remove overlap with frame
		glass.d[d][ dir] = fxy[d].d[d][!dir]; // edge near the wall
		glass.expand_in_dim( d, -0.01*frame_width); // to prevent z-fighting
		glass.expand_in_dim(!d, -0.20*frame_width); // set thickness

		if (bool(d) != hdim && c.is_open()) { // draw open door
			bool const odir(dirs[!d]);
			float const width(glass.get_sz_dim(d)), thickness(glass.get_sz_dim(!d)), delta(width - thickness);
			glass.d[ d][! dir] -= ( dir ? -1.0 : 1.0)*delta; // shrink width to thickness
			glass.d[!d][!odir] += (odir ? -1.0 : 1.0)*delta; // expand thickness to width
			// draw frame part of door
			float const door_frame_width(0.4*frame_width);
			cube_t top(glass), bot(glass), side(glass);
			set_cube_zvals(top, (glass.z2() + 0.01*door_frame_width), (glass.z2() + door_frame_width)); // prevent z-fighting
			set_cube_zvals(bot, (glass.z1() - door_frame_width), (glass.z1() - 0.01*door_frame_width));
			side.d[!d][!odir] = top.d[!d][!odir] = bot.d[!d][!odir] = glass.d[!d][!odir] + (odir ? -1.0 : 1.0)*door_frame_width;
			side.d[!d][ odir] = glass.d[!d][!odir]; // flush with glass on this end
			rgeom_mat_t &metal_mat2(get_metal_material(1)); // get the metal material again, in case the reference was invaldiated
			metal_mat2.add_cube_to_verts_untextured(top,  metal_color); // draw all faces
			metal_mat2.add_cube_to_verts_untextured(bot,  metal_color); // draw all faces
			metal_mat2.add_cube_to_verts_untextured(side, metal_color, EF_Z12); // skip top and bottom faces
		}
		glass_mat.add_cube_to_verts(glass, glass_color, all_zeros, 0, 0, 0, 0, 1); // inside surface, inverted
		glass_mat.add_cube_to_verts_untextured(glass, glass_color, (EF_Z1 | (d ? EF_Y12 : EF_X12))); // outside surface
	} // for d
}

void building_room_geom_t::add_bottle(room_object_t const &c) {
	// obj_id: bits 1-3 for type, bits 6-7 for emptiness, bit 6 for cap color
	unsigned const bottle_ndiv = 16; // use smaller ndiv to reduce vertex count
	tid_nm_pair_t tex(-1, 1.0, 1); // shadowed
	tex.set_specular(0.5, 80.0);
	rgeom_mat_t &mat(get_material(tex, 1, 0, 1)); // inc_shadows=1, dynamic=0, small=1
	colorRGBA const color(apply_light_color(c)), cap_colors[2] = {LT_GRAY, GOLD}; // should gold caps have gold colored specular?
	vector3d const sz(c.get_size());
	unsigned const dim(get_max_dim(sz)), dim1((dim+1)%3), dim2((dim+2)%3);
	bool const is_empty(c.is_bottle_empty()), add_bottom(dim != 2); // add bottom if bottle is on its side
	float const dir_sign(c.dir ? -1.0 : 1.0), radius(0.25f*(sz[dim1] + sz[dim2])); // base should be square (default/avg radius is 0.15*height)
	float const length(sz[dim]); // AKA height, if standing up
	cube_t sphere(c), body(c), neck(c);
	sphere.d[dim][ c.dir] = c.d[dim][c.dir] + dir_sign*0.5*length;
	sphere.d[dim][!c.dir] = sphere.d[dim][c.dir] + dir_sign*0.3*length;
	body  .d[dim][!c.dir] = sphere.d[dim][c.dir] + dir_sign*0.15*length;
	neck  .d[dim][ c.dir] = body.d[dim][!c.dir]; // there will be some intersection, but that should be okay
	neck.expand_in_dim(dim1, -0.29*sz[dim1]); // smaller radius
	neck.expand_in_dim(dim2, -0.29*sz[dim2]); // smaller radius
	cube_t cap(neck);
	neck.d[dim][!c.dir] = cap.d[dim][c.dir] = c.d[dim][!c.dir] - dir_sign*0.08*length; // set cap thickness
	cap.expand_in_dim(dim1, -0.006*sz[dim1]); // slightly larger radius than narrow end of neck
	cap.expand_in_dim(dim2, -0.006*sz[dim2]); // slightly larger radius than narrow end of neck
	// draw as a sphere
	vector3d skip_hemi_dir(zero_vector);
	skip_hemi_dir[dim] = -dir_sign;
	mat.add_sphere_to_verts(sphere, color, 1, skip_hemi_dir); // low_detail=1
	mat.add_ortho_cylin_to_verts(body, color, dim, (add_bottom && !c.dir), (add_bottom && c.dir), 0, 0, 1.0, 1.0, 1.0, 1.0, 0, bottle_ndiv);
	// draw neck of bottle as a truncated cone; draw as two sided if empty
	mat.add_ortho_cylin_to_verts(neck, color, dim, 0, 0, is_empty, 0, (c.dir ? 0.85 : 1.0), (c.dir ? 1.0 : 0.85), 1.0, 1.0, 0, bottle_ndiv);

	if (!is_empty) { // draw cap if nonempty
		bool const draw_bot(c.was_expanded());
		mat.add_ortho_cylin_to_verts(cap, apply_light_color(c, cap_colors[bool(c.obj_id & 64)]), dim,
			(draw_bot || c.dir), (draw_bot || !c.dir), 0, 0, 1.0, 1.0, 1.0, 1.0, 0, bottle_ndiv);
	}
	// add the label
	// Note: we could add a bottom sphere to make it a capsule, then translate below the surface in -z to flatten the bottom
	body.expand_in_dim(dim1, 0.03*radius); // expand slightly in radius
	body.expand_in_dim(dim2, 0.03*radius); // expand slightly in radius
	body.d[dim][c.dir] += dir_sign*0.24*length; body.d[dim][!c.dir] -= dir_sign*0.12*length; // shrink in length
	bottle_params_t const &bp(bottle_params[c.get_bottle_type()]);
	float const tscale(bp.label_tscale); // some labels are more square and scaled 2x to repeat as they're more stretched out; should we use a partial cylinder instead?
	float const tscale_add(0.123*c.obj_id); // add a pseudo-random rotation to the label texture
	string const &texture_fn(bp.texture_fn); // select the custom label texture for each bottle type
	rgeom_mat_t &label_mat(get_material(tid_nm_pair_t(texture_fn.empty() ? -1 : get_texture_by_name(texture_fn)), 0, 0, 1));
	label_mat.add_ortho_cylin_to_verts(body, apply_light_color(c, WHITE), dim, 0, 0, 0, 0, 1.0, 1.0, tscale, 1.0, 0, bottle_ndiv, tscale_add); // draw label
}

void building_room_geom_t::add_paper(room_object_t const &c) {
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(c.get_paper_tid(), 0.0), 0, 0, 1)); // map texture to quad
	unsigned const qv_start(mat.quad_verts.size());
	mat.add_cube_to_verts(c, apply_light_color(c), zero_vector, ~EF_Z2, c.dim, (c.dim ^ c.dir ^ 1), c.dir); // unshadowed, top face only, with proper orient
	
	if (c.flags & RO_FLAG_RAND_ROT) { // add slight rotation to misalign the paper
		float const angle((PI/8.0)*(fract(123.456*c.obj_id) - 0.5));
		rotate_verts(mat.quad_verts, plus_z, angle, c.get_cube_center(), qv_start);
	}
}

void building_room_geom_t::add_pen_pencil_marker_to_material(room_object_t const &c_, rgeom_mat_t &mat) {
	room_object_t c(c_);
	unsigned const dim(get_max_dim(c.get_size()));
	if (dim != 2 && !c.dir) {swap(c.d[dim][0], c.d[dim][1]); c.dir = 1;} // put in canonical orientation if not vertical; okay if denormalized
	colorRGBA const color(apply_light_color(c));
	bool const is_pen(c.type == TYPE_PEN), is_marker(c.type == TYPE_MARKER);
	float const length(c.get_sz_dim(dim));
	cube_t body(c), point(c);
	body.d[dim][1] = point.d[dim][0] = c.d[dim][1] - (is_marker ? 0.25 : (is_pen ? 0.08 : 0.10))*length;
	// non-AA rotation/direction?

	if (is_marker) {
		mat.add_ortho_cylin_to_verts(body,  apply_light_color(c, WHITE), dim, 1, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 16); // 16-sided cylinder, always white
		mat.add_ortho_cylin_to_verts(point, color, dim, 0, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 16); // 16-sided cylinder (cap)
	}
	else if (is_pen) { // point is at the top
		mat.add_ortho_cylin_to_verts(body,  color, dim, 1, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 16); // 16-sided cylinder
		mat.add_ortho_cylin_to_verts(point, color, dim, 0, 0, 0, 0, 1.0, 0.2, 1.0, 1.0, 0, 16); // 16-sided truncated cone
	}
	else { // eraser is at the bottom and point is at the top
		colorRGBA const lt_wood(1.0, 0.8, 0.6);
		cube_t end_part(body), lead(point);
		point.d[dim][1] = lead    .d[dim][0] = c.d[dim][1] - 0.03*length;
		body .d[dim][0] = end_part.d[dim][1] = c.d[dim][0] + 0.09*length;
		cube_t eraser(end_part), metal(end_part);
		metal.d[dim][0] = eraser.d[dim][1] = c.d[dim][0] + 0.04*length;
		mat.add_ortho_cylin_to_verts(body,   color,                          dim, 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 6); // 6-sided cylinder, should really be made flat sided
		mat.add_ortho_cylin_to_verts(point,  apply_light_color(c, lt_wood),  dim, 0, 0, 0, 0, 1.0, 0.3, 1.0, 1.0, 0, 12); // 12-sided truncated cone
		mat.add_ortho_cylin_to_verts(lead,   apply_light_color(c, DK_GRAY),  dim, 0, 0, 0, 0, 0.3, 0.0, 1.0, 1.0, 0, 12); // 12-sided cone
		mat.add_ortho_cylin_to_verts(metal,  apply_light_color(c, DK_GREEN), dim, 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 12); // 12-sided cylinder
		mat.add_ortho_cylin_to_verts(eraser, apply_light_color(c, PINK),     dim, 1, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 12); // 12-sided cylinder
	}
}
void building_room_geom_t::add_pen_pencil_marker(room_object_t const &c) {
	add_pen_pencil_marker_to_material(c, get_untextured_material(0, 0, 1)); // unshadowed, small
}

int get_flooring_texture(room_object_t const &c) {
	switch (c.item_flags) { // select texture for flooring type
	case FLOORING_MARBLE:   return MARBLE_TEX;
	case FLOORING_TILE:     return get_texture_by_name("bathroom_tile.jpg");
	case FLOORING_CONCRETE: return get_concrete_tid();
	case FLOORING_CARPET:   return get_texture_by_name((c.obj_id & 1) ? "carpet/carpet1.jpg" : "carpet/carpet2.jpg"); // select between two textures
	case FLOORING_WOOD:     return ((c.obj_id & 1) ? (int)FENCE_TEX : (int)PANELING_TEX); // select between two textures
	default: assert(0);
	}
	return -1; // shouldn't get here
}
void building_room_geom_t::add_flooring(room_object_t const &c, float tscale) {
	get_material(tid_nm_pair_t(get_flooring_texture(c), 0.8*tscale)).add_cube_to_verts(c, apply_light_color(c), tex_origin, ~EF_Z2); // top face only, unshadowed
}

void building_room_geom_t::add_wall_trim(room_object_t const &c, bool for_closet) { // uses mats_detail
	rgeom_mat_t &mat(get_untextured_material(0, 0, (for_closet ? 1 : 2))); // inc_shadows=0, dynamic=0, small=2 (1 for closet)

	if (c.shape == SHAPE_ANGLED) { // single quad
		bool const draw_ends[2] = {bool(c.flags & RO_FLAG_ADJ_TOP), bool(c.flags & RO_FLAG_ADJ_BOT)}; // triangle end cap for closets
		point pts[4]; // {LLC, ULC, URC, LRC}
		pts[0][!c.dim] = pts[1][!c.dim] = c.d[!c.dim][0];
		pts[2][!c.dim] = pts[3][!c.dim] = c.d[!c.dim][1];
		pts[0][ c.dim] = pts[3][ c.dim] = c.d[ c.dim][!c.dir];
		pts[1][ c.dim] = pts[2][ c.dim] = c.d[ c.dim][ c.dir];
		pts[0].z = pts[3].z = c.z1();
		pts[1].z = pts[2].z = c.z2();
		rgeom_mat_t::vertex_t v;
		v.set_c4(c.color);

		for (unsigned d = 0; d < 2; ++d) { // draw end caps (before swapping vertex winding order)
			if (!draw_ends[d]) continue;
			v.set_ortho_norm(!c.dim, !d);
			point epts[3] = {pts[d ? 1:3], pts[d ? 0:2], (pts[d ? 0:3] + vector3d(0.0, 0.0, c.dz()))};
			if (c.dir ^ c.dim) {swap(epts[0], epts[1]);} // reverse the winding order

			for (unsigned i = 0; i < 3; ++i) {
				v.v = epts[i];
				mat.indices.push_back(mat.itri_verts.size());
				mat.itri_verts.push_back(v);
			}
		} // for d
		if (c.flags & RO_FLAG_ADJ_LO) {pts[0][!c.dim] += c.get_length();} // add closet outside corner bevel/miter
		if (c.flags & RO_FLAG_ADJ_HI) {pts[3][!c.dim] -= c.get_length();} // add closet outside corner bevel/miter
		if (c.dir ^ c.dim) {swap(pts[0], pts[3]); swap(pts[1], pts[2]);} // change winding order/normal sign
		v.set_norm(get_poly_norm(pts));
		float const tcs[2][4] = {{0,0,1,1}, {0,1,1,0}};

		for (unsigned n = 0; n < 4; ++n) {
			v.v = pts[n];
			v.t[0] = tcs[0][n];
			v.t[1] = tcs[1][n];
			mat.quad_verts.push_back(v);
		}
	}
	else { // cube
		unsigned skip_faces(0);
		if      (c.shape == SHAPE_TALL ) {skip_faces = 0;} // door/window side trim
		else if (c.shape == SHAPE_SHORT) {skip_faces = get_skip_mask_for_xy(!c.dim);} // door top trim: skip ends
		else                             {skip_faces = get_skip_mask_for_xy(!c.dim) | EF_Z1;} // wall trim: skip bottom surface and short sides
		if (c.flags & RO_FLAG_ADJ_LO) {skip_faces |= ~get_face_mask(c.dim, 0);}
		if (c.flags & RO_FLAG_ADJ_HI) {skip_faces |= ~get_face_mask(c.dim, 1);}
		skip_faces |= ((c.flags & RO_FLAG_ADJ_BOT) ? EF_Z1 : 0) | ((c.flags & RO_FLAG_ADJ_TOP) ? EF_Z2 : 0);
		mat.add_cube_to_verts_untextured(c, c.color, skip_faces); // is_small, untextured, no shadows
	}
}

void building_room_geom_t::add_blinds(room_object_t const &c) {
	bool const vertical(!c.is_hanging());
	colorRGBA const color(c.color); // room color not applied as it looks wrong when viewed from outside the building
	// fit the texture to the cube; blinds have a fixed number of slats that compress when they are shortened
	// should these be partially transparent/backlit like bathroom windows? I guess not, most blinds are plastic or wood rather than material
	int const nm_tid(get_texture_by_name("interiors/blinds_hn.jpg", 1, 0, 1, 8.0)); // use high aniso
	float tx(vertical ? 1.0/c.dz() : 0.0), ty(vertical ? 0.5/c.get_width() : 0.0);
	if (c.dim) {swap(tx, ty);}
	int const blinds_tid(get_blinds_tid());
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(blinds_tid, nm_tid, tx, ty), 1));
	unsigned df1(~get_skip_mask_for_xy(!c.dim)), df2(~EF_Z12);
	if (vertical) {swap(df1, df2);} // swap sides vs. top/bottom
	vector3d const llc(c.get_llc());
	mat.add_cube_to_verts(c, color, llc, ~get_skip_mask_for_xy(c.dim), (c.dim ^ vertical ^ 1)); // draw front and back faces
	mat.add_cube_to_verts(c, color, llc, df1, (c.dim ^ vertical)); // draw sides / top and bottom
	get_untextured_material(1).add_cube_to_verts_untextured(c, texture_color(blinds_tid).modulate_with(color), df2); // draw top and bottom / front and back untextured
}

void building_room_geom_t::add_fireplace(room_object_t const &c, float tscale) {
	float const dir_sign(c.dir ? -1.0 : 1.0), depth(c.get_depth()), width(c.get_width()), botz(c.z1() + 0.1*c.dz());
	float const face_pos(c.d[c.dim][!c.dir] - 0.4*dir_sign*depth); // front face pos
	cube_t base(c), front(c), bot(c), top(c);
	base .d[c.dim][!c.dir] = front.d[c.dim][c.dir] = face_pos;
	top  .d[c.dim][!c.dir] = face_pos + dir_sign*0.02*width;
	front.d[c.dim][!c.dir] = face_pos + 0.025*dir_sign*depth; // set front thickness
	front.expand_in_dim(!c.dim, -0.1*width); // shrink
	front.z1()  = bot.z2() = botz;
	front.z2() -= 0.2*c.dz();
	base.z2()   = top.z1() = c.z2() - 0.04*c.dz();
	bot.expand_in_dim(!c.dim, 0.01*width); // expand slightly
	top.expand_in_dim(!c.dim, 0.02*width); // expand slightly
	colorRGBA const color(apply_light_color(c));
	point const tex_origin(c.get_llc());
	int const fp_tid(get_texture_by_name("interiors/fireplace.jpg"));
	unsigned const skip_back_face(~get_face_mask(c.dim, c.dir));
	rgeom_mat_t &brick_mat(get_material(tid_nm_pair_t(BRICK2_TEX, 1.0*tscale, 1), 1)); // shadowed
	brick_mat.add_cube_to_verts(base, color, tex_origin, (skip_back_face | EF_Z1), !c.dim); // skip back and bottom faces
	rgeom_mat_t &front_mat(get_material(tid_nm_pair_t(fp_tid, 0.0, 0), 0)); // unshadowed
	front_mat.add_cube_to_verts(front, color, tex_origin, get_face_mask(c.dim, !c.dir), !c.dim, (c.dim ^ c.dir)); // front face only
	rgeom_mat_t &fside_mat(get_material(tid_nm_pair_t(fp_tid, 0.01, 0), 0)); // unshadowed, small TC so that we get only black area
	fside_mat.add_cube_to_verts(front, color, tex_origin, (get_skip_mask_for_xy(c.dim) | EF_Z1)); // sides only
	rgeom_mat_t &marble_mat(get_material(tid_nm_pair_t(MARBLE_TEX, 1.5*tscale, 1), 1));
	marble_mat.add_cube_to_verts(bot, color, tex_origin, (skip_back_face | EF_Z1)); // skip back and bottom faces
	marble_mat.add_cube_to_verts(top, color, tex_origin,  skip_back_face); // skip back face
}

float get_railing_height(room_object_t const &c) {
	bool const is_u_stairs(c.flags & (RO_FLAG_ADJ_LO | RO_FLAG_ADJ_HI));
	return (is_u_stairs ? 0.70 : 0.35)*c.dz(); // use a larger relative height for lo/hi railings on U-shaped stairs
}
cylinder_3dw get_railing_cylinder(room_object_t const &c) {
	float const radius(0.5*c.get_width()), center(c.get_center_dim(!c.dim)), height(get_railing_height(c));
	point p[2];

	for (unsigned d = 0; d < 2; ++d) {
		p[d].z = ((c.flags & RO_FLAG_TOS) ? c.z1() : c.d[2][d]) + height; // top railing is level, otherwise sloped
		p[d][!c.dim] = center;
		p[d][ c.dim] = c.d[c.dim][c.dir^bool(d)^1];
	}
	return cylinder_3dw(p[0], p[1], radius, radius);
}
void building_room_geom_t::add_railing(room_object_t const &c) {
	cylinder_3dw const railing(get_railing_cylinder(c));
	bool const is_u_stairs(c.flags & (RO_FLAG_ADJ_LO | RO_FLAG_ADJ_HI)), is_top_railing(c.flags & RO_FLAG_TOS), draw_ends(!(c.flags & RO_FLAG_ADJ_BOT));
	float const pole_radius(0.75*railing.r1), length(c.get_length()), height(get_railing_height(c));
	tid_nm_pair_t tex(-1, 1.0, 1); // shadowed
	tex.set_specular_color(c.color, 0.7, 70.0); // use a non-white metal specular color
	rgeom_mat_t &mat(get_material(tex, 1, 0, 1)); // inc_shadows=1, dynamic=0, small=1
	mat.add_cylin_to_verts(railing.p1, railing.p2, railing.r1, railing.r2, c.color, draw_ends, draw_ends); // draw sloped railing

	if (!is_u_stairs && !(c.flags & RO_FLAG_ADJ_TOP)) {
		for (unsigned d = 0; d < 2; ++d) { // add the two vertical poles
			point pt(d ? railing.p2 : railing.p1);
			float shift_len(pole_radius);
			if (!is_top_railing) {max_eq(shift_len, 0.01f*length);}
			pt[c.dim] += ((c.dir ^ bool(d)) ? 1.0 : -1.0)*shift_len; // shift slightly inward toward the center
			float const hscale((d && !is_top_railing) ? 1.25 : 1.0); // shorten for lower end, which rests on the step (unless top railing)
			point const p1(pt - vector3d(0, 0, hscale*height)), p2(pt - vector3d(0, 0, (is_top_railing ? 0.0 : 0.02*(d ? 1.0 : -1.0)*height)));
			mat.add_cylin_to_verts(p1, p2, pole_radius, pole_radius, c.color, 0, 0); // no top or bottom
		}
		if (c.is_open()) { // add balusters
			unsigned const num(NUM_STAIRS_PER_FLOOR - 1);
			float const step_sz(1.0/(num+1)), radius(0.75*pole_radius), bot_radius(0.85*pole_radius);
			vector3d const delta(0, 0, -height);

			for (unsigned n = 0; n < num; ++n) {
				float const t((n+1)*step_sz);
				point const pt(t*railing.p1 + (1.0 - t)*railing.p2);
				mat.add_cylin_to_verts((pt + delta), pt, radius, radius, c.color, 0, 0, 0, 0, 1.0, 1.0, 0, 16); // only 16 sides, no top or bottom
			}
			mat.add_cylin_to_verts((railing.p1 + delta), (railing.p2 + delta), bot_radius, bot_radius, c.color, 1, 1); // bottom bar with both ends
		}
	}
}

void building_room_geom_t::add_stair(room_object_t const &c, float tscale, vector3d const &tex_origin) { // Note: no room lighting color atten
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(MARBLE_TEX, 1.5*tscale), 1));
	float const width(c.get_width()); // use width as a size reference because this is constant for a set of stairs and in a relative small range
	cube_t top(c), bot(c);
	bot.z2() = top.z1() = c.z2() - min(0.025*width, 0.25*c.dz()); // set top thickness
	top.d[c.dim][!c.dir] += (c.dir ? -1.0 : 1.0)*0.0125*width; // extension
	top.expand_in_dim(!c.dim, 0.01*width); // make slightly wider
	mat.add_cube_to_verts(top, STAIRS_COLOR_TOP, tex_origin); // all faces drawn
	mat.add_cube_to_verts(bot, STAIRS_COLOR_BOT, tex_origin, EF_Z2); // skip top face
}

void building_room_geom_t::add_stairs_wall(room_object_t const &c, vector3d const &tex_origin, tid_nm_pair_t const &wall_tex) {
	get_material(get_scaled_wall_tex(wall_tex), 1).add_cube_to_verts(c, c.color, tex_origin, EF_Z1); // skip bottom; no room lighting color atten
}
void building_room_geom_t::add_parking_garage_wall(room_object_t const &c, vector3d const &tex_origin, tid_nm_pair_t const &wall_tex) {
	// small=2: drawn as detail object; no room lighting color atten
	if      (c.item_flags == 0) {get_material(get_scaled_wall_tex(wall_tex), 1, 0, 2).add_cube_to_verts(c, c.color, tex_origin, EF_Z12);} // wall
	else if (c.item_flags == 1) {get_material(tid_nm_pair_t(get_concrete_tid(), wall_tex.tscale_x, 1), 1, 0, 2).add_cube_to_verts(c, c.color, all_zeros, EF_Z12);} // pillar
	else if (c.item_flags == 2) {get_material(tid_nm_pair_t(get_concrete_tid(), wall_tex.tscale_x, 0), 0, 0, 2).add_cube_to_verts(c, c.color, all_zeros, EF_Z2 );} // beam
	else {assert(0);}
}
void building_room_geom_t::add_parking_space(room_object_t const &c, vector3d const &tex_origin, float tscale) {
	float const space_width(c.get_width()), line_width(0.04*space_width);
	cube_t yellow_line(c);
	yellow_line.d[!c.dim][1] -= (space_width - line_width); // shrink to line by moving the left edge
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_texture_by_name("roads/concrete_stripe.jpg"), 0.0), 0, 0, 2)); // inc_shadows=0
	mat.add_cube_to_verts(yellow_line, c.color, yellow_line.get_llc(), ~EF_Z2, c.dim); // small=2: top surface only

	if (!(c.flags & RO_FLAG_ADJ_HI)) { // last space in the row, add the rightmost yellow line
		yellow_line.translate_dim(!c.dim, (space_width - line_width));
		mat.add_cube_to_verts(yellow_line, c.color, yellow_line.get_llc(), ~EF_Z2, c.dim); // small=2: top surface only
	}
	if (c.flags & RO_FLAG_IS_ACTIVE) { // handicap spot
		cube_t icon(c.get_cube_center());
		icon.expand_by_xy(0.25*space_width);
		set_cube_zvals(icon, c.z2(), c.z2()+0.002*space_width);
		rgeom_mat_t &mat2(get_material(tid_nm_pair_t(get_texture_by_name("roads/handicap_parking.jpg"), 0.0), 0, 0, 2)); // inc_shadows=0
		mat2.add_cube_to_verts(icon, c.color, icon.get_llc(), ~EF_Z2, c.dim, (c.dim ^ c.dir), c.dir^1); // small=2: top surface only
	}
}

void building_room_geom_t::add_pg_ramp(room_object_t const &c, vector3d const &tex_origin, float tscale) {
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_concrete_tid(), tscale, 1), 1, 0, 2));
	//mat.add_cube_to_verts(c, WHITE, all_zeros); // TESTING
	tquad_t ramp(4); // ramp surface
	float const length(c.get_length()), thickness(FLOOR_THICK_VAL_OFFICE*c.dz()), side_tc_y(thickness/length);
	float const zv[2] = {c.z1(), c.z2()};
	// dim dir z0 z1 z2 z3
	// 0   0   1  0  0  1
	// 0   1   0  1  1  0
	// 1   0   1  1  0  0
	// 1   1   0  0  1  1
	ramp.pts[0].assign(c.x1(), c.y1(), zv[!c.dir]); // LL
	ramp.pts[1].assign(c.x2(), c.y1(), zv[c.dim ^ c.dir]); // LR
	ramp.pts[2].assign(c.x2(), c.y2(), zv[c.dir]); // UR
	ramp.pts[3].assign(c.x1(), c.y2(), zv[c.dim ^ c.dir ^ 1]); // UL
	vector3d const normal(ramp.get_norm());
	auto &verts(mat.quad_verts);
	rgeom_mat_t::vertex_t v;
	v.set_c4(c.color); // no room lighting color atten
	v.set_norm(normal);

	for (unsigned tb = 0; tb < 2; ++tb) { // {top, bottom}
		for (unsigned i = 0; i < 4; ++i) {
			v.v    = ramp.pts[tb ? (3-i) : i]; // swap winding order for bottom surface
			v.t[0] = 2.0*float(v.v.x == c.x2()); // stretch texture 2x in length
			v.t[1] = 2.0*float(v.v.y == c.y2()); // stretch texture 2x in length
			verts.push_back(v);
			if (tb) {verts.back().v.z -= thickness;} // extrude thickness for bottom surface
		}
		if (tb == 0) {v.invert_normal();}
	} // for tb
	for (unsigned s = 0; s < 4; ++s) { // sides: {-y, +x, +y, -x}
		point const pts[2] = {ramp.pts[s], ramp.pts[(s+1)&3]};
		v.set_ortho_norm((s&1)^1, (s&1)^(s>>1)); // {1,0,1,0}, {0,1,1,0}

		for (unsigned i = 0; i < 4; ++i) {
			unsigned const ix(3-i); // use correct winding order
			bool const tb(ix >> 1), lr(tb ^ (ix&1));
			v.v    = pts[lr];
			v.t[0] = float(lr);
			v.t[1] = float(tb)*side_tc_y;
			if (tb) {v.v.z -= thickness;} // extrude thickness for bottom surface
			verts.push_back(v);
		}
	} // for s
}

void building_room_geom_t::add_pipe(room_object_t const &c) { // should be SHAPE_CYLIN
	unsigned const dim(c.dir ? 2 : unsigned(c.dim)); // encoded as: X:dim=0,dir=0 Y:dim=1,dir=0, Z:dim=x,dir=1
	float const radius(0.5*c.get_sz_dim((dim+1)%3));
	//assert(0.5*c.get_sz_dim((dim+2)%3) == radius); // must be a square cross section, but too strong due to FP error
	// only vertical pipes cast shadows; horizontal ceiling pipes are too high and outside the ceiling light shadow map,
	// or otherwise don't look correct when an area light is treated as a point light
	bool const is_duct(c.type == TYPE_DUCT);
	// Note: attic ducts have the attic flag set, which is aliased as the hanging flag, so we have to disable flat ends for ducts
	bool const flat_ends(!is_duct && c.is_hanging()), shadowed(is_duct || (c.flags & RO_FLAG_LIT)); // RO_FLAG_LIT flag is interpreted as "casts shadows"
	// adj flags indicate adjacencies where we draw joints connecting to other pipe sections
	bool const draw_joints[2] = {((c.flags & RO_FLAG_ADJ_LO) != 0), ((c.flags & RO_FLAG_ADJ_HI) != 0)};
	colorRGBA const color(apply_light_color(c));
	colorRGBA const spec_color(is_known_metal_color(c.color) ? c.color : WHITE); // special case metals
	bool const is_bolt(c.flags & RO_FLAG_RAND_ROT); // use RO_FLAG_RAND_ROT to indicate this is a bolt on the pipe rather than a pipe section
	unsigned const ndiv(is_bolt ? 6 : N_CYL_SIDES);
	float const side_tscale(is_bolt ? 0.0 : 1.0); // bolts are untextured
	float const len_tscale(is_duct ? 0.1*c.get_length()/radius : 1.0);
	// draw sides and possibly one or both ends
	tid_nm_pair_t tex((is_duct ? get_cylin_duct_tid() : -1), 1.0, shadowed); // custom specular color
	tex.set_specular_color(spec_color, 0.8, 60.0);
	rgeom_mat_t &mat(get_material(tex, shadowed, 0, 2)); // detail object
	// swap texture XY for ducts
	mat.add_ortho_cylin_to_verts(c, color, dim, (flat_ends && draw_joints[0]), (flat_ends && draw_joints[1]),
		0, 0, 1.0, 1.0, side_tscale, 1.0, 0, ndiv, 0.0, is_duct, len_tscale);
	if (flat_ends) return; // done

	for (unsigned d = 0; d < 2; ++d) {
		if (!draw_joints[d]) continue;
		point center(c.get_cube_center());
		center[dim] = c.d[dim][d]; // move to one end along the cylinder
		mat.add_sphere_to_verts(center, vector3d(radius, radius, radius), color);
	}
}

void building_room_geom_t::add_duct(room_object_t const &c) {
	unsigned const dim(c.dir ? 2 : unsigned(c.dim)); // encoded as: X:dim=0,dir=0 Y:dim=1,dir=0, Z:dim=x,dir=1 (same as pipes)

	if (c.shape == SHAPE_CUBE) {
		unsigned skip_faces(0);
		if (c.flags & RO_FLAG_ADJ_BOT) {skip_faces |= EF_Z1;}
		if (c.flags & RO_FLAG_ADJ_TOP) {skip_faces |= EF_Z2;}
		if (c.flags & RO_FLAG_ADJ_LO ) {skip_faces |= ~get_face_mask(dim, 0);}
		if (c.flags & RO_FLAG_ADJ_HI ) {skip_faces |= ~get_face_mask(dim, 1);}
		// texture is 2 panels wide x 1 panel tall; for mapping of tiles duct sides we need tscale_x to be a multiple of 0.5 and tscale_y to be a multiple of 1.0
		unsigned const w1((dim+1)%3), w2((dim+2)%3); // the two width dimensions
		float const width1(c.get_sz_dim(w1)), width2(c.get_sz_dim(w2)), avg_width(0.5*(width1 + width2)); // maps to texture y
		// each panel should be approximately square, so one tile should be half the length in x (since texture is 2x tiles wide)
		float const tile_len(4.0*0.5*avg_width), length(c.get_sz_dim(dim)); // multiply by 4.0 for a 4:1 aspect ratio
		float const num_tiles(max(1, round_fp(length/tile_len))), tile_len_mod(length/num_tiles); // number of tiles must be a nonzero whole number
		unsigned const dim_masks[3] = {EF_X12, EF_Y12, EF_Z12};
		float tscales[3] = {}; // {X,Y,Z}
		tscales[dim] = 1.0/tile_len_mod;
		tscales[w1 ] = 1.0/width1;
		tscales[w2 ] = 1.0/width2;
		tid_nm_pair_t tex(get_cube_duct_tid(), -1, 0.0, 0.0, 1); // shadowed
		tex.set_specular_color(WHITE, 0.8, 60.0); // set metal specular

		// each face must be drawn with a different texture scale, so three cubes drawn
		for (unsigned d = 0; d < 3; ++d) { // d is the face dim
			unsigned const face_sf(skip_faces | ~dim_masks[d]); // skip faces in the wrong dims
			if ((face_sf & EF_ALL) == EF_ALL) continue; // all faces skipped for this dim
			unsigned const d1((d+1)%3), d2((d+2)%3);
			tex.tscale_x = tscales[d2];
			tex.tscale_y = tscales[d1];
			bool const swap_st(d2 != dim);
			(swap_st ? tex.tscale_y : tex.tscale_x) *= 0.5; // account for the 2x texture repetition in X
			get_material(tex, 1, 0, 2).add_cube_to_verts(c, c.color, c.get_llc(), face_sf, swap_st); // shadowed, detail, not using lit color
		} // for d
	}
	else if (c.shape == SHAPE_CYLIN) {add_pipe(c);} // draw using pipe logic
	else {assert(0);} // unsupported shape
}

void building_room_geom_t::add_curb(room_object_t const &c) {
	float const tscale(1.0/c.get_length());
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_concrete_tid(), tscale, 1), 1, 0, 2)); // shadowed, detail object
	colorRGBA const color(apply_light_color(c));
	cube_t t(c);
	t.expand_by_xy(-0.25*c.get_width()); // pinch the top face into a trapezoid
	mat.add_cube_to_verts(t, color, c.get_llc(), ~EF_Z2); // top surface only
	point const bot_pts[4] = {point(c.x1(), c.y1(), c.z1()), point(c.x2(), c.y1(), c.z1()), point(c.x2(), c.y2(), c.z1()), point(c.x1(), c.y2(), c.z1())};
	point const top_pts[4] = {point(t.x1(), t.y1(), t.z2()), point(t.x2(), t.y1(), t.z2()), point(t.x2(), t.y2(), t.z2()), point(t.x1(), t.y2(), t.z2())};
	rgeom_mat_t::vertex_t v;
	v.set_c4(color);

	for (unsigned s = 0; s < 4; ++s) { // sides: {-y, +x, +y, -x}
		tquad_t tq(4);
		tq.pts[0] = bot_pts[s];
		tq.pts[1] = bot_pts[(s+1)&3];
		tq.pts[2] = top_pts[(s+1)&3];
		tq.pts[3] = top_pts[s];
		auto &verts(mat.quad_verts);
		v.set_norm(tq.get_norm());

		for (unsigned i = 0; i < 4; ++i) {
			v.v    = tq.pts[i];
			v.t[0] = tscale*(v.v[s&1] - c.d[s&1][0]); // scales with horizontal position
			v.t[1] = tscale*(v.v.z - c.z1()); // scales with vertical position
			verts.push_back(v);
		}
	} // for s
}

void building_room_geom_t::add_breaker_panel(room_object_t const &c) {
	colorRGBA const color(apply_light_color(c));
	rgeom_mat_t &mat(get_metal_material(1, 0, 1)); // untextured, shadowed, small=1
	// skip back face, which is against the wall; skip front face when open because it's recessed
	unsigned const box_skip_faces(c.is_open() ? get_skip_mask_for_xy(c.dim) : ~get_face_mask(c.dim, c.dir));
	mat.add_cube_to_verts(c, color, all_zeros, box_skip_faces);

	if (c.is_open()) {
		// draw inside face and inside edges of open box
		float const box_width(c.get_width()), box_depth(c.get_length()), thickness(0.25*box_depth), dir_sign(c.dir ? -1.0 : 1.0);
		unsigned const front_face_mask(get_face_mask(c.dim, !c.dir));
		cube_t box(c);
		box.d[c.dim][!c.dir] -= dir_sign*thickness; // expand in to recess
		mat.add_cube_to_verts(box, color, all_zeros, front_face_mask);
		mat.add_cube_to_verts(c,   color, all_zeros, box_skip_faces, 0, 0, 0, 1); // inverted=1
		// draw open door
		bool const open_dir(c.obj_id & 1);
		unsigned const door_face_mask(~get_face_mask(!c.dim, open_dir)); // skip inside face
		cube_t door(box);
		door.d[!c.dim][ open_dir]  = c.d[!c.dim][!open_dir]; // shrink to zero area
		door.d[!c.dim][!open_dir] += (open_dir ? -1.0 : 1.0)*thickness; // shift outward by thickness
		door.d[ c.dim][ c.dir   ]  = door.d[ c.dim][!c.dir]; // front of box
		door.d[ c.dim][!c.dir   ] += dir_sign*(box_width - box_depth); // expand outward, subtract depth to make it artificially shorter
		mat.add_cube_to_verts(door, color, all_zeros, door_face_mask); // outside
		mat.add_cube_to_verts(door, color, all_zeros, door_face_mask, 0, 0, 0, 1); // inside, inverted=1
	}
}

// Note: there is a lot duplicated with building_room_geom_t::add_elevator(), but we need a separate function for adding interior elevator buttons
cube_t get_elevator_car_panel(room_object_t const &c, float fc_thick_scale) {
	float const dz(c.dz()), thickness(fc_thick_scale*dz), signed_thickness((c.dir ? 1.0 : -1.0)*thickness);
	float const width(c.get_width()), frame_width(0.2*width), panel_width(min(0.9f*frame_width, 0.25f*dz)), front_face(c.d[c.dim][c.dir] - signed_thickness);
	cube_t panel(c);
	panel.d[c.dim][ c.dir] = front_face; // flush front inner wall
	panel.d[c.dim][!c.dir] = front_face - 0.1f*signed_thickness; // set panel thickness
	panel.d[!c.dim][0] = c.d[!c.dim][0] + 0.5f*(frame_width - panel_width) + thickness; // edge near the wall
	panel.d[!c.dim][1] = panel.d[!c.dim][0] + panel_width - thickness; // edge near door
	panel.z1() += 0.28*dz; panel.z2() -= 0.28*dz;
	return panel;
}
void building_room_geom_t::add_elevator(room_object_t const &c, elevator_t const &e, float tscale, float fc_thick_scale,
	unsigned floor_offset, float floor_spacing, bool has_parking_garage, bool is_powered) // dynamic=1
{
	// elevator car, all materials are dynamic; no lighting scale
	float const dz(c.dz()), thickness(fc_thick_scale*dz), dir_sign(c.dir ? 1.0 : -1.0), signed_thickness(dir_sign*thickness);
	cube_t floor(c), ceil(c), back(c);
	floor.z2() = floor.z1() + thickness;
	ceil. z1() = ceil. z2() - thickness;
	floor.expand_by_xy(-0.5f*thickness);
	ceil .expand_by_xy(-0.5f*thickness);
	back.d[c.dim][c.dir] = c.d[c.dim][!c.dir] + signed_thickness;
	point const tex_origin(c.get_llc());
	unsigned const front_face_mask(get_face_mask(c.dim, c.dir)), back_face_mask(get_face_mask(c.dim, !c.dir));
	unsigned const floor_ceil_face_mask(front_face_mask & (EF_X12 | EF_Y12)); // +Z faces
	tid_nm_pair_t const paneling(get_tex_auto_nm(PANELING_TEX, 2.0f*tscale));
	get_material(get_tex_auto_nm(TILE_TEX, tscale), 1, 1).add_cube_to_verts(floor, WHITE, tex_origin, floor_ceil_face_mask);
	get_material(get_tex_auto_nm(get_rect_panel_tid(), tscale), 1, 1).add_cube_to_verts(ceil, WHITE, tex_origin, floor_ceil_face_mask);
	rgeom_mat_t &paneling_mat(get_material(paneling, 1, 1));
	paneling_mat.add_cube_to_verts(back, WHITE, tex_origin, front_face_mask, !c.dim);
	float const width(c.get_width()), frame_width(0.2*width), spacing(0.02*width), front_face(c.d[c.dim][c.dir] - signed_thickness);
	cube_t front(c);
	front.d[c.dim][ c.dir] -= (c.dir ? 1.0 : -1.0)*spacing; // slight gap with elevator doors
	front.d[c.dim][!c.dir]  = front_face;

	for (unsigned d = 0; d < 2; ++d) {
		// side walls
		unsigned const side_skip_faces(get_face_mask(!c.dim, !d));
		cube_t side(c);
		side.d[!c.dim][!d] = c.d[!c.dim][d] + (d ? -1.0 : 1.0)*thickness;
		paneling_mat.add_cube_to_verts(side,  WHITE, tex_origin, side_skip_faces, c.dim);
		// front sides of doors
		front.d[!c.dim][ d] = side.d[!c.dim][!d];
		front.d[!c.dim][!d] = c   .d[!c.dim][ d] + (d ? -1.0 : 1.0)*frame_width;
		paneling_mat.add_cube_to_verts(front, WHITE, tex_origin, (back_face_mask & side_skip_faces), !c.dim); // draw front and inside side
	}
	// add button panel
	cube_t const panel(get_elevator_car_panel(c, fc_thick_scale));
	get_untextured_material(0, 1).add_cube_to_verts_untextured(panel, DK_GRAY, ~front_face_mask);
	// add floor numbers to either the panel (or the buttons themselves?)
	unsigned const num_floors(c.drawer_flags), cur_floor(c.item_flags);
	assert(num_floors > 1);
	assert(num_floors >= floor_offset); // no sub-basement only elevators
	bool const use_small_text(floor_offset > 1 || (num_floors - floor_offset) >= 20); // need more space for two non-1 digits (B2 | 20)
	float const button_spacing(panel.dz()/(num_floors + 1)); // add extra spacing on bottom and top of panel
	float const panel_width(panel.get_sz_dim(!c.dim));
	float const inner_button_radius(min(0.6f*thickness, min(0.35f*button_spacing, 0.25f*panel.get_sz_dim(!c.dim)))); // approx match to elevator
	float text_height(min(0.5f*panel_width, 0.8f*button_spacing));
	bool const ldir(c.dim ^ c.dir);
	point text_pos;
	text_pos[ c.dim] = panel.d[c.dim][!c.dir] - 0.01*signed_thickness; // slightly in front of the panel
	text_pos[!c.dim] = max((panel.d[!c.dim][0] + 0.05f*panel_width), (panel.get_center_dim(!c.dim) - (1.5f*inner_button_radius + text_height))) +
		0.6f*ldir*text_height; // shift by approx width of font character(s) because we're aligning to the right side rather than the left
	vector3d col_dir(zero_vector), normal(zero_vector);
	col_dir[!c.dim] = (ldir ? -1.0 : 1.0);
	normal [ c.dim] = -dir_sign; // opposite dir from front of elevator
	static vector<vert_tc_t> verts;
	static ostringstream oss; // reused across buttons
	tid_nm_pair_t tp(FONT_TEXTURE_ID), lit_tp(tp);
	lit_tp.emissive = 1.0;
	get_material(lit_tp, 0, 1); // make sure it's allocated
	rgeom_mat_t &mat(get_material(tp, 0, 1)); // unshadowed, dynamic=1
	color_wrapper const cw(BLACK), lit_cw(colorRGBA(1.0, 0.9, 0.5));
	norm_comp const nc(normal);
	if (use_small_text) {text_height *= 0.67;} // shrink text if there are two wide digits, but leave text alignment unchanged
	// setup for exterior floor display
	float const ext_text_height(0.5f*panel_width), center_panel_height(0.7*panel_width), up_down_height(0.4f*panel_width), up_down_text_height(0.6f*panel_width);
	cube_t display(panel);
	display.expand_in_dim(!c.dim, -0.2*panel_width);
	display.d[c.dim][0] = display.d[c.dim][1] = c.d[c.dim][c.dir]; // front face of elevator ext wall
	display.d[c.dim][c.dir] += (c.dir ? 1.0 : -1.0)*0.01*panel_width;
	point ext_text_pos;
	ext_text_pos[ c.dim] = display.d[ c.dim][c.dir] + 0.01*signed_thickness; // slightly in front of the display
	ext_text_pos[!c.dim] = display.d[!c.dim][0] + 0.1*panel_width; // starting point of the text
	point up_down_pos(ext_text_pos);
	
	if (!ldir) { // mirror pos to the other side if needed
		bool const two_digits(num_floors > 9 || floor_offset > 0); // double digits or basement/parking garage
		ext_text_pos[!c.dim] += (two_digits ? 0.77f : 0.6f)*ext_text_height;
		up_down_pos [!c.dim] -= 0.15f*up_down_text_height;
	}
	else {
		up_down_pos [!c.dim] += 0.8f*up_down_text_height;
	}
	for (unsigned f = 0; f < num_floors; ++f) { // Note: floor number starts at 1 even if the elevator doesn't extend to the ground floor
		bool const is_lit(is_powered && f == cur_floor);
		text_pos.z = panel.z1() + (f + 1)*button_spacing - 0.5*text_height;
		verts.clear();
		add_floor_number((f+1), floor_offset, has_parking_garage, oss);
		gen_text_verts(verts, text_pos, oss.str(), 1000.0*text_height, col_dir, plus_z, 1); // use_quads=1
		assert(!verts.empty());
		bool const need_swap(dot_product(normal, cross_product((verts[1].v - verts[0].v), (verts[2].v - verts[1].v))) < 0.0);
		if (need_swap) {std::reverse(verts.begin(), verts.end());} // swap vertex winding order
		rgeom_mat_t &cur_mat(is_lit ? get_material(lit_tp, 0, 1) : mat);
		for (auto i = verts.begin(); i != verts.end(); ++i) {cur_mat.quad_verts.emplace_back(i->v, nc, i->t[0], i->t[1], (is_lit ? lit_cw : cw));}
		// add floor indicator lights and up/down lights outside elevators on each floor
		float const zval(e.z1() + (f + 0.7)*floor_spacing);
		set_cube_zvals(display, (zval - up_down_height), (zval + up_down_height + center_panel_height));
		get_untextured_material(0, 1).add_cube_to_verts_untextured(display, DK_GRAY, ~back_face_mask); // exterior display panel
		// add floor text
		ext_text_pos.z = zval + 0.1*panel_width;
		verts.clear();
		add_floor_number((cur_floor+1), floor_offset, has_parking_garage, oss);
		gen_text_verts(verts, ext_text_pos, oss.str(), 1000.0*ext_text_height, -col_dir, plus_z, 1); // use_quads=1
		if (need_swap) {std::reverse(verts.begin(), verts.end());} // swap vertex winding order
		rgeom_mat_t &cur_ext_mat(is_powered ? get_material(lit_tp, 0, 1) : mat); // lit, as long as the elevator is powered
		for (auto i = verts.begin(); i != verts.end(); ++i) {cur_ext_mat.quad_verts.emplace_back(i->v, nc, i->t[0], i->t[1], (is_powered ? lit_cw : cw));}
		
		// add up/down indicators
		for (unsigned d = 0; d < 2; ++d) { // {down, up}
			up_down_pos.z = zval + (d ? center_panel_height : -0.75*up_down_height);
			verts.clear();
			gen_text_verts(verts, up_down_pos, (d ? ">" : "<"), 1000.0*up_down_text_height, plus_z, col_dir, 1); // R90, use_quads=1
			if (need_swap) {std::reverse(verts.begin(), verts.end());} // swap vertex winding order
			bool const is_lit(bool(d) == e.going_up && e.is_moving());
			rgeom_mat_t &cur_ud_mat(is_lit ? get_material(lit_tp, 0, 1) : mat); // lit, as long as the elevator is powered
			for (auto i = verts.begin(); i != verts.end(); ++i) {cur_ud_mat.quad_verts.emplace_back(i->v, nc, i->t[0], i->t[1], (is_lit ? lit_cw : cw));}
		} // for d
	} // for f
}

void building_room_geom_t::add_elevator_doors(elevator_t const &e, float fc_thick_scale) {
	float const spacing(e.get_wall_thickness()), closed_door_width(0.99*0.5*e.get_sz_dim(!e.dim)), open_door_width(1.12*e.get_frame_width());
	rgeom_mat_t &mat(get_untextured_material(1, 1));
	float open_z1(e.z1()), open_z2(e.z2());

	if (e.open_amt > 0.0) { // only draw the doors as open for the floor the elevator car is on
		assert(e.car_obj_id < objs.size());
		room_object_t const &car(objs[e.car_obj_id]); // elevator car for this elevator
		float const z_shrink(0.8*fc_thick_scale*car.dz()); // shrink slightly to avoid clipping through the ceiling and floor
		open_z1 = car.z1() + z_shrink; open_z2 = car.z2() - z_shrink;
		assert(open_z1 < open_z2);
	}
	for (unsigned d = 0; d < 2; ++d) { // left/right doors, untextured for now
		unsigned const skip_faces(((e.open_amt > 0.0) ? 0 : EF_Z12) | ~get_face_mask(!e.dim, !d)); // skip top and bottom if fully closed
		cube_t door(e);
		door.d[e.dim][!e.dir] = door.d[e.dim][e.dir] + (e.dir ? -1.0f : 1.0f)*spacing; // set correct thickness
		door.expand_in_dim(e.dim, -0.2*spacing); // shrink slightly to make thinner
		door.d[!e.dim][d] = e.d[!e.dim][!d] + (d ? 1.0f : -1.0f)*closed_door_width; // this section is always closed

		if (e.open_amt > 0.0) { // only draw the doors as open for the floor the elevator car is on
			cube_t open_door(door);
			open_door.d[!e.dim][d] += (d ? 1.0f : -1.0f)*(open_door_width - closed_door_width)*e.open_amt;
			open_door.z1() = door.z2() = open_z1; open_door.z2() = open_z2;
			mat.add_cube_to_verts_untextured(open_door, GRAY, skip_faces); // open part
			if (door.dz() > 0.0) {mat.add_cube_to_verts_untextured(door, GRAY, skip_faces);} // bottom part
			door.z1() = open_z2; door.z2() = e.z2(); // top part
		}
		if (door.dz() > 0.0) {mat.add_cube_to_verts_untextured(door, GRAY, skip_faces);} // all or top part
	} // for d
}

void building_room_geom_t::add_light(room_object_t const &c, float tscale) {
	// Note: need to use a different texture (or -1) for is_on because emissive flag alone does not cause a material change
	bool const is_on(c.is_light_on() && !(c.is_broken() && !c.is_open()));
	tid_nm_pair_t tp(((is_on || c.shape == SHAPE_SPHERE) ? (int)WHITE_TEX : (int)PLASTER_TEX), tscale);
	tp.emissive = (is_on ? 1.0 : 0.0);
	rgeom_mat_t &mat(mats_lights.get_material(tp, 0)); // no shadows
	if      (c.shape == SHAPE_CUBE  ) {mat.add_cube_to_verts  (c, c.color, c.get_llc(), EF_Z2);} // sometimes textured, skip top face
	else if (c.shape == SHAPE_CYLIN ) {mat.add_vcylin_to_verts(c, c.color, 1, 0);} // bottom only
	else if (c.shape == SHAPE_SPHERE) {mat.add_sphere_to_verts(c, c.color);}
	else {assert(0);}
}

void building_room_geom_t::add_rug(room_object_t const &c) {
	bool const swap_tex_st(c.dy() < c.dx()); // rug textures are oriented with the long side in X, so swap the coordinates (rotate 90 degrees) if our rug is oriented the other way
	get_material(tid_nm_pair_t(c.get_rug_tid(), 0.0)).add_cube_to_verts(c, c.color, c.get_llc(), 61, swap_tex_st); // only draw top/+z face
}

void building_room_geom_t::add_picture(room_object_t const &c) { // also whiteboards; not affected by room color
	bool const whiteboard(c.type == TYPE_WBOARD);
	int picture_tid(WHITE_TEX);

	if (!whiteboard && c.taken_level == 0) { // picture, not taken/frame only
		int const user_tid(get_rand_screenshot_texture(c.obj_id));
		picture_tid  = ((user_tid >= 0) ? (unsigned)user_tid : c.get_picture_tid()); // if user texture is valid, use that instead
		num_pic_tids = get_num_screenshot_tids();
		has_pictures = 1;
	}
	unsigned skip_faces(get_face_mask(c.dim, c.dir)); // only the face oriented outward
	bool const mirror_x(!whiteboard && !(c.dim ^ c.dir));
	point const tex_origin(c.get_llc());
	get_untextured_material(); // ensure frame material is valid
	rgeom_mat_t &picture_mat(get_material(tid_nm_pair_t(picture_tid, 0.0)));
	unsigned const picture_qv_start(picture_mat.quad_verts.size());
	picture_mat.add_cube_to_verts(c, c.color, tex_origin, skip_faces, !c.dim, mirror_x);
	// add a frame
	cube_t frame(c);
	vector3d exp;
	exp.z = exp[!c.dim] = (whiteboard ? 0.04 : 0.06)*c.dz(); // frame width
	exp[c.dim] = (whiteboard ? -0.1 : -0.25)*c.get_depth(); // shrink in this dim
	frame.expand_by(exp);
	rgeom_mat_t &frame_mat(get_untextured_material());
	unsigned const frame_qv_start(frame_mat.quad_verts.size());
	frame_mat.add_cube_to_verts_untextured(frame, (whiteboard ? GRAY : BLACK), skip_faces);
	
	if (whiteboard) { // add a marker ledge
		cube_t ledge(c);
		ledge.z2() = ledge.z1() + 0.016*c.dz(); // along the bottom edge
		ledge.d[c.dim][c.dir] += (c.dir ? 1.5 : -1.5)*c.get_depth(); // extrude outward
		get_untextured_material(1).add_cube_to_verts_untextured(ledge, GRAY, (1 << (2*(2-c.dim) + !c.dir))); // shadowed
	}
	else if (c.flags & RO_FLAG_RAND_ROT) { // apply a random rotation
		float const angle(0.2f*(fract(PI*c.obj_id + 1.61803f*c.item_flags) - 0.5f)); // random rotation based on obj_id and item flags
		point rotate_pt(c.get_cube_center());
		rotate_pt.z += 0.45*c.dz(); // rotate about a point near the top of the picture
		vector3d normal(zero_vector);
		normal[c.dim] = (c.dir ? -1.0 : 1.0);
		rotate_verts(picture_mat.quad_verts, normal, angle, rotate_pt, picture_qv_start);
		rotate_verts(frame_mat  .quad_verts, normal, angle, rotate_pt, frame_qv_start  );
	}
}

void building_room_geom_t::add_book_title(string const &title, cube_t const &title_area, rgeom_mat_t &mat, colorRGBA const &color,
	unsigned hdim, unsigned tdim, unsigned wdim, bool cdir, bool ldir, bool wdir)
{
	vector3d column_dir(zero_vector), line_dir(zero_vector), normal(zero_vector);
	column_dir[hdim] = (cdir ? -1.0 : 1.0); // along book height
	line_dir  [tdim] = (ldir ? -1.0 : 1.0); // along book thickness
	normal    [wdim] = (wdir ? -1.0 : 1.0); // along book width
	static vector<vert_tc_t> verts;
	verts.clear();
	gen_text_verts(verts, all_zeros, title, 1.0, column_dir, line_dir, 1); // use_quads=1 (could cache this for c.obj_id + dim/dir bits)
	assert(!verts.empty());
	cube_t text_bcube(verts[0].v);
	for (auto i = verts.begin()+2; i != verts.end(); i += 2) {text_bcube.union_with_pt(i->v);} // only need to include opposite corners
	float const wscale(title_area.get_sz_dim(hdim)/text_bcube.get_sz_dim(hdim)), hscale(title_area.get_sz_dim(tdim)/text_bcube.get_sz_dim(tdim));
	float width_scale(wscale), height_scale(hscale);
	min_eq(width_scale,  1.5f*height_scale); // use a reasonable aspect ratio
	min_eq(height_scale, 1.5f*width_scale );
	float const title_start_hdim(title_area.d[hdim][cdir] + column_dir[hdim]*0.5*title_area.get_sz_dim(hdim)*(1.0 -  width_scale/wscale)); // centered
	float const title_start_tdim(title_area.d[tdim][ldir] + line_dir  [tdim]*0.5*title_area.get_sz_dim(tdim)*(1.0 - height_scale/hscale)); // centered
	if (dot_product(normal, cross_product((verts[1].v - verts[0].v), (verts[2].v - verts[1].v))) < 0.0) {std::reverse(verts.begin(), verts.end());} // swap vertex winding order
	color_wrapper const cw(color);
	norm_comp const nc(normal);

	for (auto i = verts.begin(); i != verts.end(); ++i) {
		i->v[wdim] = title_area.d[wdim][!wdir]; // spine pos
		i->v[hdim] = (i->v[hdim] - text_bcube.d[hdim][cdir])*width_scale  + title_start_hdim;
		i->v[tdim] = (i->v[tdim] - text_bcube.d[tdim][ldir])*height_scale + title_start_tdim;
		mat.quad_verts.emplace_back(i->v, nc, i->t[0], i->t[1], cw);
	}
}

void building_room_geom_t::add_book(room_object_t const &c, bool inc_lg, bool inc_sm, bool inc_text,
	float tilt_angle, unsigned extra_skip_faces, bool no_title, float z_rot_angle)
{
	bool const is_held(z_rot_angle != 0.0); // held by the player, and need to draw the bottom
	bool const draw_cover_as_small(c.was_expanded() || is_held); // books in drawers, held, or dropped are always drawn as small objects
	if (draw_cover_as_small && !inc_sm && !inc_text) return; // nothing to draw
	bool const is_open(c.is_open()), upright(c.get_width() < c.dz()); // on a bookcase
	bool const from_book_set(c.flags & RO_FLAG_FROM_SET);
	bool const tdir((upright && !from_book_set) ? (c.dim ^ c.dir ^ bool(c.obj_id%7)) : 1); // sometimes upside down when upright and not from a set
	bool const ldir(!tdir), cdir(c.dim ^ c.dir ^ upright ^ ldir); // colum and line directions (left/right/top/bot) + mirror flags for front cover
	bool const was_dropped(c.taken_level > 0); // or held
	bool const shadowed(was_dropped && !is_held); // only shadowed if dropped by the player, since otherwise shadows are too small to have much effect; skip held objects (don't work)
	unsigned const tdim(upright ? !c.dim : 2), hdim(upright ? 2 : !c.dim); // thickness dim, height dim (c.dim is width dim)
	float const thickness(c.get_sz_dim(tdim)), width(c.get_length()), cov_thickness(0.125*thickness), indent(0.02*width); // Note: length/width are sort of backwards here
	cube_t bot(c), top(c), spine(c), pages(c), cover(c);
	bot.d[tdim][1] = c.d[tdim][0] + cov_thickness;
	top.d[tdim][0] = c.d[tdim][1] - cov_thickness;
	pages.d[tdim][0] = spine.d[tdim][0] = bot.d[tdim][1];
	pages.d[tdim][1] = spine.d[tdim][1] = top.d[tdim][0];
	vector3d shrink(zero_vector);
	shrink[c.dim] = shrink[upright ? 2 : !c.dim] = -indent;
	pages.expand_by(shrink);
	spine.d[c.dim][c.dir] = pages.d[c.dim][!c.dir];
	vector3d axis, tilt_about(c.get_urc()), zrot_about(c.get_cube_center());
	axis[c.dim] = 1.0; // along book width
	tilt_angle *= (c.dim ? -1.0 : 1.0);
	colorRGBA const color(apply_light_color(c));
	// skip top face, bottom face if not tilted, thickness dim if upright
	unsigned const sides_mask(upright ? get_skip_mask_for_xy(tdim) : (is_held ? EF_Z12 : EF_Z2));
	unsigned const spine_mask(is_open ? 0 : ~get_face_mask(c.dim, !c.dir)); // spine is drawn as part of the small faces when open
	unsigned const skip_faces(extra_skip_faces | ((tilt_angle == 0.0) ? EF_Z1 : 0) | sides_mask);

	if (z_rot_angle == 0.0 && (c.flags & RO_FLAG_RAND_ROT) && (c.obj_id%3) == 0) { // books placed on tables/desks are sometimes randomly rotated a bit
		z_rot_angle = (PI/12.0)*(fract(123.456*c.obj_id) - 0.5);
	}
	if ((draw_cover_as_small ? inc_sm : inc_lg) && !is_open) { // draw large faces: outside faces of covers and spine; not for open books
		rgeom_mat_t &mat(get_untextured_material(shadowed, 0, draw_cover_as_small));
		unsigned const qv_start(mat.quad_verts.size());
		mat.add_cube_to_verts_untextured(c, color, (extra_skip_faces | ~(sides_mask | spine_mask))); // untextured
		rotate_verts(mat.quad_verts, axis,   tilt_angle,  tilt_about, qv_start);
		rotate_verts(mat.quad_verts, plus_z, z_rot_angle, zrot_about, qv_start);
	}
	if (inc_sm) { // draw small faces: insides of covers, edges, and pages
		rgeom_mat_t &mat(get_untextured_material(shadowed, 0, 1));
		unsigned const qv_start(mat.quad_verts.size());
		unsigned const bot_skip_faces(extra_skip_faces | (was_dropped ? 0 : (EF_Z1 | ~get_face_mask(tdim, 0)))); // skip bottom face if not dropped
		unsigned top_skip_faces(extra_skip_faces | (upright ? EF_Z1 : 0) | ~get_face_mask(tdim, 1)); // skip top face, skip bottom face if upright
		colorRGBA const pages_color(apply_light_color(c, WHITE));

		if (is_open) { // draw book top cover as open
			assert(!upright);
			assert(!is_held);
			top_skip_faces = (extra_skip_faces | EF_Z2); // skip top face only
			top.translate_dim(c.dim, (c.dir ? -1.0 : 1.0)*(width - indent));
			mat.add_cube_to_verts_untextured(top, pages_color, ~EF_Z2); // untextured white, top face only
		}
		mat.add_cube_to_verts_untextured(bot,   color, bot_skip_faces); // untextured
		mat.add_cube_to_verts_untextured(top,   color, top_skip_faces); // untextured
		mat.add_cube_to_verts_untextured(spine, color, (skip_faces | spine_mask)); // untextured, skip back of spine (drawn as lg geom)
		mat.add_cube_to_verts_untextured(pages, pages_color, (skip_faces | spine_mask)); // untextured
		rotate_verts(mat.quad_verts, axis,   tilt_angle,  tilt_about, qv_start);
		rotate_verts(mat.quad_verts, plus_z, z_rot_angle, zrot_about, qv_start);

		if (is_open) {
			rgeom_mat_t &mat(get_material(tid_nm_pair_t(c.get_paper_tid(), 0.0), 0, 0, 1)); // map texture to quad
			unsigned const qv_start(mat.quad_verts.size());
			mat.add_cube_to_verts(pages, pages_color, zero_vector, ~EF_Z2, !c.dim, !c.dir, !cdir); // unshadowed, top face only, with proper orient
			rotate_verts(mat.quad_verts, plus_z, z_rot_angle, zrot_about, qv_start); // rotated, but not tilted
		}
	}
	bool const has_cover(ADD_BOOK_COVERS && !from_book_set && !is_open && c.enable_pictures() && (/*upright ||*/ (c.obj_id&2)));

	if (has_cover) { // add picture to book cover
		vector3d expand;
		float const height(c.get_sz_dim(hdim));
		float const img_width(0.9*width), img_height(min(0.8f*height, 0.65f*img_width)); // use correct aspect ratio
		expand[ hdim] = -0.5f*(height - img_height);
		expand[c.dim] = -0.5f*(width  - img_width);
		expand[ tdim] =  0.1f*indent; // expand outward, other dims expand inward
		cover.expand_by(expand);

		if (inc_sm) {
			int const picture_tid(c.get_picture_tid()); // not using user screenshot images
			bool const swap_xy(upright ^ (!c.dim));
			rgeom_mat_t &cover_mat(get_material(tid_nm_pair_t(picture_tid, 0.0), 0, 0, 1));
			unsigned const qv_start(cover_mat.quad_verts.size());
			cover_mat.add_cube_to_verts(cover, WHITE, zero_vector, get_face_mask(tdim, tdir), swap_xy, ldir, !cdir); // no shadows, small=1
			rotate_verts(cover_mat.quad_verts, axis,   tilt_angle,  tilt_about, qv_start);
			rotate_verts(cover_mat.quad_verts, plus_z, z_rot_angle, zrot_about, qv_start);
		}
	} // end cover image
	bool const add_spine_title(from_book_set || (c.obj_id & 7)); // 7/8 of the time, always for sets
	bool can_add_front_title(tilt_angle == 0.0 && !upright); // skip front title if tilted or upright because the logic is complex, it's slow, and usually not visible anyway

	if (ADD_BOOK_TITLES && inc_text && !no_title && !is_open && (can_add_front_title || add_spine_title)) { // add title(s) if not open
		unsigned const SPLIT_LINE_SZ = 24;
		bool const is_set_volume(from_book_set && c.drawer_flags > 0), add_volume_index(c.drawer_flags <= 20 && (c.flags & RO_FLAG_HAS_VOL_IX));
		// if this is a set, but not a numbered volume, include the volume index in the title random seed so that the title is unique
		unsigned const title_rand_id(c.obj_id + ((is_set_volume && !add_volume_index) ? (unsigned(c.drawer_flags) << 16) : 0));
		string title(gen_book_title(title_rand_id, nullptr, SPLIT_LINE_SZ)); // select our title text
		if (title.empty()) return; // no title (error?)
		rand_gen_t rgen;
		rgen.set_state(c.obj_id+1, c.obj_id+123);
		rgen.rand_mix();

		if (add_volume_index) { // add book set volume index numbers
			unsigned const vol_ix(c.drawer_flags);
			title.push_back(' ');

			if (rgen.rand_bool()) { // Roman numerals 1-20
				string const vol_nums[20] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX"};
				title += vol_nums[vol_ix - 1];
			}
			else { // numbers 1-20; use individual digits for faster stringify
				if (vol_ix >= 10) {title.push_back('0' + (vol_ix/10));} // tens digit
				title.push_back('0' + (vol_ix%10)); // ones digit
			}
		}
		colorRGBA text_color(BLACK);
		for (unsigned i = 0; i < 3; ++i) {text_color[i] = ((c.color[i] > 0.5) ? 0.0 : 1.0);} // invert + saturate to contrast with book cover
		text_color = apply_light_color(c, text_color);
		rgeom_mat_t &mat(get_material(tid_nm_pair_t(FONT_TEXTURE_ID), 0, 0, 1)); // no shadows, small=1
		unsigned const qv_start(mat.quad_verts.size());
		// maybe choose author
		string author;
		bool add_author((!from_book_set || is_set_volume) && (rgen.rand() & 3)), add_spine_author(0); // add an author 75% of the time if not from a non-volume set
		
		if (add_author) {
			add_spine_author = (rgen.rand() & 3); // 75% of the time
			if ((can_add_front_title && !has_cover) || add_spine_author) {author = gen_random_full_name(rgen);} // generate author if it will be added
		}
		if (add_spine_title) { // add title along spine
			cube_t title_area(c);
			vector3d expand;
			expand[ hdim] = -4.0*indent; // shrink
			expand[ tdim] = -1.0*indent; // shrink
			expand[c.dim] =  0.2*indent; // expand outward
			title_area.expand_by(expand);

			if (add_spine_author) {
				bool const author_side(rgen.rand_bool());
				float const title_height(title_area.get_sz_dim(hdim));
				cube_t author_area(title_area);
				author_area.d[hdim][!author_side] -= (author_side ? -1.0 : 1.0)*0.72*title_height; // 28% of height
				title_area .d[hdim][ author_side] += (author_side ? -1.0 : 1.0)*0.34*title_height; // 66% of height
				add_book_title(author, author_area, mat, text_color, hdim, tdim, c.dim, cdir, ldir, c.dir);
			}
			add_book_title(title, title_area, mat, text_color, hdim, tdim, c.dim, cdir, ldir, c.dir);
		}
		if (can_add_front_title && (!add_spine_title || from_book_set || (c.obj_id%3))) { // add title to front cover
			cube_t title_area_fc(c);
			title_area_fc.z1()  = title_area_fc.z2();
			title_area_fc.z2() += 0.2*indent;
			title_area_fc.expand_in_dim(c.dim, -4.0*indent);
			bool const top_dir(c.dim ^ c.dir);
			
			if (has_cover) { // place above cover; else, place in center
				title_area_fc.d[!c.dim][!top_dir] = cover.d[!c.dim][top_dir];
				title_area_fc.expand_in_dim(!c.dim, -1.0*indent);
			}
			else if (add_author) { // add the author if there's no cover picture
				float const translate_val((top_dir ? -1.0 : 1.0)*0.15*c.get_width());
				cube_t author_area(title_area_fc);
				author_area.translate_dim(!c.dim, translate_val); // shift down to not overlap the title
				author_area.expand_by_xy(-0.25*author_area.get_size()); // make author smaller than the title
				add_book_title(author, author_area, mat, text_color, c.dim, !c.dim, 2, !c.dir, !top_dir, 0); // {columns, lines, normal}
				title_area_fc.translate_dim(!c.dim, -translate_val); // shift the title up in the other direction
			}
			add_book_title(title, title_area_fc, mat, text_color, c.dim, !c.dim, 2, !c.dir, !top_dir, 0); // {columns, lines, normal}
		}
		rotate_verts(mat.quad_verts, axis,   tilt_angle,  tilt_about, qv_start);
		rotate_verts(mat.quad_verts, plus_z, z_rot_angle, zrot_about, qv_start);
	} // end titles
}

void get_bookcase_cubes(room_object_t const &c, cube_t &top, cube_t &middle, cube_t &back, cube_t lr[2], bool no_shelves, float sides_scale) {
	float const width(c.get_width()), depth((c.dir ? -1.0 : 1.0)*c.get_depth()), height(c.dz()), side_thickness(0.06*sides_scale*width);
	middle = c;

	for (unsigned d = 0; d < 2; ++d) { // left/right sides
		lr[d] = c;
		lr[d] .d[!c.dim][ d] += (d ? -1.0f : 1.0f)*(width - side_thickness);
		middle.d[!c.dim][!d]  = lr[d].d[!c.dim][d];
	}
	top = middle;
	top.z1()   += height - side_thickness; // make same width as sides
	middle.z2() = top.z1();
	if (!no_shelves) {middle.z1() += 0.07*height;} // only sides extend to the floor
	back = middle;
	back  .d[c.dim][ c.dir] += 0.94*depth;
	middle.d[c.dim][!c.dir]  = back.d[c.dim][c.dir];
}

void building_room_geom_t::add_bcase_book(room_object_t const &c, cube_t const &book, bool inc_lg, bool inc_sm, bool inc_text, bool backwards, bool in_set,
	unsigned skip_faces, unsigned book_ix, unsigned set_start_ix, colorRGBA const &color, vect_room_object_t *books)
{
	assert(book.is_strictly_normalized());
	bool const book_dir(c.dir ^ backwards ^ 1);
	room_object_t obj(book, TYPE_BOOK, c.room_id, c.dim, book_dir, c.flags, c.light_amt, SHAPE_CUBE, color);

	if (in_set) {
		obj.obj_id = c.obj_id + 123*set_start_ix;
		obj.flags |= RO_FLAG_FROM_SET; // set a flag so that books are consistent: title on front and spine, no author, no picture
		obj.drawer_flags = (book_ix - set_start_ix + 1); // first book starts at 1
		// only assign volume index when placed left to right, otherwise the order is backwards;
		// we could use (num_in_set - (book_ix - set_start_ix)) if we knew num_in_set ahead of time
		if (obj.dim ^ obj.dir ^ 1) {obj.flags |= RO_FLAG_HAS_VOL_IX;}
	}
	else { // individual book; book_ix/obj_id is unique
		obj.obj_id = c.obj_id + 123*book_ix;
	}
	obj.item_flags = (uint16_t)book_ix; // always unique per bookcase book; used for removing books from bookcases
	if (inc_lg || inc_sm || inc_text) {add_book(obj, inc_lg, inc_sm, inc_text, 0.0, skip_faces, backwards);} // detailed book, no title if backwards
	if (books) {books->push_back(obj);}
}

void building_room_geom_t::add_bookcase(room_object_t const &c, bool inc_lg, bool inc_sm, bool inc_text, float tscale,
	bool no_shelves, float sides_scale, point const *const use_this_tex_origin, vect_room_object_t *books)
{
	colorRGBA const color(apply_wood_light_color(c));
	point const tex_origin(use_this_tex_origin ? *use_this_tex_origin : c.get_llc());
	unsigned const skip_faces(c.was_moved() ? 0 : ~get_face_mask(c.dim, !c.dir)); // skip back face, unless moved by the player and no longer against the wall
	unsigned const skip_faces_shelves(skip_faces | get_skip_mask_for_xy(!c.dim)); // skip back face and sides
	float const depth((c.dir ? -1.0 : 1.0)*c.get_depth()); // signed depth
	cube_t top, middle, back, lr[2];
	get_bookcase_cubes(c, top, middle, back, lr, no_shelves, sides_scale);

	if (inc_lg) {
		unsigned const back_skip_faces(c.was_moved() ? ~get_skip_mask_for_xy(c.dim) : get_face_mask(c.dim, c.dir)); // back - only face oriented outward
		rgeom_mat_t &wood_mat(get_wood_material(tscale));
		wood_mat.add_cube_to_verts(top,  color, tex_origin, skip_faces_shelves); // top
		wood_mat.add_cube_to_verts(back, color, tex_origin, back_skip_faces   ); // back
		for (unsigned d = 0; d < 2; ++d) {wood_mat.add_cube_to_verts(lr[d], color, tex_origin, (skip_faces | EF_Z1));} // left/right sides
	}
	if (no_shelves) return;
	// add shelves
	rand_gen_t rgen;
	c.set_rand_gen_state(rgen);
	rgen.rand_mix();
	unsigned const num_shelves(3 + ((17*c.room_id + int(1000.0*fabs(c.z1())))%3)); // 3-5, randomly selected by room ID and floor
	float const shelf_dz(middle.dz()/num_shelves), shelf_thick(0.12*shelf_dz);
	// 40% of the time lower shelves are higher than upper shelves
	float const shelf_dz_range((rgen.rand_float() < 0.4) ? rgen.rand_uniform(0.15, 0.28)*shelf_dz : 0.0);
	unsigned const skip_book_flags(c.get_combined_flags());
	cube_t shelves[5];
	float cur_zval(0.0), shelf_heights[5] = {};
	
	for (unsigned i = 0; i < num_shelves; ++i) {
		float cur_dz(shelf_dz);
		if (2*i < num_shelves-1) {cur_dz += shelf_dz_range;} else if (2*i > num_shelves-1) {cur_dz -= shelf_dz_range;} // lower shelves are taller than upper shelves
		cube_t &shelf(shelves[i]);
		shelf = middle; // copy XY parts
		shelf.z1() += cur_zval;
		shelf.z2()  = shelf.z1() + shelf_thick;
		cur_zval   += cur_dz;
		shelf_heights[i] = (cur_dz - shelf_thick);
		if (inc_lg) {get_wood_material(tscale).add_cube_to_verts(shelf, color, tex_origin, skip_faces_shelves);} // Note: mat reference may be invalidated by adding books
	}
	// add books
	for (unsigned i = 0, book_ix = 0; i < num_shelves; ++i) {
		// Future work: add vertical shelf splits as well? With recursive nesting?
		if (rgen.rand_float() < 0.15) continue; // no books on this shelf
		cube_t const &shelf(shelves[i]);
		float const shelf_width(shelf.get_sz_dim(!c.dim)), shelf_height(shelf_heights[i]);

		if (rgen.rand_float() < 0.1) { // add a stack of books rather than upright books
			unsigned const num_stacked((rgen.rand()%8) + 1); // 1-8
			float const shelf_depth(shelf.get_sz_dim(c.dim)), max_book_width(1.0*shelf_depth); // must fit within shelf width
			float const max_book_len(min(1.0f*shelf_height, 1.5f*max_book_width)), max_book_hlen(0.5*max_book_len), dist_from_end(1.2*max_book_hlen); // height along width of shelf
			if (shelf_width <= 2.0*dist_from_end) continue; // shouldn't happen, but if it does it's probably best to skip this shelf
			float const max_book_thick(min(0.8f*shelf_thick, shelf_height/(num_stacked+1)));
			float const center_w(rgen.rand_uniform((shelf.d[!c.dim][0] + dist_from_end), (shelf.d[!c.dim][1] - dist_from_end)));
			float const center_d(shelf.get_center_dim(c.dim));
			float cur_zval(shelf.z2()); // stack starts on the top of the shelf

			for (unsigned n = 0; n < num_stacked; ++n) {
				float const book_thick(max_book_thick*rgen.rand_uniform(0.5, 1.0)), next_zval(cur_zval + book_thick);
				if (next_zval + max_book_thick > shelf_height) break; // not enough space to stack this book - done
				float const book_len(max_book_hlen*rgen.rand_uniform(0.7, 1.0)), book_width(min(0.5f*max_book_width*rgen.rand_uniform(0.7, 1.0), 1.1f*book_len));
				point bot_center;
				bot_center[ c.dim] = center_d + depth*rgen.rand_uniform(0.0, 0.05); // Note: signed depth
				bot_center[!c.dim] = center_w + 0.1*shelf_depth*rgen.rand_uniform(-1.0, 1.0);
				cube_t book(bot_center, bot_center);
				set_cube_zvals(book, cur_zval, next_zval);
				book.expand_in_dim( c.dim, book_width);
				book.expand_in_dim(!c.dim, book_len  );
				colorRGBA const book_color(book_colors[rgen.rand() % NUM_BOOK_COLORS]);
				bool const backwards((rgen.rand()%10) == 0); // spine facing out 90% of the time

				if (!(skip_book_flags & (1<<(book_ix&31)))) {
					add_bcase_book(c, book, inc_lg, inc_sm, inc_text, backwards, 0, skip_faces, book_ix, 0, book_color, books); // in_set=0, set_start_ix=0
				}
				++book_ix;
				cur_zval = next_zval;
			} // for n
			continue; // done with this shelf
		}
		unsigned const num_spaces(22 + (rgen.rand()%11)); // 22-32 books per shelf
		unsigned skip_mask(0), set_start_ix(0);

		for (unsigned n = 0; n < num_spaces; ++n) {
			if (rgen.rand_float() < 0.12) {
				unsigned const skip_end(n + (rgen.rand()%8) + 1); // skip 1-8 books
				for (; n < skip_end; ++n) {skip_mask |= (1<<n);}
			}
		}
		float const book_space(shelf_width/num_spaces), shelf_end(shelf.d[!c.dim][1]);
		bool const enable_sets(rgen.rand_float() < 0.4); // 40% of shelves can have sets
		bool prev_tilted(0), in_set(0), last_start_or_end_set(0);
		colorRGBA book_color;
		float pos(shelf.d[!c.dim][0]), last_book_pos(pos), min_height(0.0), width(0.0), height(0.0), depth_val(0.0);

		for (unsigned n = 0; n < num_spaces; ++n) { // fill this shelf horizontally
			if ((pos + 0.7*book_space) > shelf_end) break; // not enough space for another book
			// don't toggle on two consecutive books or on the last book on the shelf; this should prevent single book sets except when there's a missing book
			bool const start_or_end_set(enable_sets && !last_start_or_end_set && n+1 < num_spaces && (rgen.rand()&7) == 0);
			last_start_or_end_set = start_or_end_set;
			
			if (start_or_end_set) {
				in_set ^= 1;
				if (in_set) {set_start_ix = book_ix;} // first book in this set
			}
			if (start_or_end_set || !in_set) { // choose a new book set color/width/height if we're starting a new set or not currently in a set
				book_color = book_colors[rgen.rand() % NUM_BOOK_COLORS];
				width      = book_space*rgen.rand_uniform(0.7, 1.3);
				height     = max(shelf_height*rgen.rand_uniform(0.6, 0.98), min_height);
				depth_val  = depth*rgen.rand_uniform(0.0, 0.2);
			}
			if (!prev_tilted && (skip_mask & (1<<n))) { // skip this book, and don't tilt the next one
				pos   += width;
				in_set = 0;
				continue;
			}
			float const side_gap(0.05*width*rgen.rand_float()); // add a small gap between books to help separate books of different colors
			float const right_pos(min((pos + width - side_gap), shelf_end)), avail_space(right_pos - last_book_pos);
			float tilt_angle(0.0);
			cube_t book;
			book.z1() = shelf.z2();
			book.d[c.dim][ c.dir] = shelf.d[c.dim][ c.dir] + depth_val; // facing out
			book.d[c.dim][!c.dir] = shelf.d[c.dim][!c.dir]; // facing in
			book.translate_dim(c.dim, depth*rgen.rand_uniform(0.0, 0.05)); // slight random shift outward
			min_height = 0.0;

			if (avail_space > 1.1f*height && rgen.rand_float() < 0.5) { // book has space to fall over 50% of the time
				book.d[!c.dim][0] = last_book_pos + rgen.rand_uniform(0.0, (right_pos - last_book_pos - height)); // shift a random amount within the gap
				book.d[!c.dim][1] = book.d[!c.dim][0] + height;
				book.z2() = shelf.z2() + width;
			}
			else { // upright
				if (!prev_tilted && avail_space > 2.0*width && (right_pos + book_space) < shelf_end && n+1 < num_spaces) {
					// rotates about the URC; note that books are limited to tilting only in the direction of iteration, which is constant per bookcase
					float const lean_width(min((avail_space - width), rgen.rand_uniform(0.1, 0.6)*height)); // use part of the availabe space to lean
					tilt_angle = asinf(lean_width/height);
					float const delta_z(height - sqrt(height*height - lean_width*lean_width)); // move down to touch the bottom of the bookcase when rotated
					book.z1() -= delta_z;
					min_height = rgen.rand_uniform(0.95, 1.05)*(height - delta_z); // make sure the book this book is leaning on is tall enough
				}
				book.d[!c.dim][0] = pos;
				book.d[!c.dim][1] = right_pos; // clamp to edge of bookcase interior
				book.z2() = book.z1() + height;
				assert(pos < right_pos);
			}
			bool const backwards(!in_set && (rgen.rand()%10) == 0); // spine facing out 90% of the time if not in a set

			if (!(skip_book_flags & (1<<(book_ix&31)))) { // may have more than 32 books, and will wrap in that case
				add_bcase_book(c, book, inc_lg, inc_sm, inc_text, backwards, in_set, skip_faces, book_ix, set_start_ix, book_color, books);
			}
			++book_ix;
			pos += width;
			last_book_pos = pos;
			prev_tilted   = (tilt_angle != 0.0); // don't tilt two books in a row
		} // for n
	} // for i
}

void building_room_geom_t::add_wine_rack(room_object_t const &c, bool inc_lg, bool inc_sm, float tscale) {
	if (inc_lg) { // add wooden frame
		float const height(c.dz()), width(c.get_width()), depth(c.get_depth()), shelf_thick(0.1*depth);
		unsigned const num_rows(max(1, round_fp(2.0*height/depth))), num_cols(max(1, round_fp(2.0*width/depth)));
		float const row_step((height - shelf_thick)/num_rows), col_step((width - shelf_thick)/num_cols);
		colorRGBA const color(apply_wood_light_color(c));
		rgeom_mat_t &wood_mat(get_wood_material(tscale));
		cube_t frame(c);
		frame.d[c.dim][c.dir] += (c.dir ? -1.0 : 1.0)*0.09*depth; // slightly less depth so that bottles stick out a bit
		point const tex_orig(c.get_llc()); // relative to the wine rack so that textures don't slide when it's moved

		// create rows and columns of cubbies by intersecting horizontal and vertical cubes
		for (unsigned i = 0; i <= num_rows; ++i) { // rows/horizontal
			cube_t hc(frame);
			hc.z1() = c .z1() + row_step*i;
			hc.z2() = hc.z1() + shelf_thick;
			wood_mat.add_cube_to_verts(hc, color, tex_orig, 0); // draw all faces, even the back, in case it's visible through the window
		}
		for (unsigned i = 0; i <= num_cols; ++i) { // columns/vertical
			cube_t vc(frame);
			vc.d[!c.dim][0] = c .d[!c.dim][0] + col_step*i;
			vc.d[!c.dim][1] = vc.d[!c.dim][0] + shelf_thick;
			wood_mat.add_cube_to_verts(vc, color, tex_orig, 0); // draw all faces, even the back, in case it's visible through the window
		}
	}
	if (inc_sm && !c.obj_expanded()) { // add wine bottles if not expanded
		vect_room_object_t &objects(get_temp_objects());
		add_wine_rack_bottles(c, objects);
		add_nested_objs_to_verts(objects);
	}
}

room_object_t get_desk_drawers_part(room_object_t const &c) {
	bool const side(c.obj_id & 1);
	float const desk_width(c.get_width()), height(c.dz());
	room_object_t drawers(c);
	drawers.z1() += 0.05*height; // shift up a bit from the floor
	drawers.z2()  = c.z1() + 0.85*height;
	drawers.expand_by_xy(-0.15*get_tc_leg_width(c, 0.06));
	drawers.d[!c.dim][!side] += (side ? 1.0 : -1.0)*0.75*desk_width; // put the drawers off to one side
	return drawers;
}
room_object_t get_desk_top_back(room_object_t const &c) {
	room_object_t c_top_back(c);
	set_cube_zvals(c_top_back, c.z2(), (c.z2() + 1.8*c.dz()));
	c_top_back.d[c.dim][c.dir] += 0.75*(c.dir ? -1.0 : 1.0)*c.get_depth();
	return c_top_back;
}
void building_room_geom_t::add_desk(room_object_t const &c, float tscale, bool inc_lg, bool inc_sm) {
	// desk top and legs, similar to add_table()
	point const tex_origin(c.get_llc());
	colorRGBA const color(apply_wood_light_color(c));

	if (inc_lg) {
		cube_t top(c), legs_bcube(c);
		top.z1() += 0.85 * c.dz();
		legs_bcube.z2() = top.z1();
		get_wood_material(tscale).add_cube_to_verts(top, color, tex_origin); // all faces drawn
		add_tc_legs(legs_bcube, color, 0.06, tscale);
	}
	if (c.desk_has_drawers()) { // add drawers 75% of the time
		room_object_t drawers(get_desk_drawers_part(c));
		if (inc_lg) {get_wood_material(tscale).add_cube_to_verts(drawers, color, tex_origin);} // all faces drawn

		if (inc_sm) {
			bool const side(c.obj_id & 1);
			drawers.d[!c.dim][side] -= (side ? 1.0 : -1.0)*0.85*get_tc_leg_width(c, 0.06); // make sure the drawers can pull out without hitting the desk legs
			add_dresser_drawers(drawers, tscale);
		}
	}
	if (inc_lg && c.shape == SHAPE_TALL) { // add top/back section of desk; this part is outside the bcube
		add_bookcase(get_desk_top_back(c), 1, 1, 0, tscale, 1, 0.4, &tex_origin); // no_shelves=1, side_width=0.4, both large and small, no text, use same tex origin
	}
}

void building_room_geom_t::add_reception_desk(room_object_t const &c, float tscale) {
	vector3d const sz(c.get_size());
	float const top_z1(c.z1() + 0.94*sz.z), depth(sz[c.dim]), width(sz[!c.dim]), overhang(0.04*depth), lr_width(0.2*width), cutlen(depth - lr_width);
	assert(width > depth && cutlen > 0.0);
	colorRGBA const color(apply_light_color(c));
	// wood paneling sides
	rgeom_mat_t &side_mat(get_material(tid_nm_pair_t(PANELING_TEX, get_paneling_nm_tid(), 4.0*tscale, 4.0*tscale), 1)); // with shadows
	point const tex_origin(c.get_llc());
	unsigned const lr_dim_mask(~get_face_mask(c.dim, c.dir));
	cube_t base(c);
	base.z2() = top_z1;
	base.expand_by_xy(-overhang);
	cube_t front(base), left(base), right(base);
	front.d[ c.dim][!c.dir] -= (c.dir ? -1.0 : 1.0)*cutlen;
	left .d[!c.dim][1] -= (width - lr_width);
	right.d[!c.dim][0] += (width - lr_width);
	left .d[ c.dim][c.dir] = right.d[ c.dim][c.dir] = front.d[ c.dim][!c.dir];
	side_mat.add_cube_to_verts(front, color, tex_origin, EF_Z2);
	side_mat.add_cube_to_verts(left,  color, tex_origin, (EF_Z2 | lr_dim_mask)); // skip top face
	side_mat.add_cube_to_verts(right, color, tex_origin, (EF_Z2 | lr_dim_mask)); // skip top face
	// shiny marble top
	// Note: I wanted to add cylinders to the left and right top to round the corners like in the mapx lobby, but it's not easy to get the textures to line up here
	tid_nm_pair_t top_tex(get_counter_tid(), 2.5*tscale);
	top_tex.set_specular(0.5, 80.0);
	rgeom_mat_t &top_mat(get_material(top_tex, 1)); // with shadows
	cube_t top_front(front), top_left(left), top_right(right);
	top_front.z1() = top_left.z1() = top_right.z1() = top_z1;
	top_front.z2() = top_left.z2() = top_right.z2() = c.z2();
	top_front.expand_by_xy(overhang);
	top_left .expand_by_xy(overhang);
	top_right.expand_by_xy(overhang);
	top_left.d[c.dim][c.dir] = top_right.d[c.dim][c.dir] = top_front.d[c.dim][!c.dir]; // remove the overlap
	top_mat.add_cube_to_verts(top_front, color, tex_origin, 0); // all faces drawn
	top_mat.add_cube_to_verts(top_left,  color, tex_origin, lr_dim_mask);
	top_mat.add_cube_to_verts(top_right, color, tex_origin, lr_dim_mask);
}

void add_pillow(cube_t const &c, rgeom_mat_t &mat, colorRGBA const &color, point const &tex_origin) {
	unsigned const ndiv = 24; // number of quads in X and Y
	float const ndiv_inv(1.0/ndiv), dx_inv(1.0/c.dx()), dy_inv(1.0/c.dy());
	color_wrapper cw(color);
	auto &verts(mat.itri_verts); // Note: could cache verts
	unsigned const start(verts.size()), stride(ndiv + 1);
	float dists[ndiv+1] = {};
	norm_comp const nc(plus_z);

	for (unsigned x = 0; x <= ndiv; ++x) {
		float const v(2.0f*x*ndiv_inv - 1.0f); // centered on 0 in range [-1, 1]
		dists[x] = 0.5*SIGN(v)*sqrt(abs(v)) + 0.5; // nonlinear spacing, closer near the edges, convert back to [0, 1] range
	}
	for (unsigned y = 0; y <= ndiv; ++y) {
		float const yval(c.y1() + dists[y]*c.dy()), ey(2.0f*max(0.0f, min((yval - c.y1()), (c.y2() - yval)))*dy_inv);

		for (unsigned x = 0; x <= ndiv; ++x) {
			float const xval(c.x1() + dists[x]*c.dx()), ex(2.0f*max(0.0f, min((xval - c.x1()), (c.x2() - xval)))*dx_inv), zval(c.z1() + c.dz()*pow(ex*ey, 0.2f));
			verts.emplace_back(point(xval, yval, zval), nc, mat.tex.tscale_x*(xval - tex_origin.x), mat.tex.tscale_y*(yval - tex_origin.y), cw);
		} // for x
	} // for y
	for (unsigned y = 0; y <= ndiv; ++y) {
		for (unsigned x = 0; x <= ndiv; ++x) {
			unsigned const off(start + y*stride + x);
			vector3d const &v(verts[off].v);
			vector3d normal(zero_vector);
			if (x > 0    && y >    0) {normal += cross_product((v - verts[off-stride].v), (verts[off-1].v - v));} // LL
			if (x < ndiv && y >    0) {normal += cross_product((v - verts[off+1].v), (verts[off-stride].v - v));} // LR
			if (x < ndiv && y < ndiv) {normal += cross_product((v - verts[off+stride].v), (verts[off+1].v - v));} // UR
			if (x > 0    && y < ndiv) {normal += cross_product((v - verts[off-1].v), (verts[off+stride].v - v));} // UL
			verts[off].set_norm(normal.get_norm()); // this is the slowest line
		} // for x
	} // for y
	for (unsigned y = 0; y < ndiv; ++y) {
		for (unsigned x = 0; x < ndiv; ++x) {
			unsigned const off(start + y*stride + x);
			mat.indices.push_back(off + 0); // T1
			mat.indices.push_back(off + 1);
			mat.indices.push_back(off + stride+1);
			mat.indices.push_back(off + 0); // T2
			mat.indices.push_back(off + stride+1);
			mat.indices.push_back(off + stride);
		} // for x
	} // for y
}

bool bed_is_wide       (room_object_t const &c) {return (c.get_width() > 0.7*c.get_length());}
bool bed_has_posts     (room_object_t const &c) {return (bed_is_wide   (c) && (c.obj_id & 1 ));} // no posts for twin beds
bool bed_has_canopy    (room_object_t const &c) {return (bed_has_posts (c) && (c.obj_id & 2 ));}
bool bed_has_canopy_mat(room_object_t const &c) {return (bed_has_canopy(c) && (c.obj_id & 12) != 0);} // 75% of the time
int get_canopy_texture() {return get_texture_by_name("fabrics/wool.jpg");}

colorRGBA get_canopy_base_color(room_object_t const &c) {
	return (WHITE*0.5 + c.color.modulate_with(texture_color(c.get_sheet_tid()))*0.5); // brighter version of sheet color, 50% white
}

void get_bed_cubes(room_object_t const &c, cube_t cubes[6]) { // frame, head, foot, mattress, pillow, legs_bcube; no posts or canopy
	bool const is_wide(bed_is_wide(c));
	float const height(c.dz()), length(c.get_length()), width(c.get_width());
	float const head_width(0.04), foot_width(bed_has_posts(c) ? head_width : 0.03f); // relative to length
	cube_t frame(c), head(c), foot(c), mattress(c), legs_bcube(c), pillow(c);
	head.d[c.dim][!c.dir] += (c.dir ? 1.0 : -1.0)*(1.0 - head_width)*length;
	foot.d[c.dim][ c.dir] -= (c.dir ? 1.0 : -1.0)*(1.0 - foot_width)*length;
	mattress.d[c.dim][ c.dir] = head.d[c.dim][!c.dir];
	mattress.d[c.dim][!c.dir] = foot.d[c.dim][ c.dir];
	frame.z1() += 0.3*height;
	frame.z2() -= 0.65*height;
	foot.z2()  -= 0.2*height;
	mattress.z1()   = head.z1()   = foot.z1() = frame.z2();
	mattress.z2()   = pillow.z1() = mattress.z1() + 0.2*height;
	pillow.z2()     = pillow.z1() + 0.13*height;
	legs_bcube.z2() = frame.z1();
	mattress.expand_in_dim(!c.dim, -0.02*width); // shrink slightly
	float const pillow_space((is_wide ? 0.08 : 0.23)*width);
	pillow.expand_in_dim(!c.dim, -pillow_space);
	pillow.d[c.dim][ c.dir] = mattress.d[c.dim][ c.dir] + (c.dir ? -1.0 : 1.0)*0.02*length; // head
	pillow.d[c.dim][!c.dir] = pillow  .d[c.dim][ c.dir] + (c.dir ? -1.0 : 1.0)*(is_wide ? 0.25 : 0.6)*pillow.get_sz_dim(!c.dim);
	cubes[0] = frame; cubes[1] = head; cubes[2] = foot; cubes[3] = mattress; cubes[4] = pillow; cubes[5] = legs_bcube;
}
void building_room_geom_t::add_bed(room_object_t const &c, bool inc_lg, bool inc_sm, float tscale) {
	bool const add_posts(bed_has_posts(c));
	float const length(c.get_length()), head_width(0.04), foot_width(add_posts ? head_width : 0.03f); // relative to length
	cube_t cubes[6]; // frame, head, foot, mattress, pillow, legs_bcube; no posts or canopy
	get_bed_cubes(c, cubes);
	cube_t const &frame(cubes[0]), &head(cubes[1]), &foot(cubes[2]), &mattress(cubes[3]), &pillow(cubes[4]), &legs_bcube(cubes[5]);
	colorRGBA const sheet_color(apply_light_color(c));
	tid_nm_pair_t const sheet_tex(c.get_sheet_tid(), tscale);
	point const tex_origin(c.get_llc());

	if (inc_lg) {
		bool const no_mattress(c.taken_level > 2);
		colorRGBA const color(apply_wood_light_color(c));
		add_tc_legs(legs_bcube, color, max(head_width, foot_width), tscale);
		if (no_mattress) {get_wood_material(4.0*tscale);} // pre-allocate slats material if needed
		rgeom_mat_t &wood_mat(get_wood_material(tscale));

		if (no_mattress) { // mattress is gone, draw the slats on the bottom of the bed
			unsigned const num_slats = 12;
			unsigned const slat_skip_faces(get_skip_mask_for_xy(!c.dim));
			float const width(c.get_width()), side_width(0.08*width), slat_spacing(length/num_slats), slat_width(0.45*slat_spacing), slat_gap(0.5f*(slat_spacing - slat_width));
			cube_t sides[2] = {frame, frame}, slat(frame);
			sides[0].d[!c.dim][1] -= (width - side_width);
			sides[1].d[!c.dim][0] += (width - side_width);
			for (unsigned d = 0; d < 2; ++d) {wood_mat.add_cube_to_verts(sides[d], color, tex_origin);}
			slat.expand_in_dim(!c.dim, -side_width); // flush with sides
			cube_t ends[2] = {slat, slat};
			ends[0].d[c.dim][1] = frame.d[c.dim][0] + slat_gap;
			ends[1].d[c.dim][0] = frame.d[c.dim][1] - slat_gap;
			for (unsigned d = 0; d < 2; ++d) {wood_mat.add_cube_to_verts(ends[d], color, tex_origin, slat_skip_faces);}
			slat.d[c.dim][1] = slat.d[c.dim][0] + slat_spacing;
			slat.expand_in_dim(c.dim, -slat_gap); // add gap between slats to each side
			slat.expand_in_dim(2, -0.25*frame.dz()); // make thinner in Z
			rgeom_mat_t &slat_mat(get_wood_material(4.0*tscale));
			colorRGBA const slat_color(color*1.5); // make them lighter in color

			for (unsigned n = 0; n < num_slats; ++n) {
				slat_mat.add_cube_to_verts(slat, slat_color, tex_origin, (slat_skip_faces | EF_Z1));
				slat.translate_dim(c.dim, slat_spacing);
			}
		}
		else {
			wood_mat.add_cube_to_verts(frame, color, tex_origin);
		}
		wood_mat.add_cube_to_verts(head, color, tex_origin, EF_Z1);
		wood_mat.add_cube_to_verts(foot, color, tex_origin, EF_Z1);
		
		if (add_posts) { // maybe add bed posts and canopy; these extend outside of the bed bcube, but that probably doesn't matter
			bool const add_canopy(bed_has_canopy(c));
			float const post_width(min(head_width, foot_width)*length);
			cube_t posts_area(c);
			posts_area.z1() = foot.z2(); // start at the foot
			posts_area.z2() = posts_area.z1() + (add_canopy ? 1.4 : 0.6)*c.dz(); // higher posts for canopy bed
			cube_t posts[4];
			get_tc_leg_cubes_abs_width(posts_area, post_width, posts);
			bool const use_cylinders(!add_canopy && (c.obj_id & 4));

			for (unsigned i = 0; i < 4; ++i) {
				if (!add_canopy && posts[i].d[c.dim][!c.dir] == c.d[c.dim][!c.dir]) {posts[i].translate_dim(2, -(head.z2() - foot.z2()));} // make footboard posts shorter
				if (use_cylinders) {wood_mat.add_vcylin_to_verts(posts[i], color, 0, 1);}
				else {wood_mat.add_cube_to_verts(posts[i], color, tex_origin, EF_Z1);} // skip bottom face
			}
			if (add_canopy) {
				for (unsigned i = 0; i < 4; ++i) { // add 4 horizontal cube bars along the top of the bed connecting each adjacent pair of posts
					cube_t top(posts[i]);
					unsigned const next_ix[4] = {1, 3, 0, 2};
					top.union_with_cube(posts[next_ix[i]]); // next post
					top.z1() = top.z2() - post_width; // height = width
					bool const dim(top.dx() < top.dy());
					top.expand_in_dim(dim, -post_width); // remove overlaps with the post
					wood_mat.add_cube_to_verts(top, color, tex_origin, get_skip_mask_for_xy(dim));
				}
				if (bed_has_canopy_mat(c)) { // 75% of the time
					// add material to the top; it would be great if this could be partially transparent, but I can't figure out how to make that draw properly
					bool const shadowed(1); // partially transparent? sadly, we can't make it partially shadow casting
					float const canopy_tscale(5.0*tscale);
					rgeom_mat_t &canopy_mat(get_material(tid_nm_pair_t(get_canopy_texture(), canopy_tscale, shadowed), shadowed));
					colorRGBA const base_color(WHITE*0.5 + c.color.modulate_with(texture_color(sheet_tex.tid))*0.5); // brighter version of sheet color, 50% white
					colorRGBA const color(apply_light_color(c, base_color)); // partially transparent?
					float const dz(posts[0].z2() - c.z1()), offset(0.0001*dz); // full bed height
					cube_t canopy(c);
					canopy.expand_by_xy(offset); // expand slightly to prevent z-fighting with the bed posts and top frame
					canopy.z1() = canopy.z2() = posts[0].z2() + offset; // copy zvals from the first post z2, since they're all the same; zero height
					canopy_mat.add_cube_to_verts(canopy, color, tex_origin, ~EF_Z12); // draw top and bottom face only (double sided)
					// calculat triangle offsets to avoid z-fighting so that the last triangle drawn is in front so that alpha blending works properly
					float const tri_offs[4][2] = {{0,0}, {0,-offset}, {offset,0}, {offset,offset}};

					for (unsigned i = 0; i < 4; ++i) { // add triangles on the two exterior sides of each post
						bool const dx(i&1), dy(i>>1);

						for (unsigned d = 0; d < 2; ++d) {
							float const tri_width(0.3*canopy.get_sz_dim(d));
							point corner(canopy.d[0][dx], canopy.d[1][dy], canopy.z1());
							corner[d] += tri_offs[i][d];
							point const bot(corner.x, corner.y, corner.z-0.4f*dz);
							point top(corner);
							top[d] += ((d ? dy : dx) ? -1.0 : 1.0)*tri_width; // shift 30% the way along the top side
							point const verts[3] = {corner, bot, top};
							canopy_mat.add_triangle_to_verts(verts, color, 1, canopy_tscale*tri_width); // two_sided=1
						} // for d
					} // for i
				}
			}
		}
		if (!no_mattress) {
			unsigned const mattress_skip_faces(EF_Z1 | get_skip_mask_for_xy(c.dim));
			if (c.taken_level > 1) {get_untextured_material(1).add_cube_to_verts_untextured(mattress, sheet_color, mattress_skip_faces);} // sheets taken, bare mattress
			else {get_material(sheet_tex, 1).add_cube_to_verts(mattress, sheet_color, tex_origin, mattress_skip_faces);} // draw matterss with sheets
		}
	}
	if (inc_sm && c.taken_level == 0) { // draw pillows if not taken
		rgeom_mat_t &pillow_mat(get_material(sheet_tex, 1, 0, 1)); // small=1

		if (bed_is_wide(c)) { // two pillows
			for (unsigned d = 0; d < 2; ++d) {
				cube_t p(pillow);
				p.d[!c.dim][d] += (d ? -1.0 : 1.0)*0.55*pillow.get_sz_dim(!c.dim);
				add_pillow(p, pillow_mat, sheet_color, tex_origin);
			}
		}
		else {add_pillow(pillow, pillow_mat, sheet_color, tex_origin);} // one pillow
	}
}

void building_room_geom_t::add_trashcan(room_object_t const &c) {
	rgeom_mat_t &mat(get_untextured_material(1, 0, 1)); // inc_shadows=1, dynamic=0, small=1
	colorRGBA const color(apply_light_color(c));

	if (c.shape == SHAPE_CYLIN) {
		mat.add_vcylin_to_verts(c, color, 1, 0, 1, 1, 0.7, 1.0); // untextured, bottom only, two_sided truncated cone with inverted bottom normal
	}
	else { // sloped cube; this shape is rather unique, so is drawn inline; untextured
		cube_t base(c);
		base.expand_by_xy(vector3d(-0.2*c.dx(), -0.2*c.dy(), 0.0)); // shrink base by 40%
		auto &verts(mat.quad_verts);
		rgeom_mat_t::vertex_t v;
		v.set_c4(color);
		v.set_ortho_norm(2, 1); // +z
		
		for (unsigned i = 0; i < 4; ++i) { // bottom
			bool const xp(i==0||i==1), yp(i==1||i==2);
			v.v.assign(base.d[0][xp], base.d[1][yp], base.z1());
			v.t[0] = float(xp); v.t[1] = float(yp); // required for normal mapping ddx/ddy on texture coordinate
			verts.push_back(v);
		}
		for (unsigned dim = 0; dim < 2; ++dim) { // x,y
			for (unsigned dir = 0; dir < 2; ++dir) {
				unsigned const six(verts.size());

				for (unsigned i = 0; i < 4; ++i) {
					bool const tb(i==1||i==2), lohi(i==0||i==1);
					v.v[ dim] = (tb ? (cube_t)c : base).d[ dim][dir];
					v.v[!dim] = (tb ? (cube_t)c : base).d[!dim][lohi];
					v.v.z  = c.d[2][tb];
					//v.t[0] = float(tb); v.t[1] = float(lohi); // causes a seam between triangles due to TBN basis change, so leave at 0.0
					verts.push_back(v);
				}
				for (unsigned i = 0; i < 4; ++i) {verts.push_back(verts[six+3-i]);} // add reversed quad for opposing face
				norm_comp n(cross_product((verts[six].v - verts[six+1].v), (verts[six].v - verts[six+2].v)).get_norm());
				for (unsigned i = 0; i < 4; ++i) {verts[six+i].set_norm(n);} // front face
				n.invert_normal();
				for (unsigned i = 4; i < 8; ++i) {verts[six+i].set_norm(n);} // back face
			} // for dir
		} // for dim
	}
}

void add_pipe_with_bend(rgeom_mat_t &mat, colorRGBA const &color, point const &bot_pt, point const &top_pt, point const &bend, unsigned ndiv, float radius, bool draw_ends) {
	mat.add_sphere_to_verts(bend, vector3d(radius, radius, radius), color, (ndiv == 16)); // round part, low detail if ndiv==16
	mat.add_cylin_to_verts (bot_pt, bend, radius, radius, color, draw_ends, 0, 0, 0, 1.0, 1.0, 0, ndiv); // vertical
	mat.add_cylin_to_verts (top_pt, bend, radius, radius, color, draw_ends, 0, 0, 0, 1.0, 1.0, 0, ndiv); // horizontal
}

void building_room_geom_t::add_water_heater(room_object_t const &c) {
	bool const is_house(c.flags & RO_FLAG_IS_HOUSE);
	float const height(c.dz()), radius(c.get_radius()), pipe_radius(0.05*radius);
	float const front_pos(c.d[c.dim][c.dir]), front_dir(c.dir ? 1.0 : -1.0), top_z(c.z1() + 0.8*height);
	cube_t body(c), pan(c), top(c), vent(c), cone(c), box, pipes[2];

	for (unsigned d = 0; d < 2; ++d) {
		point pt(c.xc(), c.yc(), 0.0); // zval will be set below
		pt[!c.dim] += (d ? 1.0 : -1.0)*0.65*radius;
		pipes[d].set_from_sphere(pt, pipe_radius);
		set_cube_zvals(pipes[d], top_z, c.z2());
	}
	pan .z1() = c.z1() + 0.001*height; // prevent z-fighting
	pan .z2() = c.z1() + 0.05*height;
	top .z1() = top_z - 0.02*height;
	body.z2() = top_z - 0.01*height; // overlap top to fill the gap
	top .z2() = cone.z1() = vent.z1() = top_z;
	cone.z2() = top_z + 0.08*height;
	set_cube_zvals(box, (c.z1() + 0.14*height), (c.z1() + 0.2*height));
	pan .expand_by_xy(0.050*radius);
	top .expand_by_xy(0.010*radius);
	vent.expand_by_xy(-0.78*radius); // shrink
	cone.expand_by_xy(-0.78*radius); // shrink
	set_wall_width(box, c.get_center_dim(!c.dim), 0.2*radius, !c.dim);
	box.d[c.dim][!c.dir] = front_pos - front_dir*0.10*radius; // back  of box
	box.d[c.dim][ c.dir] = front_pos + front_dir*0.12*radius; // front of box
	rgeom_mat_t &metal_mat(get_metal_material(1, 0, 1)); // shadowed=1, small=1
	metal_mat.add_vcylin_to_verts(body, apply_light_color(c, GRAY   ), 0, 0, 0); // main body - draw sides only
	metal_mat.add_vcylin_to_verts(pan,  apply_light_color(c, LT_GRAY), 1, 0, 1, 0, 1.0, 1.0, 1.0, 1.0, 0, 64); // bottom pan - two sided, with bottom; ndiv=64
	metal_mat.add_vcylin_to_verts(top,  apply_light_color(c, DK_GRAY), 0, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 64); // top - draw top; ndiv=64
	metal_mat.add_vcylin_to_verts(vent, apply_light_color(c, LT_GRAY), 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, 16); // ndiv=16
	metal_mat.add_vcylin_to_verts(cone, apply_light_color(c, LT_GRAY), 0, 0, 0, 0, 1.8, 0.0); // cone
	if (!is_house) {get_metal_material(1, 0, 1, BRASS_C);} // make sure it exists in the materials
	rgeom_mat_t &copper_mat(get_metal_material(1, 0, 1, COPPER_C)); // small=1
	colorRGBA const copper_color(apply_light_color(c, COPPER_C));
	bool const low_detail = 1;
	unsigned const pipe_ndiv(get_rgeom_sphere_ndiv(low_detail));
	
	for (unsigned d = 0; d < 2; ++d) {
		cube_t &pipe(pipes[d]);

		if (!is_house) { // bend office building water pipes back down into the floor since routing is in the basement
			float const bend_zval(pipe.zc()), pipe_len(0.92*radius);
			pipe.z2() = bend_zval; // shorten; no longer reaches the ceiling
			cube_t v_pipe(pipe);
			v_pipe.z1() = c.z1(); // down to the floor
			v_pipe.translate_dim(c.dim, (c.dir ? 1.0 : -1.0)*pipe_len); // shift in front of water heater
			point const bends[2] = {point(pipe.xc(), pipe.yc(), bend_zval), point(v_pipe.xc(), v_pipe.yc(), bend_zval)};
			copper_mat.add_vcylin_to_verts(v_pipe, copper_color, 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, pipe_ndiv);
			copper_mat.add_cylin_to_verts(bends[0], bends[1], pipe_radius, pipe_radius, copper_color, 0, 0, 0, 0, 1.0, 1.0, 0, pipe_ndiv);
			// add brass fittings
			rgeom_mat_t &brass_mat(get_metal_material(1, 0, 1, BRASS_C )); // small=1
			colorRGBA const brass_color(apply_light_color(c, BRASS_C));
			float const fr(1.1*pipe_radius), extend(2.0*fr);
			vector3d const delta((bends[0] - bends[1])*(extend/pipe_len));

			for (unsigned f = 0; f < 2; ++f) {
				add_pipe_with_bend(brass_mat, brass_color, bends[f]-vector3d(0.0, 0.0, extend), bends[f]+(f ? 1.0 : -1.0)*delta, bends[f], pipe_ndiv, fr, 1); // draw_ends=1
			}
		}
		copper_mat.add_vcylin_to_verts(pipe, copper_color, 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0, pipe_ndiv);
	} // for d
	get_untextured_material(1, 0, 1).add_cube_to_verts_untextured(box, apply_light_color(c, LT_GRAY)); // control box

	if (is_house && (c.room_id & 1)) { // add sticker 50% of the time for houses
		cube_t sticker(c);
		sticker.expand_by_xy(0.005*radius);
		set_cube_zvals(sticker, (c.z1() + 0.30*height), (c.z1() + 0.50*height));
		rgeom_mat_t &sticker_mat(get_material(tid_nm_pair_t(get_texture_by_name("interiors/water_heater_sticker.jpg")), 0, 0, 1)); // no shadows, small=1
		sticker_mat.add_vcylin_to_verts(sticker, apply_light_color(c, LT_GRAY), 0, 0, 0, 0, 1.0, 1.0, 2.0); // sticker - draw sides only with scaled texture
	}
}

colorRGBA get_furnace_color() {return texture_color(get_texture_by_name("interiors/furnace.jpg"));}

void add_furnace_pipe_with_bend(room_object_t const &c, rgeom_mat_t &mat, colorRGBA const &color,
	unsigned ndiv, float rscale, float lr_offset, float z_offset, float extend)
{
	bool const mirror_x(c.dim ^ c.dir ^ 1);
	float const height(c.dz()), pipe_radius(rscale*height), ndim_pos(mirror_x ? lr_offset : (1.0 - lr_offset));
	point entry_pos; // on the front bottom right side
	entry_pos[ c.dim] = c.d[c.dim][c.dir];
	entry_pos[!c.dim] = ndim_pos*c.d[!c.dim][0] + (1.0 - ndim_pos)*c.d[!c.dim][1];
	entry_pos.z = c.z1() + z_offset*height;
	point bend(entry_pos);
	bend[c.dim] += (c.dir ? 1.0 : -1.0)*extend*pipe_radius; // move outward
	point bot_pos(bend);
	bot_pos.z = c.z1();
	add_pipe_with_bend(mat, color, bot_pos, entry_pos, bend, ndiv, pipe_radius, 0); // draw_ends=0
}
void narrow_furnace_intake(cube_t &duct, room_object_t const &c) {
	duct.d[c.dim][c.dir] -= (c.dir ? 1.0 : -1.0)*0.35*c.get_length(); // shift inward toward back
	duct.expand_in_dim(!c.dim, -0.05*c.get_width()); // shrink slightly
}
void building_room_geom_t::add_furnace(room_object_t const &c) {
	colorRGBA const duct_color(apply_light_color(c, DUCT_COLOR));
	room_object_t main_unit(c);
	room_object_t base(c); // base area below the furnace that connects to the ducts
	main_unit.z1() = base.z2() = c.z1() + 0.167*c.dz();
	add_obj_with_front_texture(main_unit, "interiors/furnace.jpg", get_furnace_color(), 1); // small=1
	float const expand_amt(0.01*c.get_width());
	base.expand_in_dim(!c.dim, expand_amt); // expand slightly in width
	base.d[c.dim][c.dir] += (c.dir ? 1.0 : -1.0)*expand_amt; // shift slightly outward in the front
	base.dir    = 1; // encoding for vertical
	base.flags |= RO_FLAG_ADJ_BOT; // skip bottom face; top face is slightly visible through expanded edges
	add_duct(base);

	if (c.in_attic()) {
		// add ductwork on the top ... somewhere?
	}
	else { // basement: add duct up into the ceiling; not a collision object
		room_object_t duct(c);
		narrow_furnace_intake(duct, c);
		set_cube_zvals(duct, c.z2(), c.z2()+0.6*c.dz()); // extend to cover the remaining gap between the top of the furnace and the ceiling
		duct.dir    = 1; // encoding for vertical
		duct.flags |= (RO_FLAG_ADJ_BOT | RO_FLAG_ADJ_TOP); // skip top and bottom face
		add_duct(duct);
	}
	// add pipes
	bool const low_detail = 1;
	unsigned const pipe_ndiv(get_rgeom_sphere_ndiv(low_detail));
	// insulated
	rgeom_mat_t &insul_mat(get_metal_material(1, 0, 1, WHITE)); // black reflective tape (not actually metal); shadows=1, small=1
	add_furnace_pipe_with_bend(c, insul_mat, apply_light_color(c, BLACK), pipe_ndiv, 0.02, 0.87, 0.484, 2.2);
	// copper
	rgeom_mat_t &copper_mat(get_metal_material(1, 0, 1, COPPER_C)); // shadows=1, small=1
	add_furnace_pipe_with_bend(c, copper_mat, apply_light_color(c, COPPER_C), pipe_ndiv, 0.007, 0.88, 0.45, 1.6);
	// drain (2x)
	rgeom_mat_t &plastic_mat(get_untextured_material(1, 0, 1)); // shadows=1, small=1

	for (unsigned d = 0; d < 2; ++d) {
		add_furnace_pipe_with_bend(c, plastic_mat, apply_light_color(c, WHITE), pipe_ndiv, 0.016, (d ? 0.081 : 0.173), 0.2, 1.8);
	}
}

void building_room_geom_t::add_toaster_proxy(room_object_t const &c) { // draw a simple untextured XY cube to show a lower LOD model of the toaster
	cube_t c2(c);
	c2.expand_in_dim( c.dim, -0.10*c.get_length());
	c2.expand_in_dim(!c.dim, -0.05*c.get_width ());
	c2.z1() += 0.06*c.dz();
	c2.z2() -= 0.02*c.dz();
	unsigned const skip_faces(~get_skip_mask_for_xy(!c.dim));
	get_untextured_material(0, 0, 1).add_cube_to_verts_untextured(c2, apply_light_color(c, c.color*0.75), skip_faces); // unshadowed, small=1, scaled by material color
}

void building_room_geom_t::add_laundry_basket(room_object_t const &c) {
	// Note: no alpha test is enabled in the shader when drawing this, so the holes in the material may not be drawn correctly against objects such as exterior walls
	rgeom_mat_t &tex_mat(get_material(tid_nm_pair_t(get_texture_by_name("interiors/plastic_mesh.png")), 1, 0, 1)); // inc_shadows=1, dynamic=0, small=1
	//rgeom_mat_t &tex_mat(mats_amask.get_material(tid_nm_pair_t(get_texture_by_name("interiors/plastic_mesh.png")), 1)); // shadowed with alpha mask - doesn't look as good, need min_alpha=0.0
	cube_t bot(c), mid(c), top(c);
	bot.z2() = mid.z1() = c.z1() + 0.12*c.dz();
	mid.z2() = top.z1() = c.z2() - 0.12*c.dz();
	colorRGBA const color(apply_light_color(c));
	tex_mat  .add_vcylin_to_verts(mid, color, 0, 0, 1, 1); // two_sided cylinder
	rgeom_mat_t &solid_mat(get_untextured_material(0, 0, 1)); // inc_shadows=0, dynamic=0, small=1
	solid_mat.add_vcylin_to_verts(bot, color, 1, 0, 1, 1); // two_sided cylinder with bottom
	solid_mat.add_vcylin_to_verts(top, color, 0, 0, 1, 1); // two_sided cylinder
}

void building_room_geom_t::add_br_stall(room_object_t const &c) {
	rgeom_mat_t &mat(get_untextured_material(1));
	colorRGBA const color(apply_light_color(c));

	if (c.shape == SHAPE_SHORT) { // wall separating urinals, drawn as a single cube
		mat.add_cube_to_verts_untextured(c, color, ~get_face_mask(c.dim, c.dir));
		return;
	}
	float const dz(c.dz()), wall_thick(0.0125*dz), frame_thick(6.0*wall_thick), door_gap(0.3*wall_thick);
	cube_t sides(c), front(c);
	sides.z2() -= 0.35*dz;
	sides.z1() += 0.15*dz;
	sides.d[c.dim][!c.dir] += (c.dir ? 1.0 : -1.0)*wall_thick; // shorten for door
	front.d[c.dim][ c.dir] = sides.d[c.dim][!c.dir]; // dir points toward the inside of the stall
	cube_t side1(sides), side2(sides), front1(front), front2(front), door(front);
	door.z2() -= 0.38*dz;
	door.z1() += 0.18*dz;
	side1.d[!c.dim][1] = side1.d[!c.dim][0] + wall_thick;
	side2.d[!c.dim][0] = side2.d[!c.dim][1] - wall_thick;
	door.expand_in_dim(!c.dim, -frame_thick);
	front1.d[!c.dim][1] = door.d[!c.dim][0];
	front2.d[!c.dim][0] = door.d[!c.dim][1];
	unsigned const side_skip_mask(get_skip_mask_for_xy(c.dim));
	mat.add_cube_to_verts_untextured(side1,  color, side_skip_mask);
	mat.add_cube_to_verts_untextured(side2,  color, side_skip_mask);
	mat.add_cube_to_verts_untextured(front1, color, EF_Z12);
	mat.add_cube_to_verts_untextured(front2, color, EF_Z12);
	door.expand_in_dim(!c.dim, -door_gap);

	if (c.is_open()) { // make the door open
		bool const hinge_side(0); // does it matter?
		float const door_width(door.get_sz_dim(!c.dim));
		door.d[!c.dim][!hinge_side] -= (hinge_side ? -1.0 : 1.0)*(door_width - wall_thick); // width => thickness
		door.d[ c.dim][ c.dir     ] -= (c.dir      ? -1.0 : 1.0)*(door_width - wall_thick); // thickness => width
	}
	mat.add_cube_to_verts_untextured(door, color);
}

int get_cubicle_tid(room_object_t const &c) {return get_texture_by_name((c.obj_id & 1) ? "carpet/carpet1.jpg" : "carpet/carpet2.jpg");} // select from one of 2 textures

void building_room_geom_t::add_cubicle(room_object_t const &c, float tscale) {
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_cubicle_tid(c), tscale), 1));
	colorRGBA const color(apply_light_color(c));
	point const tex_origin(c.get_llc());
	float const dz(c.dz()), wall_thick(0.07*dz), frame_thick(8.0*wall_thick), dir_sign(c.dir ? 1.0 : -1.0);
	bool const is_short(c.shape == SHAPE_SHORT);
	cube_t sides(c), front(c), back(c);
	if (is_short) {back.z2() -= 0.4*dz;}
	sides.d[c.dim][!c.dir] += dir_sign*wall_thick; // front
	sides.d[c.dim][ c.dir] -= dir_sign*wall_thick; // back
	front.d[c.dim][ c.dir] = sides.d[c.dim][!c.dir];
	back .d[c.dim][!c.dir] = sides.d[c.dim][ c.dir];
	cube_t side1(sides), side2(sides), front1(front), front2(front);
	side1 .d[!c.dim][1] = side1.d[!c.dim][0] + wall_thick;
	side2 .d[!c.dim][0] = side2.d[!c.dim][1] - wall_thick;
	front1.d[!c.dim][1] = front.d[!c.dim][0] + frame_thick;
	front2.d[!c.dim][0] = front.d[!c.dim][1] - frame_thick;
	unsigned const side_skip_mask (EF_Z12 | get_skip_mask_for_xy( c.dim));
	unsigned const front_skip_mask(EF_Z12 | get_skip_mask_for_xy(!c.dim));
	mat.add_cube_to_verts(side1,  color, tex_origin, side_skip_mask);
	mat.add_cube_to_verts(side2,  color, tex_origin, side_skip_mask);
	mat.add_cube_to_verts(front1, color, tex_origin, front_skip_mask);
	mat.add_cube_to_verts(front2, color, tex_origin, front_skip_mask);
	mat.add_cube_to_verts(back,   color, tex_origin, EF_Z12);
	// black edges on walls
	rgeom_mat_t &edge_mat(get_untextured_material(0)); // unshadowed
	unsigned const side_edge_skip_mask (~(EF_Z2 | (is_short ? ~get_face_mask(c.dim, c.dir) : 0)));
	unsigned const front_edge_skip_mask(~(EF_Z2 | get_skip_mask_for_xy(!c.dim)));
	colorRGBA const edge_color(apply_light_color(c, BKGRAY));
	edge_mat.add_cube_to_verts_untextured(side1,  edge_color, side_edge_skip_mask);
	edge_mat.add_cube_to_verts_untextured(side2,  edge_color, side_edge_skip_mask);
	edge_mat.add_cube_to_verts_untextured(front1, edge_color, front_edge_skip_mask);
	edge_mat.add_cube_to_verts_untextured(front2, edge_color, front_edge_skip_mask);
	edge_mat.add_cube_to_verts_untextured(back,   edge_color, ~EF_Z2);
	// desk surface
	rgeom_mat_t &surf_mat(get_material(tid_nm_pair_t(MARBLE_TEX, 4.0*tscale), 1));
	colorRGBA const surf_color(apply_light_color(c, LT_GRAY));
	cube_t surface(sides);
	set_cube_zvals(surface, (c.z1() + 0.45*dz), (c.z1() + 0.50*dz));
	cube_t surf1(surface), surf2(surface), surf3(surface); // left, right, back
	surf1.d[!c.dim][0] = side1.d[!c.dim][1];
	surf1.d[!c.dim][1] = surf3.d[!c.dim][0] = front1.d[!c.dim][1];
	surf2.d[!c.dim][0] = surf3.d[!c.dim][1] = front2.d[!c.dim][0];
	surf2.d[!c.dim][1] = side2.d[!c.dim][0];
	surf3.d[ c.dim][!c.dir] = surface.d[c.dim][c.dir] - dir_sign*frame_thick;
	surf_mat.add_cube_to_verts(surf1, surf_color, tex_origin, get_skip_mask_for_xy( c.dim));
	surf_mat.add_cube_to_verts(surf2, surf_color, tex_origin, get_skip_mask_for_xy( c.dim));
	surf_mat.add_cube_to_verts(surf3, surf_color, tex_origin, get_skip_mask_for_xy(!c.dim));
}

class sign_helper_t {
	map<string, unsigned> txt_to_id;
	vector<string> text;
public:
	unsigned register_text(string const &t) {
		auto it(txt_to_id.find(t));
		if (it != txt_to_id.end()) return it->second; // found
		unsigned const id(text.size());
		txt_to_id[t] = id; // new text, insert it
		text.push_back(t);
		assert(text.size() == txt_to_id.size());
		return id;
	}
	string const &get_text(unsigned id) const {
		assert(id < text.size());
		return text[id];
	}
};

sign_helper_t sign_helper;

unsigned register_sign_text(string const &text) {return sign_helper.register_text(text);}

void add_sign_text_verts(string const &text, cube_t const &sign, bool dim, bool dir, colorRGBA const &color, vector<vert_norm_comp_tc_color> &verts_out) {
	assert(!text.empty());
	cube_t ct(sign); // text area is slightly smaller than full cube
	ct.expand_in_dim(!dim, -0.1*ct.get_sz_dim(!dim));
	ct.expand_in_dim(2, -0.05*ct.dz());
	vector3d col_dir(zero_vector), normal(zero_vector);
	bool const ldir(dim ^ dir);
	col_dir[!dim] = (ldir  ? 1.0 : -1.0);
	normal [ dim] = (dir ? 1.0 : -1.0);
	static vector<vert_tc_t> verts;
	verts.clear();
	point pos;
	pos[dim] = ct.d[dim][dir] + (dir ? 1.0 : -1.0)*0.1*ct.get_sz_dim(dim); // normal
	gen_text_verts(verts, pos, text, 1.0, col_dir, plus_z, 1); // use_quads=1
	assert(!verts.empty());
	cube_t text_bcube(verts[0].v);
	for (auto i = verts.begin()+2; i != verts.end(); i += 2) {text_bcube.union_with_pt(i->v);} // only need to include opposite corners
	float const width_scale(ct.get_sz_dim(!dim)/text_bcube.get_sz_dim(!dim)), height_scale(ct.dz()/text_bcube.dz());
	if (dot_product(normal, cross_product((verts[1].v - verts[0].v), (verts[2].v - verts[1].v))) < 0.0) {std::reverse(verts.begin(), verts.end());} // swap vertex winding order
	color_wrapper const cw(color);
	norm_comp const nc(normal);

	for (auto i = verts.begin(); i != verts.end(); ++i) {
		i->v[!dim] = i->v[!dim]*width_scale + ct.d[!dim][!ldir]; // line
		i->v.z     = i->v.z*height_scale + ct.z1(); // column
		verts_out.emplace_back(i->v, nc, i->t[0], i->t[1], cw);
	}
}

void building_room_geom_t::add_sign(room_object_t const &c, bool inc_back, bool inc_text) {
	if (inc_back) {
		bool const hanging(c.is_hanging()); // for exit sign
		unsigned const skip_faces(hanging ? EF_Z2 : ~get_face_mask(c.dim, !c.dir)); // skip back face, top face if hanging
		// what about transparent plastic back for hanging signs?
		get_untextured_material(0, 0, 1).add_cube_to_verts_untextured(c, apply_light_color(c, WHITE), skip_faces); // back of the sign, always white (for now); unshadowed, small
	}
	if (!inc_text) return;
	// add sign text
	tid_nm_pair_t tex(FONT_TEXTURE_ID);
	if (c.flags & RO_FLAG_EMISSIVE) {tex.emissive = 1.0;}
	rgeom_mat_t &mat(get_material(tex, 0, 0, 1)); // unshadowed, small=1
	add_sign_text_verts(sign_helper.get_text(c.obj_id), c, c.dim, c.dir, apply_light_color(c), mat.quad_verts);
}

bool get_dishwasher_for_ksink(room_object_t const &c, cube_t &dishwasher) {
	if (c.type != TYPE_KSINK) return 0; // error?
	float const dz(c.dz()), depth(c.get_depth()), width(c.get_width()), dir_sign(c.dir ? 1.0 : -1.0);
	if (width <= 3.5*depth) return 0; // too small to fit a dishwasher
	dishwasher = c;
	bool const side((c.flags & RO_FLAG_ADJ_LO) ? 1 : ((c.flags & RO_FLAG_ADJ_HI) ? 0 : (c.obj_id & 1))); // left/right of the sink
	dishwasher.z1() += 0.06*dz;
	dishwasher.z2() -= 0.05*dz;
	dishwasher.d[ c.dim][!c.dir]  = c.d[c.dim][c.dir] - dir_sign*0.1*depth; // back
	dishwasher.d[ c.dim][ c.dir] += dir_sign*0.05*depth; // front
	dishwasher.d[!c.dim][!side ]  = c.get_center_dim(!c.dim) + (side ? 1.0 : -1.0)*0.66*depth;
	dishwasher.d[!c.dim][ side ]  = dishwasher.d[!c.dim][!side] + (side ? 1.0 : -1.0)*1.05*depth;
	return 1;
}
room_object_t split_cabinet_at_dishwasher(room_object_t &cabinet, cube_t const &dishwasher) { // Note: modifies cabinet
	room_object_t left_part(cabinet);
	left_part.d[!cabinet.dim][1] = dishwasher.d[!cabinet.dim][0];
	cabinet  .d[!cabinet.dim][0] = dishwasher.d[!cabinet.dim][1];
	left_part.flags &= ~RO_FLAG_ADJ_HI;
	cabinet  .flags &= ~RO_FLAG_ADJ_LO;
	cabinet.drawer_flags >>= 8; // left half has first 8 door bits, right half has last 8 door bits
	return left_part;
}
cube_t get_sink_cube(room_object_t const &c) {
	assert(c.type == TYPE_KSINK || c.type == TYPE_BRSINK);
	float const dz(c.dz()), sdepth(0.8*c.get_depth()), swidth(min(1.4f*sdepth, 0.75f*c.get_width()));
	vector3d const center(c.get_cube_center());
	cube_t sink(center, center);
	set_cube_zvals(sink, (c.z2() - 0.3*dz), (c.z2() - 0.05*dz));
	sink.expand_in_dim( c.dim, 0.5*sdepth);
	sink.expand_in_dim(!c.dim, 0.5*swidth);
	return sink;
}

void building_room_geom_t::add_counter(room_object_t const &c, float tscale) { // for kitchens
	float const dz(c.dz()), depth(c.get_depth()), dir_sign(c.dir ? 1.0 : -1.0);
	cube_t top(c), dishwasher;
	top.z1() += 0.95*dz;
	tid_nm_pair_t const marble_tex(get_counter_tid(), 2.5*tscale);
	rgeom_mat_t &top_mat(get_material(marble_tex, 1));
	colorRGBA const top_color(apply_light_color(c, WHITE));

	if (c.type == TYPE_KSINK || c.type == TYPE_BRSINK) { // counter with kitchen or bathroom sink
		float const sdepth(0.8*depth);
		vector3d faucet_pos(c.get_cube_center());
		faucet_pos[c.dim] -= dir_sign*0.56*sdepth;
		cube_t const sink(get_sink_cube(c));
		cube_t faucet1(faucet_pos, faucet_pos);
		set_cube_zvals(faucet1, top.z2(), (top.z2() + 0.30*dz));
		faucet1.expand_in_dim( c.dim, 0.04*sdepth);
		faucet1.expand_in_dim(!c.dim, 0.07*sdepth);
		cube_t faucet2(faucet1);
		faucet2.z1()  = faucet1.z2();
		faucet2.z2() += 0.035*dz;
		faucet2.d[c.dim][c.dir] += dir_sign*0.28*sdepth;
		vect_cube_t &cubes(get_temp_cubes());
		subtract_cube_from_cube(top, sink, cubes);
		for (auto i = cubes.begin(); i != cubes.end(); ++i) {top_mat.add_cube_to_verts(*i, top_color, tex_origin);} // should always be 4 cubes
		colorRGBA const sink_color(apply_light_color(c, GRAY));
		rgeom_mat_t &basin_mat(get_metal_material(0));
		basin_mat.add_cube_to_verts(sink, sink_color, tex_origin, EF_Z2, 0, 0, 0, 1); // basin: inverted, skip top face, unshadowed
		// drain
		cube_t drain(point(sink.xc(), sink.yc(), sink.z1()));
		drain.expand_by_xy(0.1*min(sink.dx(), sink.dy()));
		drain.z2() += 0.012*sink.dz();
		basin_mat.add_vcylin_to_verts(drain, apply_light_color(c, BKGRAY), 0, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 1); // top only
		
		if (c.drawer_flags > 0) { // draw outside of sink basin if any drawers are open
			cube_t sink_outer(sink);
			sink_outer.expand_by_xy(0.01*dz); // expand by sink basin thickness
			sink_outer.z1() -= 0.1*dz;
			basin_mat.add_cube_to_verts_untextured(sink_outer, sink_color, (~get_face_mask(c.dim, !c.dir) | EF_Z2)); // skip back and top
		}
		rgeom_mat_t &metal_mat(get_metal_material(1)); // shadowed, specular metal (specular doesn't do much because it's flat, but may make more of a diff using a cylinder later)
		metal_mat.add_cube_to_verts_untextured(faucet1, sink_color, EF_Z12); // vertical part of faucet, skip top and bottom faces
		metal_mat.add_cube_to_verts_untextured(faucet2, sink_color, 0); // horizontal part of faucet, draw all faces

		if (c.type == TYPE_BRSINK) { // bathroom sink
			metal_mat.add_cube_to_verts_untextured(sink, sink_color, EF_Z2); // outside of basin, no top surface, shadowed
			cube_t front(c);
			front.z2() = top.z1();
			front.z1() = sink.z1() - 0.1*dz; // slightly below the sink basin
			front.d[c.dim][!c.dir] += dir_sign*0.94*depth;
			get_material(marble_tex, 1).add_cube_to_verts(front, top_color, tex_origin, EF_Z2); // front surface, no top face; same as top_mat
		}
		else if (c.type == TYPE_KSINK && get_dishwasher_for_ksink(c, dishwasher)) { // kitchen sink - add dishwasher if wide enough
			unsigned const dw_skip_faces(~get_face_mask(c.dim, !c.dir));
			colorRGBA const dw_color(apply_light_color(c, LT_GRAY));
			if (c.is_open()) {} // TODO: draw dishwasher as open? does it need to be a separate object for this?
			metal_mat.add_cube_to_verts_untextured(dishwasher, dw_color, dw_skip_faces);
			cube_t dishwasher_back(dishwasher), handle(dishwasher);
			dishwasher_back.d[c.dim][!c.dir] = c.d[c.dim][!c.dir]; // flush with the cabinet
			metal_mat.add_cube_to_verts_untextured(dishwasher_back, dw_color, ~dw_skip_faces); // draw only the back face, in case it's visible through a window
			handle.z1() += 0.77*dz;
			handle.z2() -= 0.10*dz;
			handle.expand_in_dim(!c.dim, -0.1*depth);
			handle.d[c.dim][ c.dir]  = handle.d[c.dim][!c.dir] = dishwasher.d[c.dim][c.dir];
			handle.d[c.dim][ c.dir] += dir_sign*0.04*depth; // front
			metal_mat.add_ortho_cylin_to_verts(handle, sink_color, !c.dim, 1, 1); // add handle as a cylinder in the proper dim with both ends
		}
	}
	else { // regular counter top
		top_mat.add_cube_to_verts(top, top_color, tex_origin); // top surface, all faces
	}
	if (c.type != TYPE_BRSINK) { // add wood sides of counter/cabinet
		float const overhang(0.05*depth);
		room_object_t cabinet(c);
		cabinet.z2() = top.z1();
		//cabinet.expand_in_dim(!c.dim, -overhang); // add side overhang: disable to allow cabinets to be flush with objects
		cabinet.d[c.dim][c.dir] -= dir_sign*overhang; // add front overhang
		if (!dishwasher.is_all_zeros()) {add_cabinet(split_cabinet_at_dishwasher(cabinet, dishwasher), tscale);}
		add_cabinet(cabinet, tscale); // draw the wood part
	}
	if (c.item_flags) { // add backsplash, 50% chance of tile vs. matching marble
		tid_nm_pair_t const bs_tex((c.room_id & 1) ? marble_tex : tid_nm_pair_t(get_texture_by_name("bathroom_tile.jpg"), 2.5*tscale));
		rgeom_mat_t &bs_mat(get_material(bs_tex, 0)); // no shadows
		cube_t bsz(c);
		bsz.z1()  = c.z2();
		bsz.z2() += 0.33*c.dz();

		if (c.item_flags & 1) { // back
			cube_t bs(bsz);
			bs.d[c.dim][c.dir] -= (c.dir ? 1.0 : -1.0)*0.99*depth;
			bs_mat.add_cube_to_verts(bs, top_color, zero_vector, (EF_Z1 | ~get_face_mask(c.dim, !c.dir)));
		}
		for (unsigned d = 0; d < 2; ++d) { // handle the other dim
			if (!(c.item_flags & (1<<(d+1)))) continue; // not adjacent in this dir
			cube_t bs(bsz);
			bs.d[!c.dim][!d] -= (d ? -1.0 : 1.0)*(c.get_width() - 0.01*depth);
			bs_mat.add_cube_to_verts(bs, top_color, zero_vector, (EF_Z1 | ~get_face_mask(!c.dim, d)));
		}
	}
}

float get_cabinet_doors(room_object_t const &c, vect_cube_t &doors) {
	float const cab_depth(c.get_depth()), door_height(0.8*c.dz()), door_thick(0.05*door_height);
	cube_t front(c);
	if (c.flags & RO_FLAG_ADJ_LO) {front.d[!c.dim][0] += cab_depth;} // exclude L-joins of cabinets from having doors; assumes all cabinets are the same depth
	if (c.flags & RO_FLAG_ADJ_HI) {front.d[!c.dim][1] -= cab_depth;}
	float const cab_width(front.get_sz_dim(!c.dim));
	if (cab_width < 0.0) return 0.0; // this seems to happen on occasion; maybe it's a bug, or maybe the random size parameters can lead to bad values; either way, skip it
	float door_width(0.75*door_height), door_spacing(1.2*door_width);
	unsigned const num_doors(floor(cab_width/door_spacing));
	if (num_doors == 0) return 0.0; // is this possible?
	assert(num_doors < 1000); // sanity check
	door_spacing = cab_width/num_doors;
	float const tb_border(0.5f*(c.dz() - door_height)), side_border(0.16*door_width), dir_sign(c.dir ? 1.0 : -1.0);
	door_width = (door_spacing - 2.0*side_border); // recalculate actual value
	float lo(front.d[!c.dim][0]);
	cube_t door0(c);
	door0.d[ c.dim][!c.dir]  = door0.d[c.dim][c.dir];
	door0.d[ c.dim][ c.dir] += dir_sign*door_thick; // expand out a bit
	door0.expand_in_dim(2, -tb_border); // shrink in Z

	for (unsigned n = 0; n < num_doors; ++n) {
		cube_t door(door0);
		float const hi(lo + door_spacing);
		door.d[!c.dim][0] = lo;
		door.d[!c.dim][1] = hi;
		door.expand_in_dim(!c.dim, -side_border); // shrink in XY
		doors.push_back(door);
		lo = hi; // advance to next door
	} // for n
	return door_width;
}
void get_cabinet_or_counter_doors(room_object_t const &c, vect_cube_t &doors) {
	doors.clear();
	if (c.type == TYPE_CABINET) {get_cabinet_doors(c, doors); return;}
	room_object_t cabinet(c), dishwasher; // start with counter
	cabinet.z2() -= 0.05*c.dz(); // remove counter top
	cabinet.d[c.dim][c.dir] -= (c.dir ? 1.0 : -1.0)*0.05*c.get_depth(); // add front overhang
	
	if (c.type == TYPE_KSINK && get_dishwasher_for_ksink(c, dishwasher)) {
		get_cabinet_doors(split_cabinet_at_dishwasher(cabinet, dishwasher), doors);
		doors.resize(8); // hack to force right side cabinet doors to use the correct set of second 8 drawer_flags bits
	}
	get_cabinet_doors(cabinet, doors);
}

void building_room_geom_t::add_cabinet(room_object_t const &c, float tscale) { // for kitchens
	assert(c.is_strictly_normalized());
	static vect_cube_t doors;
	doors.clear();
	float const door_width(get_cabinet_doors(c, doors)), dir_sign(c.dir ? 1.0 : -1.0);
	rgeom_mat_t &wood_mat(get_wood_material(tscale));
	bool const any_doors_open(c.drawer_flags > 0);
	colorRGBA const cabinet_color(apply_wood_light_color(c));
	unsigned skip_faces((c.type == TYPE_COUNTER) ? EF_Z12 : EF_Z2); // skip top face (can't skip back in case it's against a window)

	if (any_doors_open) {
		unsigned const skip_front_face(~get_face_mask(c.dim, c.dir));
		float const wall_thickness(0.04*c.dz());
		vect_cube_t &cubes(get_temp_cubes());
		cubes.push_back(c); // start with entire cabinet
		cube_t interior(c);
		interior.expand_by(-wall_thickness);
		wood_mat.add_cube_to_verts(interior, cabinet_color*0.5, tex_origin, skip_front_face, 0, 0, 0, 1); // darker interior; skip front face; inverted

		for (unsigned n = 0; n < doors.size(); ++n) { // draw open doors as holes
			if (!(c.drawer_flags & (1 << n))) continue; // not open
			cube_t hole(doors[n]), frame(hole);
			hole.expand_in_dim(c.dim, c.get_depth()); // expand so that it cuts entirely through the cabinet
			frame.d[c.dim][ c.dir]  = frame.d[c.dim][!c.dir];
			frame.d[c.dim][!c.dir] -= dir_sign*wall_thickness; // move inward by door thickness
			wood_mat.add_cube_to_verts(frame, cabinet_color, tex_origin, get_skip_mask_for_xy(c.dim), 0, 0, 0, 1); // skip front/back face; inverted
			subtract_cube_from_cubes(hole, cubes, nullptr, 1); // clip_in_z=1
		} // for n
		// draw front faces with holes cut in them for open doors
		for (auto i = cubes.begin(); i != cubes.end(); ++i) {wood_mat.add_cube_to_verts(*i, cabinet_color, tex_origin, ~skip_front_face);}
		skip_faces |= skip_front_face; // front face drawn above, don't draw it again below
	}
	wood_mat.add_cube_to_verts(c, cabinet_color, tex_origin, skip_faces); // draw wood exterior

	// add cabinet doors; maybe these should be small objects, but there are at most a few cabinets per house and none in office buildings
	if (doors.empty()) return; // no doors
	get_metal_material(0); // ensure material exists so that door_mat reference is not invalidated
	rgeom_mat_t &door_mat(get_wood_material(1.5*tscale, any_doors_open)); // only shadowed if a door is open
	rgeom_mat_t &handle_mat(get_metal_material(0)); // untextured, unshadowed
	colorRGBA const door_color(apply_light_color(c, WHITE)); // lighter color than cabinet
	colorRGBA const handle_color(apply_light_color(c, GRAY_BLACK));
	unsigned const door_skip_faces(~get_face_mask(c.dim, !c.dir));
	float const door_thick(doors[0].get_sz_dim(c.dim)), handle_thick(0.75*door_thick);
	float const hwidth(0.04*doors[0].dz()), near_side(0.1*door_width), far_side(door_width - near_side - hwidth);

	for (unsigned n = 0; n < doors.size(); ++n) {
		bool const is_open(c.drawer_flags & (1 << n)), handle_side(n & 1); // alternate handle side
		cube_t &door(doors[n]);

		if (is_open) { // make this door open
			door.d[ c.dim][c.dir] += dir_sign*(door_width - door_thick); // expand out to full width
			door.d[!c.dim][!handle_side] -= (handle_side ? -1.0f : 1.0f)*(door_width - door_thick); // shrink to correct thickness
		}
		door_mat.add_cube_to_verts(door, door_color, tex_origin, door_skip_faces);
		// add door handle
		cube_t handle(door);
		handle.d[c.dim][!c.dir]  = door.d[c.dim][c.dir];
		handle.d[c.dim][ c.dir] += dir_sign*handle_thick; // expand out a bit
		handle.expand_in_dim(2, -0.4*door.dz()); // shrink in Z

		if (is_open) { // rotate 90 degrees
			handle.d[!c.dim][!handle_side] = door.d[!c.dim][handle_side];
			handle.d[!c.dim][ handle_side] = door.d[!c.dim][handle_side] + (handle_side ? 1.0 : -1.0)*handle_thick; // expand out a bit
			handle.d[ c.dim][0] = door.d[c.dim][0] + (!c.dir ? near_side : far_side);
			handle.d[ c.dim][1] = door.d[c.dim][1] - (!c.dir ? far_side : near_side);
		}
		else {
			handle.d[!c.dim][0] = door.d[!c.dim][0] + (handle_side ? near_side : far_side);
			handle.d[!c.dim][1] = door.d[!c.dim][1] - (handle_side ? far_side : near_side);
		}
		handle_mat.add_cube_to_verts_untextured(handle, handle_color, door_skip_faces); // same skip_faces
	} // for n
}

void building_room_geom_t::add_window(room_object_t const &c, float tscale) { // frosted window blocks
	// Maybe windows should be refractive + blurred to simulate frosted glass?
	// - Using a separate drawing pass like reflections could be slow because there can be multiple windows in one bathroom,
	//   and they can be seen when the player is outside the bathroom.
	// - What about post-processing blur?
	//   - Can't use the stencil buffer because that's used (and cleared) by the main drawing code after drawing windows.
	//   - Drawing windows last won't properly alpha blend with other windows, showers, water, etc., and the depth buffer may be wrong.
	//   - Drawing windows in world space on the CPU (like blast effects) doesn't correctly handle occlusion by fragments of lower depth (such as interior walls).
	unsigned const skip_faces(get_skip_mask_for_xy(!c.dim) | EF_Z12); // only enable faces in dim
	cube_t window(c);
	tid_nm_pair_t tex(get_bath_wind_tid(), 0.0); // fit texture to the window front/back faces
	if (c.is_light_on()) {tex.emissive = 0.33;} // one third emissive
	get_material(tex, 0).add_cube_to_verts(window, c.color, c.get_llc(), skip_faces); // no apply_light_color()
}

colorRGBA const &get_outlet_or_switch_box_color(room_object_t const &c) {return (c.is_hanging() ? GRAY : c.color);} // should be silver metal

void building_room_geom_t::add_switch(room_object_t const &c, bool draw_detail_pass) { // light switch, etc.
	float const scaled_depth((c.is_hanging() ? 0.2 : 1.0)*c.get_length()); // non-recessed switch has smaller face plate depth
	room_object_t plate(c);
	plate.d[c.dim][!c.dir] -= (c.dir ? -1.0 : 1.0)*0.70*scaled_depth; // front face of plate

	if (draw_detail_pass) { // draw face plate (static detail)
		add_flat_textured_detail_wall_object(plate, get_outlet_or_switch_box_color(c), get_texture_by_name("interiors/light_switch.jpg"), 0); // draw_z1_face=0
	}
	else { // draw rocker (small object that can move/change state)
		float const width(c.get_width());
		cube_t rocker(c);
		set_wall_width(rocker, plate.d[c.dim][!c.dir], 0.15*scaled_depth, c.dim);
		rocker.expand_in_dim(!c.dim, -0.27*width); // shrink horizontally
		rocker.expand_in_dim(2,      -0.39*width); // shrink vertically
		rgeom_mat_t &mat(get_untextured_material(0, 0, 1)); // unshadowed, small
		unsigned const qv_start(mat.quad_verts.size());
		mat.add_cube_to_verts_untextured(rocker, c.color, (~get_face_mask(c.dim, c.dir) | EF_Z1)); // skip bottom face and face that's against the wall
		vector3d rot_axis(zero_vector);
		rot_axis[!c.dim] = ((c.dir ^ c.is_open()) ? 1.0 : -1.0);
		rotate_verts(mat.quad_verts, rot_axis, 0.015*PI, plate.get_cube_center(), qv_start); // rotate rocker slightly about base plate center; could be optimized by caching
	}
}

void building_room_geom_t::add_breaker(room_object_t const &c) {
	unsigned const skip_faces(~get_face_mask(c.dim, c.dir)); // skip face that's against the wall
	vector3d const sz(c.get_size());
	cube_t plate(c), rocker(c);
	plate.d[c.dim][!c.dir] -= (c.dir ? -1.0 : 1.0)*0.70*sz[c.dim]; // front face of plate
	set_wall_width(rocker, plate.d[c.dim][!c.dir], 1.0*sz[c.dim], c.dim); // stick out more than light switches
	rocker.expand_in_dim(!c.dim, -0.47*sz[!c.dim]); // shrink horizontally
	rocker.expand_in_dim(2,      -0.30*sz.z      ); // shrink vertically
	rgeom_mat_t &mat(get_untextured_material(0, 0, 1)); // unshadowed, small
	mat.add_cube_to_verts_untextured(plate, apply_light_color(c), skip_faces);
	unsigned const qv_start(mat.quad_verts.size());
	mat.add_cube_to_verts_untextured(rocker, apply_light_color(c, WHITE), skip_faces);
	vector3d const rot_axis(0.0, 0.0, (c.is_open() ? 1.0 : -1.0));
	rotate_verts(mat.quad_verts, rot_axis, 0.12*PI, plate.get_cube_center(), qv_start); // rotate rocker slightly about base plate center
}

void building_room_geom_t::add_flat_textured_detail_wall_object(room_object_t const &c, colorRGBA const &side_color, int tid, bool draw_z1_face) { // uses mats_detail
	rgeom_mat_t &front_mat(get_material(tid_nm_pair_t(tid, 0.0, 0), 0, 0, 2)); // small=2/detail
	front_mat.add_cube_to_verts(c, c.color, zero_vector, get_face_mask(c.dim, !c.dir), !c.dim); // textured front face; always fully lit to match wall
	unsigned const skip_faces(get_skip_mask_for_xy(c.dim) | (draw_z1_face ? EF_Z1 : 0)); // skip front/back and maybe bottom faces
	get_untextured_material(0, 0, 2).add_cube_to_verts_untextured(c, side_color, skip_faces); // sides: unshadowed, small
}
void building_room_geom_t::add_outlet(room_object_t const &c) {
	add_flat_textured_detail_wall_object(c, get_outlet_or_switch_box_color(c), get_texture_by_name("interiors/outlet1.jpg"), 1); // draw_z1_face=1 (optimization)
}
void building_room_geom_t::add_vent(room_object_t const &c) {
	int const tid(get_texture_by_name("interiors/vent.jpg"));

	if (c.is_hanging()) { // vent on a ceiling
		rgeom_mat_t &front_mat(get_material(tid_nm_pair_t(tid, 0.0, 0), 0, 0, 2)); // small=2/detail
		front_mat.add_cube_to_verts(c, c.color, zero_vector, ~EF_Z1, !c.dim); // textured bottom face; always fully lit to match wall
		get_untextured_material(0, 0, 2).add_cube_to_verts_untextured(c, c.color, EF_Z12); // sides: unshadowed, small; skip top and bottom face
	}
	else {add_flat_textured_detail_wall_object(c, c.color, tid, 0);} // vent on a wall; draw_z1_face=0
}

void building_room_geom_t::add_plate(room_object_t const &c) { // is_small=1
	// select plate texture based on room and a property of this building; plates in the same room will match
	unsigned const NUM_PLATE_TEXTURES = 6;
	string const plate_textures[NUM_PLATE_TEXTURES] = {"plates/plate1.png", "plates/plate2.jpg", "plates/plate3.jpg", "plates/plate4.jpg", "plates/plate5.jpg", "plates/plate6.jpg"};
	int const tid(get_texture_by_name(plate_textures[(c.room_id + stairs_start) % NUM_PLATE_TEXTURES]));
	rgeom_mat_t &top_mat(get_material(tid_nm_pair_t(tid, 0.0, 0), 0, 0, 1)); // unshadowed, small
	colorRGBA color(apply_light_color(c));
	UNROLL_3X(min_eq(color[i_], 0.9f);); // clamp color to 90% max to avoid over saturation
	top_mat.add_vcylin_to_verts(c, color, 0, 1, 0, 0, 1.0, 1.0, 1.0, 1.0, 1); // top surface, skip sides
	get_untextured_material(0, 0, 1).add_vcylin_to_verts(c, color, 0, 0, 0, 0, 0.8, 1.0); // bottom: truncated cone, untextured, unshadowed, sloped sides only
}

void building_room_geom_t::add_tub_outer(room_object_t const &c) {
	colorRGBA const color(apply_light_color(c));
	get_untextured_material(1).add_cube_to_verts_untextured(c, color, EF_Z12); // shadowed, no top/bottom faces
}

void building_room_geom_t::add_crack(room_object_t const &c) { // in window? (TV and computer monitor cracks are drawn below)
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_crack_tid(c), 0.0, 0), 0, 0, 1)); // unshadowed, small
	mat.add_cube_to_verts(c, apply_light_color(c), all_zeros, get_face_mask(c.dim, c.dir), !c.dim, (c.obj_id&1), (c.obj_id&2)); // X/Y mirror based on obj_id
}

void building_room_geom_t::add_tv_picture(room_object_t const &c) {
	if ((c.obj_id & 1) && !c.is_broken()) return; // TV is off half the time
	cube_t screen(c);
	screen.d[c.dim][c.dir] += (c.dir ? -1.0 : 1.0)*0.35*c.get_depth();
	screen.expand_in_dim(!c.dim, -0.03*c.get_width()); // shrink the sides in
	screen.z1() += 0.09*c.dz();
	screen.z2() -= 0.04*c.dz();
	unsigned skip_faces(get_face_mask(c.dim, c.dir)); // only the face oriented outward

	if (c.is_broken()) {
		rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_crack_tid(c), 0.0)));
		mat.add_cube_to_verts(screen, apply_light_color(c, WHITE), c.get_llc(), skip_faces, !c.dim, (c.obj_id&1), (c.obj_id&2)); // X/Y mirror based on obj_id
	}
	else {
		tid_nm_pair_t tex(((c.shape == SHAPE_SHORT) ? c.get_comp_monitor_tid() : c.get_tv_tid()), 0.0); // computer monitor vs. TV
		tex.emissive = 1.0;
		get_material(tex).add_cube_to_verts(screen, WHITE, c.get_llc(), skip_faces, !c.dim, !(c.dim ^ c.dir));
	}
}

void building_room_geom_t::add_potted_plant(room_object_t const &c, bool inc_pot, bool inc_plant) {
	float const plant_diameter(2.0*c.get_radius()), stem_radius(0.04*plant_diameter);
	float const pot_height(max(0.6*plant_diameter, 0.3*c.dz())), pot_top(c.z1() + pot_height), dirt_level(pot_top - 0.15*pot_height);
	float const cx(c.get_center_dim(0)), cy(c.get_center_dim(1));
	point const base_pos(cx, cy, dirt_level); // base of plant trunk, center of dirt disk

	if (inc_pot) {
		// draw the pot, tapered with narrower bottom; draw the bottom of the pot if there's no dirt
		bool const no_dirt(c.taken_level > 1);
		float const pot_radius(0.4*plant_diameter);
		get_untextured_material(1).add_cylin_to_verts(point(cx, cy, c.z1()), point(cx, cy, pot_top), 0.65*pot_radius, pot_radius, apply_light_color(c), no_dirt, 0, 1, 0);
		
		if (!no_dirt) { // draw dirt in the pot as a disk if not taken
			rgeom_mat_t &dirt_mat(get_material(tid_nm_pair_t(get_texture_by_name("rock2.png")), 1)); // use dirt texture
			dirt_mat.add_disk_to_verts(base_pos, 0.947*pot_radius, 0, apply_light_color(c, WHITE));
		}
	}
	if (inc_plant && c.taken_level == 0) { // plant not taken
		// draw plant leaves
		s_plant plant;
		plant.create_no_verts(base_pos, (c.z2() - base_pos.z), stem_radius, c.obj_id, 0, 1); // land_plants_only=1
		static vector<vert_norm_comp> points;
		points.clear();
		plant.create_leaf_points(points, 10.0, 1.5, 4); // plant_scale=10.0 seems to work well; more levels and rings
		auto &leaf_verts(mats_amask.get_material(tid_nm_pair_t(plant.get_leaf_tid()), 1).quad_verts);
		color_wrapper const leaf_cw(apply_light_color(c, plant.get_leaf_color()));
		float const ts[4] = {0,1,1,0}, tt[4] = {0,0,1,1};
		for (unsigned i = 0; i < points.size(); ++i) {leaf_verts.emplace_back(vert_norm_comp_tc(points[i], ts[i&3], tt[i&3]), leaf_cw);}
		// draw plant stem
		colorRGBA const stem_color(plant.get_stem_color());
		mats_amask.get_material(get_tex_auto_nm(WOOD2_TEX), 1).add_cylin_to_verts(point(cx, cy, base_pos.z), point(cx, cy, c.z2()), stem_radius, 0.0f, stem_color, 0, 0); // stem
	}
}

int get_lg_ball_tid   (room_object_t const &c) {return get_texture_by_name((c.item_flags & 1) ? "interiors/basketball.png" : "interiors/soccer_ball_diffuse.png");}
int get_lg_ball_nm_tid(room_object_t const &c) {return ((c.item_flags & 1) ? -1 : get_texture_by_name("interiors/soccer_ball_normal.png", 1));}

xform_matrix get_player_cview_rot_matrix() {
	float const angle(atan2(cview_dir.y, cview_dir.x)); // angle of camera view in XY plane, for rotating about Z
	return get_rotation_matrix(plus_z, angle);
}

void building_room_geom_t::add_lg_ball(room_object_t const &c) { // is_small=1
	bool const dynamic(c.is_dynamic()); // either small or dynamic
	rgeom_mat_t &mat(get_material(tid_nm_pair_t(get_lg_ball_tid(c), get_lg_ball_nm_tid(c), 0.0, 0.0), 1, dynamic, !dynamic));
	// rotate the texture coords when the ball is rolling
	mat.add_sphere_to_verts(c, apply_light_color(c), 0, zero_vector, (c.has_dstate() ? &get_dstate(c).rot_matrix : nullptr)); // low_detail=0
}
/*static*/ void building_room_geom_t::draw_lg_ball_in_building(room_object_t const &c, shader_t &s) {
	//highres_timer_t timer("Draw Ball"); // 0.105ms
	xform_matrix const rot_matrix(get_player_cview_rot_matrix());
	// Note: since we're using indexed triangles now, we can't simply call draw_quad_verts_as_tris(); instead we create temp VBO/IBO; not the most efficient solution, but it should work
	static rgeom_mat_t mat = rgeom_mat_t(tid_nm_pair_t()); // allocated memory is reused across frames; VBO/IBO are recreated every time
	mat.tex = tid_nm_pair_t(get_lg_ball_tid(c), get_lg_ball_nm_tid(c), 0.0, 0.0);
	mat.add_sphere_to_verts(c, apply_light_color(c), 0, zero_vector, &rot_matrix); // low_detail=0
	tid_nm_pair_dstate_t state(s);
	mat.upload_draw_and_clear(state);
}

void building_room_geom_t::add_toy(room_object_t const &c) { // is_small=1
	tid_nm_pair_t tex(-1, 1.0, 1); // shadowed
	tex.set_specular(0.5, 80.0);
	rgeom_mat_t &mat(get_material(tex, 1, 0, 1)); // shadowed, small
	point const center(c.get_cube_center());
	float const height(c.dz()), radius(c.get_radius()), post_top_srcale(0.6), post_rb(0.45*radius), base_height(0.10*height);
	float zval(c.z1() + base_height); // top of base/bottom of post
	// draw the center post as a truncated cone
	colorRGBA const base_color(apply_light_color(c));
	cube_t post(center);
	post.expand_by_xy(post_rb);
	set_cube_zvals(post, zval, c.z2());
	mat.add_vcylin_to_verts(post, base_color, 0, 1, 0, 0, 1.0, post_top_srcale);
	// use squished cylinder on the bottom and a cube on the top for the base
	cube_t base(c);
	base.z2() = base.z1() + 2.0*radius;
	unsigned const base_start(mat.itri_verts.size());
	mat.add_ortho_cylin_to_verts(base, base_color, c.dim, 1, 1);
	// squish in Z
	float const z_squish_factor(base_height/(2.0*radius));
	for (auto i = mat.itri_verts.begin()+base_start; i != mat.itri_verts.end(); ++i) {i->v.z = z_squish_factor*(i->v.z - c.z1()) + c.z1();}
	set_cube_zvals(base, (c.z1() + 0.5*base_height), zval); // top half
	mat.add_cube_to_verts_untextured(base, base_color, EF_Z1); // skip bottom face
	// draw the rings
	unsigned const num_rings(4), num_ring_colors(6);
	colorRGBA const ring_colors[num_ring_colors] = {RED, GREEN, BLUE, YELLOW, ORANGE, PURPLE};
	assert(c.taken_level <= 4);
	unsigned const rings_to_draw(num_rings - c.taken_level); // remove rings from the top if they're taken by the player
	unsigned colors_used(0);
	rand_gen_t rgen;
	rgen.set_state(c.obj_id, c.obj_id);
	rgen.rand_mix();

	for (unsigned n = 0; n < rings_to_draw; ++n) {
		unsigned color_id(0);

		for (unsigned m = 0; m < 100; ++m) {
			color_id = rgen.rand() % num_ring_colors;
			if (colors_used & (1<<color_id)) continue; // color already used, try again
			colors_used |= (1<<color_id);
			break;
		}
		float const ro(0.8*(1.0 - 0.07*n)*radius), ri(0.42*ro);
		mat.add_vert_torus_to_verts(point(center.x, center.y, zval+ri), ri, ro, apply_light_color(c, ring_colors[color_id]), 1.0, 0); // tscale=1.0, low_detail=0
		zval += 2.0*ri; // move up
	} // for n
}

void building_room_geom_t::add_pan(room_object_t const &c) { // is_small=1
	colorRGBA const color(apply_light_color(c));
	rgeom_mat_t &mat(get_metal_material(1, 0, 1)); // shadowed, small
	mat.add_vcylin_to_verts(c, color, 1, 0, 1, 1, 0.8, 1.0);
	// add handle
	float const diameter(c.get_sz_dim(!c.dim)), handle_radius(0.08*diameter), edge_pos(c.d[!c.dim][c.dir]);
	cube_t handle;
	handle.d[!c.dim][!c.dir] = edge_pos;
	handle.d[!c.dim][ c.dir] = edge_pos + (c.dir ? 1.0 : -1.0)*0.6*diameter;
	set_wall_width(handle, c.get_center_dim(c.dim), handle_radius, c.dim);
	set_cube_zvals(handle, (c.z2() - 2.0*handle_radius), c.z2());
	unsigned const base_start(mat.itri_verts.size());
	mat.add_ortho_cylin_to_verts(handle, color, !c.dim, 1, 1);
	// squish in Z by 50% from z2
	for (auto i = mat.itri_verts.begin()+base_start; i != mat.itri_verts.end(); ++i) {i->v.z = c.z2() - 0.5*(c.z2() - i->v.z);}
}

colorRGBA room_object_t::get_color() const {
	switch (type) {
	case TYPE_TABLE:    return get_table_color(*this);
	case TYPE_CHAIR:    return (color + get_textured_wood_color())*0.5; // 50% seat color / 50% wood legs color
	case TYPE_STAIR:    return (STAIRS_COLOR_TOP*0.5 + STAIRS_COLOR_BOT*0.5).modulate_with(texture_color(MARBLE_TEX));
	case TYPE_STAIR_WALL: return texture_color(STUCCO_TEX);
	case TYPE_PG_WALL:    return texture_color(STUCCO_TEX);
	case TYPE_PARK_SPACE: return LT_GRAY; // texture_color(...)?
	case TYPE_ELEVATOR: return LT_BROWN; // ???
	case TYPE_RUG:      return texture_color(get_rug_tid());
	case TYPE_PICTURE:  return texture_color(get_picture_tid());
	case TYPE_BCASE:    return get_textured_wood_color();
	case TYPE_WINE_RACK:return get_textured_wood_color();
	case TYPE_DESK:     return get_textured_wood_color();
	case TYPE_RDESK:    return (texture_color(PANELING_TEX)*0.5 + texture_color(get_counter_tid())*0.5);
	case TYPE_BED:      return (color.modulate_with(texture_color(get_sheet_tid())) + get_textured_wood_color())*0.5; // half wood and half cloth
	case TYPE_COUNTER:  return get_counter_color();
	case TYPE_KSINK:    return (get_counter_color()*0.9 + GRAY*0.1); // counter, with a bit of gray mixed in from the sink
	case TYPE_BRSINK:   return texture_color(get_counter_tid()).modulate_with(color);
	case TYPE_CABINET:  return get_textured_wood_color();
	case TYPE_PLANT:    return (color*0.75 + blend_color(GREEN, BROWN, 0.5, 0)*0.25); // halfway between green and brown, as a guess; mix in 75% of pot color
	case TYPE_CLOSET:   return (color*0.5 + WHITE*0.5); // half white door and half wall color
	case TYPE_DRESSER:  return  get_textured_wood_color();
	case TYPE_NIGHTSTAND:return get_textured_wood_color();
	case TYPE_FLOORING: return texture_color(get_flooring_texture(*this)).modulate_with(color);
	case TYPE_CRATE:    return texture_color(get_crate_tid(*this)).modulate_with(color);
	case TYPE_BOX:      return texture_color(get_box_tid()).modulate_with(color);
	case TYPE_CUBICLE:  return texture_color(get_cubicle_tid(*this));
	case TYPE_SHELVES:  return get_textured_wood_color();
	case TYPE_KEYBOARD: return BKGRAY;
	case TYPE_COMPUTER: return BKGRAY;
	case TYPE_MWAVE:    return GRAY;
	case TYPE_SHOWER:   return colorRGBA(WHITE, 0.25); // partially transparent - does this actually work?
	case TYPE_BLINDS:   return texture_color(get_blinds_tid()).modulate_with(color);
	case TYPE_LG_BALL:  return texture_color(get_lg_ball_tid(*this));
	case TYPE_HANGER_ROD:return get_textured_wood_color();
	case TYPE_MONEY:    return texture_color(get_money_tid());
	case TYPE_PHONE:    return color*0.5; // 50% case color, 50% black
	case TYPE_LAPTOP:   return BKGRAY; // black-gray case, ignore logo colors
	case TYPE_TPROLL:   return (WHITE*0.75  + GRAY*0.25);
	case TYPE_SPRAYCAN: return (DK_GRAY*0.5 + color*0.5);
	case TYPE_CRACK:    return ALPHA0; // transparent
	case TYPE_FPLACE:   return texture_color(BRICK2_TEX).modulate_with(color);
	case TYPE_WHEATER:  return GRAY;
	case TYPE_FURNACE:  return get_furnace_color();
	case TYPE_ATTIC_DOOR:return get_textured_wood_color();
	case TYPE_DUCT:     return texture_color((shape == SHAPE_CYLIN) ? get_cylin_duct_tid() : get_cube_duct_tid()).modulate_with(color);
	//case TYPE_CHIMNEY:  return texture_color(get_material().side_tex); // should modulate with texture color, but we don't have it here
	default: return color; // TYPE_LIGHT, TYPE_TCAN, TYPE_BOOK, TYPE_BOTTLE, TYPE_PEN_PENCIL, etc.
	}
	if (is_obj_model_type()) {return color.modulate_with(get_model_color());} // handle models
	return color; // Note: probably should always set color so that we can return it here
}

