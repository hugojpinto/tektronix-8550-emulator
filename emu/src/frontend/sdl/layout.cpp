// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// MAME .lay subset -- see layout.h
#include "frontend/sdl/layout.h"
#include "host/ct8500_raster.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace lay {

namespace {

// a minimal XML reader: elements and attributes; text, comments, <? ?> skipped
struct node { std::string tag; std::map<std::string, std::string> attr; std::vector<node> kids; };

std::string unescape(const std::string &s)
{
	std::string o;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '&') { o += s[i]; continue; }
		size_t e = s.find(';', i);
		if (e == std::string::npos) { o += s[i]; continue; }
		std::string ent = s.substr(i + 1, e - i - 1);
		if (ent == "amp") o += '&'; else if (ent == "lt") o += '<'; else if (ent == "gt") o += '>';
		else if (ent == "quot") o += '"'; else if (ent == "apos") o += '\'';
		else if (!ent.empty() && ent[0] == '#') o += char(strtol(ent.c_str() + 1 + (ent[1] == 'x'), nullptr, ent[1] == 'x' ? 16 : 10));
		i = e;
	}
	return o;
}

bool parse_xml(const std::string &s, node &root, std::string &err)
{
	std::vector<node *> stack = { &root };
	size_t i = 0;
	while ((i = s.find('<', i)) != std::string::npos) {
		if (s.compare(i, 4, "<!--") == 0) { i = s.find("-->", i); if (i == std::string::npos) break; i += 3; continue; }
		if (s.compare(i, 2, "<?") == 0) { i = s.find("?>", i); if (i == std::string::npos) break; i += 2; continue; }
		size_t e = s.find('>', i);
		if (e == std::string::npos) { err = "unterminated tag"; return false; }
		std::string t = s.substr(i + 1, e - i - 1);
		i = e + 1;
		if (!t.empty() && t[0] == '/') {
			if (stack.size() < 2) { err = "unbalanced </" + t.substr(1) + ">"; return false; }
			stack.pop_back();
			continue;
		}
		bool selfclose = !t.empty() && t.back() == '/';
		if (selfclose) t.pop_back();
		node n;
		size_t p = 0;
		while (p < t.size() && !isspace(uint8_t(t[p]))) p++;
		n.tag = t.substr(0, p);
		while (p < t.size()) {
			while (p < t.size() && isspace(uint8_t(t[p]))) p++;
			size_t k = p; while (p < t.size() && t[p] != '=' && !isspace(uint8_t(t[p]))) p++;
			std::string key = t.substr(k, p - k);
			while (p < t.size() && t[p] != '"' && t[p] != '\'') p++;
			if (p >= t.size()) break;
			char q = t[p++]; size_t v = p; while (p < t.size() && t[p] != q) p++;
			if (!key.empty()) n.attr[key] = unescape(t.substr(v, p - v));
			p++;
		}
		stack.back()->kids.push_back(n);
		if (!selfclose) stack.push_back(&stack.back()->kids.back());
	}
	return true;
}

float num(const node &n, const char *k, float d)
{
	auto it = n.attr.find(k);
	return it == n.attr.end() ? d : float(atof(it->second.c_str()));
}

rect bounds_of(const node &n, bool *found = nullptr)
{
	for (const node &k : n.kids)
		if (k.tag == "bounds") {
			if (found) *found = true;
			rect b; b.x = num(k, "x", 0); b.y = num(k, "y", 0);
			b.w = num(k, "width", num(k, "right", 1) - b.x); b.h = num(k, "height", num(k, "bottom", 1) - b.y);
			return b;
		}
	if (found) *found = false;
	return rect();
}

color color_of(const node &n)
{
	color c;
	for (const node &k : n.kids)
		if (k.tag == "color") { c.r = num(k, "red", 1); c.g = num(k, "green", 1); c.b = num(k, "blue", 1); c.a = num(k, "alpha", 1); }
	return c;
}

SDL_FColor fc(const color &c, float alpha) { return { c.r, c.g, c.b, c.a * alpha }; }

void fill_rect(SDL_Renderer *r, const rect &b, const color &c, float alpha)
{
	SDL_SetRenderDrawColorFloat(r, c.r, c.g, c.b, c.a * alpha);
	SDL_FRect f = { b.x, b.y, b.w, b.h };
	SDL_RenderFillRect(r, &f);
}

void fill_disk(SDL_Renderer *r, const rect &b, const color &c, float alpha)
{
	const int N = 32;
	SDL_Vertex v[N + 1]; int idx[N * 3];
	float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
	v[0] = { { cx, cy }, fc(c, alpha), { 0, 0 } };
	for (int i = 0; i < N; i++) {
		float a = float(i) * 6.2831853f / N;
		v[i + 1] = { { cx + std::cos(a) * b.w / 2, cy + std::sin(a) * b.h / 2 }, fc(c, alpha), { 0, 0 } };
		idx[i * 3] = 0; idx[i * 3 + 1] = 1 + i; idx[i * 3 + 2] = 1 + (i + 1) % N;
	}
	SDL_RenderGeometry(r, nullptr, v, N + 1, idx, N * 3);
}

}

