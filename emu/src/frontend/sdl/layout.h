// Front-panel artwork in a subset of MAME's .lay format (version 2), so the panels
// can go to MAME's layout/ directory when the machines are upstreamed:
//   <element name="..."> with <rect>/<disk>/<text string="..."> components, each with
//     optional state="0|1", <color red green blue [alpha]> and <bounds x y width height>;
//   <view name="..."> with <bounds> and <element ref="..." name="OUTPUT"
//     inputtag="INPUT"> items, each with <bounds>.
// Bindings use tek8550_system's names: an item's name= is an output (lamp state,
// drawn by intensity), inputtag= an input (pressed with the mouse). Extensions: an
// inputtag "driveN:eject" ejects drive N; an item with drive="N" is drive N's bay
// (the mounted image's name is shown in it, and discs are dropped on it).
#ifndef TEK_FRONTEND_SDL_LAYOUT_H
#define TEK_FRONTEND_SDL_LAYOUT_H
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace lay {

struct color { float r = 1, g = 1, b = 1, a = 1; };
struct rect { float x = 0, y = 0, w = 1, h = 1; };

struct component {
	enum kind_t { RECT, DISK, TEXT } kind = RECT;
	int state = -1;                 // -1 = always drawn
	color col;
	std::string text;
	rect b; bool has_bounds = false;
};

struct element { std::vector<component> comps; rect extent; };

struct item {
	std::string ref, output, input;
	int drive = -1;
	int rotate = 0;                 // <orientation rotate="90|270">: text runs vertically
	rect b;
};

struct view { std::string name; rect b; std::vector<item> items; };

class layout
{
public:
	bool load(const std::string &path, std::string &err);
	std::map<std::string, element> elements;
	std::vector<view> views;
};

// draw a view; level(item) is the item's state as an intensity 0..1, caption(item)
// is extra text for drive bays
void draw(SDL_Renderer *r, const layout &l, const view &v,
	const std::function<float(const item &)> &level,
	const std::function<std::string(const item &)> &caption);
// text in the CT8500 font, fitted to a box and centred
void draw_text(SDL_Renderer *r, const std::string &s, const rect &box, const color &c, int rotate = 0);
const item *hit(const view &v, float x, float y, bool (*want)(const item &));

}
#endif