void draw_text(SDL_Renderer *r, const std::string &s, const rect &box, const color &c, int rotate)
{
	if (s.empty()) return;
	const bool vert = rotate == 90 || rotate == 270;
	const float along = vert ? box.h : box.w, across = vert ? box.w : box.h;
	float px = across / 8.f;                                     // dot size: 8 rows per line
	if (s.size() * 6 * px > along) px = along / float(s.size() * 6);
	const float a0 = (along - float(s.size()) * 6 * px + px) / 2, c0 = (across - 8 * px) / 2;
	const float d = px < 3 ? px : px * 0.9f;                     // small text: solid dots stay visible
	SDL_SetRenderDrawColorFloat(r, c.r, c.g, c.b, c.a);
	for (size_t i = 0; i < s.size(); i++) {
		const uint8_t *g = ct8500_raster::glyph(uint8_t(s[i]));
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 5; x++) {
				if (!(g[y] >> (4 - x) & 1)) continue;
				float u = a0 + (float(i) * 6 + float(x)) * px, v = c0 + float(y) * px;   // along, across
				SDL_FRect f;
				if (rotate == 270) f = { box.x + v, box.y + box.h - u - d, d, d };        // reads bottom to top
				else if (rotate == 90) f = { box.x + box.w - v - d, box.y + u, d, d };    // reads top to bottom
				else f = { box.x + u, box.y + v, d, d };
				SDL_RenderFillRect(r, &f);
			}
	}
}

bool layout::load(const std::string &path, std::string &err)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { err = "cannot open " + path; return false; }
	std::string s; char buf[4096]; size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
	fclose(f);
	node root;
	if (!parse_xml(s, root, err)) { err = path + ": " + err; return false; }
	const node *ml = nullptr;
	for (const node &k : root.kids) if (k.tag == "mamelayout") ml = &k;
	if (!ml) { err = path + ": no <mamelayout>"; return false; }
	for (const node &k : ml->kids) {
		if (k.tag == "element") {
			element e;
			bool first = true;
			for (const node &c : k.kids) {
				component cp;
				if (c.tag == "rect") cp.kind = component::RECT;
				else if (c.tag == "disk") cp.kind = component::DISK;
				else if (c.tag == "text") { cp.kind = component::TEXT; cp.text = c.attr.count("string") ? c.attr.at("string") : ""; }
				else continue;
				cp.state = c.attr.count("state") ? atoi(c.attr.at("state").c_str()) : -1;
				cp.col = color_of(c);
				cp.b = bounds_of(c, &cp.has_bounds);
				if (first) { e.extent = cp.b; first = false; }
				else {                                              // extent = union of the components
					float x1 = std::max(e.extent.x + e.extent.w, cp.b.x + cp.b.w), y1 = std::max(e.extent.y + e.extent.h, cp.b.y + cp.b.h);
					e.extent.x = std::min(e.extent.x, cp.b.x); e.extent.y = std::min(e.extent.y, cp.b.y);
					e.extent.w = x1 - e.extent.x; e.extent.h = y1 - e.extent.y;
				}
				e.comps.push_back(cp);
			}
			elements[k.attr.count("name") ? k.attr.at("name") : ""] = e;
		} else if (k.tag == "view") {
			view v;
			v.name = k.attr.count("name") ? k.attr.at("name") : "";
			v.b = bounds_of(k);
			for (const node &c : k.kids) {
				if (c.tag != "element") continue;
				item it;
				it.ref = c.attr.count("ref") ? c.attr.at("ref") : "";
				if (c.attr.count("name")) it.output = c.attr.at("name");
				if (c.attr.count("inputtag")) it.input = c.attr.at("inputtag");
				if (c.attr.count("drive")) it.drive = atoi(c.attr.at("drive").c_str());
				for (const node &o : c.kids) if (o.tag == "orientation") it.rotate = int(num(o, "rotate", 0));
				it.b = bounds_of(c);
				if (!elements.count(it.ref)) { err = path + ": unknown element " + it.ref; return false; }
				v.items.push_back(it);
			}
			views.push_back(v);
		}
	}
	return true;
}

void draw(SDL_Renderer *r, const layout &l, const view &v,
	const std::function<float(const item &)> &level,
	const std::function<std::string(const item &)> &caption)
{
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	for (const item &it : v.items) {
		const element &e = l.elements.at(it.ref);
		float lv = level(it);
		for (const component &c : e.comps) {
			float alpha = c.state < 0 ? 1.f : c.state == 0 ? 1.f : lv;   // state 1 fades in over state 0
			if (alpha <= 0.003f) continue;
			// component bounds are in the element's own units, scaled into the item
			rect b = it.b;
			if (e.extent.w > 0 && e.extent.h > 0) {
				b.x = it.b.x + (c.b.x - e.extent.x) / e.extent.w * it.b.w;
				b.y = it.b.y + (c.b.y - e.extent.y) / e.extent.h * it.b.h;
				b.w = c.b.w / e.extent.w * it.b.w; b.h = c.b.h / e.extent.h * it.b.h;
			}
			switch (c.kind) {
			case component::RECT: fill_rect(r, b, c.col, alpha); break;
			case component::DISK: fill_disk(r, b, c.col, alpha); break;
			case component::TEXT: { color k = c.col; k.a *= alpha; draw_text(r, c.text, b, k, it.rotate); break; }
			}
		}
		std::string cap = caption(it);
		if (!cap.empty()) {
			rect b = { it.b.x + it.b.w * 0.63f, it.b.y + it.b.h * 0.64f, it.b.w * 0.34f, it.b.h * 0.13f };   // where the property sticker is
			fill_rect(r, b, { 0.93f, 0.91f, 0.84f, 1 }, 1);
			draw_text(r, cap, { b.x + 3, b.y + 2, b.w - 6, b.h - 4 }, { 0.12f, 0.12f, 0.14f, 1 });
		}
	}
}

const item *hit(const view &v, float x, float y, bool (*want)(const item &))
{
	for (auto it = v.items.rbegin(); it != v.items.rend(); ++it)
		if (want(*it) && x >= it->b.x && x < it->b.x + it->b.w && y >= it->b.y && y < it->b.y + it->b.h) return &*it;
	return nullptr;
}

}
