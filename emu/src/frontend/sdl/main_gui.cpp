// tek8550-gui: the complete 8550 MDL with a graphical CT8500 (SDL3).
// The same system and options as the tty front end (system/tek8550.*); this file only
// draws the terminal, takes the keyboard and paces the emulation to real time.
//   tek8550-gui [tek8550 options] [-style crt|sharp] [-scale F (default: fit the screen)] [-panels on|off] [-artwork DIR] [-shot SECONDS FILE.bmp]
// With -cycles the run is flat out (batch); -shot saves the window at a machine time
// (SDL_VIDEODRIVER=offscreen runs it with no display).
// Hotkeys (Cmd on macOS, Ctrl+Alt elsewhere): V paste, C copy the screen, T turbo,
// F full screen, S sharp/CRT, P front panels, L fit the windows to the screen, R restart both
// units, Q quit.
// Front panels: artwork/8301.lay and 8501.lay (MAME .lay subset, frontend/sdl/layout.h);
// click RESTART, drop a disc image on a drive bay to insert it, click the bay to eject.
#include "system/tek8550.h"
#include "host/ct8500_raster.h"
#include "frontend/sdl/layout.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <cmath>
#include <map>
#include <algorithm>

namespace {

constexpr int RW = ct8500_raster::W, RH = ct8500_raster::H;   // 640 x 250 dots
constexpr int TW = RW, TH = RH * 2;                            // each scan line doubled: 640 x 500
constexpr int OVER_X = 40, OVER_Y = 32;                        // tube border around the raster
constexpr int LW = TW + 2 * OVER_X, LH = TH + 2 * OVER_Y;      // logical canvas

struct rgb { float r, g, b; };
const rgb BG = { 4, 8, 5 }, GREEN = { 70, 255, 120 }, GREEN_DIM = { 35, 150, 70 };   // as the screenshots

struct terminal_view {
	bool crt = true;
	std::vector<uint8_t> dots = std::vector<uint8_t>(size_t(RW) * RH);
	std::vector<float> lum[3];
	std::vector<uint32_t> px = std::vector<uint32_t>(size_t(TW) * TH);

	void compose()
	{
		for (auto &l : lum) l.assign(size_t(TW) * TH, 0.f);
		for (int y = 0; y < RH; y++)
			for (int x = 0; x < RW; x++) {
				uint8_t v = dots[size_t(y) * RW + x];
				if (!v) continue;
				const rgb &c = v == 2 ? GREEN : GREEN_DIM;
				for (int k = 0; k < 2; k++) {
					float f = (crt && k) ? 0.55f : 1.f;               // second scan line dimmer
					size_t i = size_t(y * 2 + k) * TW + x;
					lum[0][i] = c.r * f; lum[1][i] = c.g * f; lum[2][i] = c.b * f;
				}
			}
		if (crt) bloom();
		for (size_t i = 0; i < px.size(); i++) {
			auto ch = [&](int k, float bg) { float v = bg + lum[k][i]; return uint32_t(v > 255 ? 255 : v); };
			px[i] = 0xff000000u | ch(0, BG.r) << 16 | ch(1, BG.g) << 8 | ch(2, BG.b);
		}
	}
	// phosphor glow: a blurred copy added back (two box passes per axis)
	void bloom()
	{
		std::vector<float> tmp(size_t(TW) * TH), blur(size_t(TW) * TH);
		const int R = 3;
		for (int k = 0; k < 3; k++) {
			const std::vector<float> &src = lum[k];
			for (int y = 0; y < TH; y++) {                         // horizontal
				float acc = 0; const float *s = &src[size_t(y) * TW]; float *d = &tmp[size_t(y) * TW];
				for (int x = -R; x < TW + R; x++) {
					if (x + R < TW) acc += s[x + R];
					if (x - R - 1 >= 0) acc -= s[x - R - 1];
					if (x >= 0 && x < TW) d[x] = acc;
				}
			}
			for (int x = 0; x < TW; x++) {                         // vertical
				float acc = 0;
				for (int y = -R; y < TH + R; y++) {
					if (y + R < TH) acc += tmp[size_t(y + R) * TW + x];
					if (y - R - 1 >= 0) acc -= tmp[size_t(y - R - 1) * TW + x];
					if (y >= 0 && y < TH) blur[size_t(y) * TW + x] = acc;
				}
			}
			const float g = 0.55f / float((2 * R + 1) * (2 * R + 1));
			for (size_t i = 0; i < blur.size(); i++) lum[k][i] += blur[i] * g;
		}
	}
};

// a short 880 Hz beep for BEL
struct bell_sound {
	SDL_AudioStream *stream = nullptr;
	std::vector<int16_t> wave;
	void open()
	{
		SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, 22050 };
		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
		if (!stream) return;
		for (int i = 0; i < 22050 / 8; i++) wave.push_back(int16_t(((i * 880 * 2 / 22050) & 1) ? 3000 : -3000));
		SDL_ResumeAudioStreamDevice(stream);
	}
	void ring() { if (stream && SDL_GetAudioStreamQueued(stream) < 4000) SDL_PutAudioStreamData(stream, wave.data(), int(wave.size() * 2)); }
};

// a front-panel window drawn from a .lay view
struct panel {
	std::string tag;
	SDL_Window *win = nullptr; SDL_Renderer *ren = nullptr;
	lay::layout l; const lay::view *v = nullptr;
	const lay::item *pressed = nullptr;
	bool shown = true;
};

std::string base_name(const std::string &p)
{
	size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? p : p.substr(s + 1);
}

std::string screen_text(const ct8500 &t)
{
	std::string s;
	for (int y = 0; y < ct8500::VIEW; y++) {
		std::string line;
		for (int x = 0; x < ct8500::COLS; x++) {
			uint8_t c = t.cell(y, x);
			line += (c >= 0x20 && c < 0x7f && !(t.attrs(y, x) & ct8500::A_BLANK)) ? char(c) : ' ';
		}
		while (!line.empty() && line.back() == ' ') line.pop_back();
		s += line + '\n';
	}
	return s;
}

}

int main(int argc, char **argv)
{
	// front-end options first; the rest are the system's
	std::string style = "crt", artwork = "artwork"; double scale = 0; bool panels_on = true;   // scale 0: fit the screen
	struct shot_t { double at; std::string file; };
	std::vector<shot_t> shots;
	std::vector<char *> rest = { argv[0] };
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "-style" && i + 1 < argc) style = argv[++i];
		else if (a == "-scale" && i + 1 < argc) scale = atof(argv[++i]);
		else if (a == "-panels" && i + 1 < argc) panels_on = std::string(argv[++i]) != "off";
		else if (a == "-artwork" && i + 1 < argc) artwork = argv[++i];
		else if (a == "-shot" && i + 2 < argc) { double t = atof(argv[++i]); shots.push_back({ t, argv[++i] }); }
		else rest.push_back(argv[i]);
	}
	tek8550_options opt;
	std::string bad;
	if (!opt.parse(int(rest.size()), rest.data(), bad)) {
		fprintf(stderr, "usage: %s %s [-style crt|sharp] [-scale F] [-panels on|off] [-artwork DIR] [-shot SECONDS FILE.bmp]\n", argv[0], tek8550_options::usage()); return 1;
	}

	tek8550_system sys(opt);
	std::string err;
	if (!sys.start(true, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	ct8500 &ct = sys.terminal();

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
	SDL_Window *win = nullptr; SDL_Renderer *ren = nullptr;
	if (!SDL_CreateWindowAndRenderer("Tektronix CT8500 \xe2\x80\x94 8550 MDL", int(LW * (scale > 0 ? scale : 1.25)), int(LH * (scale > 0 ? scale : 1.25)),
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
	SDL_SetRenderLogicalPresentation(ren, LW, LH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(ren, 1);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, TW, TH);
	terminal_view view; view.crt = style != "sharp";
	SDL_SetTextureScaleMode(tex, view.crt ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
	SDL_StartTextInput(win);
	bell_sound bell; bell.open();

	// front panels, to the right of the terminal
	std::vector<panel> panels;
	std::string disc[4];
	for (int d = 0; d < 4; d++) disc[d] = opt.disks[d].empty() ? "" : base_name(opt.disks[d]);
	if (panels_on) {
		int tx = 0, ty = 0, tw = 0, th = 0;
		SDL_GetWindowPosition(win, &tx, &ty); SDL_GetWindowSize(win, &tw, &th);
		int py = ty;
		for (const char *tag : { "8301", "8501" }) {
			panel p; p.tag = tag;
			std::string e;
			if (!p.l.load(artwork + "/" + tag + ".lay", e) || p.l.views.empty()) { fprintf(stderr, "[panel %s: %s]\n", tag, e.c_str()); continue; }
			panels.push_back(std::move(p));
		}
		for (panel &p : panels) {
			p.v = &p.l.views[0];
			const float ps = scale > 0 ? float(scale) * 0.6f : 0.75f;
			if (!SDL_CreateWindowAndRenderer(p.v->name.c_str(), int(p.v->b.w * ps), int(p.v->b.h * ps), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &p.win, &p.ren)) continue;
			SDL_SetRenderLogicalPresentation(p.ren, int(p.v->b.w), int(p.v->b.h), SDL_LOGICAL_PRESENTATION_LETTERBOX);
			SDL_SetWindowPosition(p.win, tx + tw + 12, py);
			py += int(p.v->b.h * ps) + 40;
		}
		SDL_RaiseWindow(win);
	}
	// fit layout: one scale for the terminal and the stacked panels (panels at 0.62 of the
	// terminal's scale) so that side by side they fill the usable width of the screen
	auto fit_layout = [&]() {
		SDL_Rect u;
		if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &u)) return;
		const float gap = 8, bar = 30, r = 0.62f;                 // bar: title bar allowance
		float pw = 0, ph = 0; int n = 0;
		for (panel &p : panels) if (p.win && p.shown) { pw = std::max(pw, p.v->b.w); ph += p.v->b.h; n++; }
		float t = (float(u.w) - (n ? 3 : 2) * gap) / (LW + r * pw);
		t = std::min(t, (float(u.h) - bar - 2 * gap) / LH);
		if (n) t = std::min(t, (float(u.h) - n * bar - (n + 1) * gap) / (r * ph));
		float tw = LW * t, th = LH * t;
		float total = tw + (n ? gap + r * pw * t : 0);              // the whole set, centred
		float x0 = float(u.x) + (float(u.w) - total) / 2;
		SDL_SetWindowSize(win, int(tw), int(th));
		SDL_SetWindowPosition(win, int(x0), int(float(u.y) + bar + gap));
		float py = float(u.y) + bar + gap;
		for (panel &p : panels) {
			if (!p.win || !p.shown) continue;
			SDL_SetWindowSize(p.win, int(p.v->b.w * r * t), int(p.v->b.h * r * t));
			SDL_SetWindowPosition(p.win, int(x0 + tw + gap), int(py));
			py += p.v->b.h * r * t + bar + gap;
		}
		SDL_RaiseWindow(win);
	};
	if (scale <= 0) fit_layout();

	// lamps: the fraction of the frame each output was on, with a short afterglow
	std::vector<std::string> lamp_names;
	for (panel &p : panels) for (auto &it : p.v->items) if (!it.output.empty()) lamp_names.push_back(it.output);
	std::vector<double> lamp_acc(lamp_names.size()), lamp_level(lamp_names.size());
	std::map<std::string, size_t> lamp_index;
	for (size_t i = 0; i < lamp_names.size(); i++) lamp_index[lamp_names[i]] = i;
	long frame_ticks = 0;
	auto panel_of = [&](SDL_WindowID id) -> panel * { for (panel &p : panels) if (p.win && SDL_GetWindowID(p.win) == id) return &p; return nullptr; };

	std::deque<uint8_t> paste;                   // pasted text, typed at the line rate
	double next_paste = 0;
	const double char_time = 10.0 / 9600;        // the terminal port's 9600 baud
	bool turbo = opt.max_cycles >= 0, fullscreen = false, quit = false, redraw = true, blink_on = true;
	Uint64 wall0 = SDL_GetTicksNS(); double emu0 = sys.now();
	auto resync = [&]() { wall0 = SDL_GetTicksNS(); emu0 = sys.now(); };
	auto update_title = [&]() {
		SDL_SetWindowTitle(win, turbo ? "Tektronix CT8500 \xe2\x80\x94 8550 MDL  [turbo]" : "Tektronix CT8500 \xe2\x80\x94 8550 MDL");
	};

	while (!quit && !sys.finished()) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_EVENT_QUIT: quit = true; break;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				if (panel *p = panel_of(e.window.windowID)) { SDL_HideWindow(p->win); p->shown = false; }
				else quit = true;
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
				panel *p = panel_of(e.button.windowID);
				if (!p) break;
				SDL_ConvertEventToRenderCoordinates(p->ren, &e);
				if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) { p->pressed = nullptr; break; }
				const lay::item *it = lay::hit(*p->v, e.button.x, e.button.y, [](const lay::item &i) { return !i.input.empty(); });
				if (!it) break;
				p->pressed = it;
				if (it->input.compare(0, 5, "drive") == 0 && it->input.find(":eject") != std::string::npos) {
					int d = it->input[5] - '0'; std::string er;
					if (!disc[d].empty() && !sys.eject(d, er)) fprintf(stderr, "[drive %d: %s]\n", d, er.c_str());
					disc[d].clear();
				} else sys.input(it->input, 1);
				break;
			}
			case SDL_EVENT_DROP_FILE: {
				panel *p = panel_of(e.drop.windowID);
				if (!p || !e.drop.data) break;
				std::string path = e.drop.data;
				SDL_ConvertEventToRenderCoordinates(p->ren, &e);
				const lay::item *it = lay::hit(*p->v, e.drop.x, e.drop.y, [](const lay::item &i) { return i.drive >= 0; });
				if (!it) break;
				std::string er;
				if (sys.mount(it->drive, path, er)) disc[it->drive] = base_name(path);
				else fprintf(stderr, "[drive %d: %s]\n", it->drive, er.c_str());
				break;
			}
			case SDL_EVENT_TEXT_INPUT:
				for (const char *p = e.text.text; *p; p++)
					if (uint8_t(*p) < 0x80) ct.type(uint8_t(*p));
				break;
			case SDL_EVENT_KEY_DOWN: {
				SDL_Keycode k = e.key.key; SDL_Keymod m = e.key.mod;
				bool hot = (m & SDL_KMOD_GUI) || ((m & SDL_KMOD_CTRL) && (m & SDL_KMOD_ALT));
				if (hot) {
					if (k == SDLK_Q) quit = true;
					else if (k == SDLK_V) {
						if (char *t = SDL_GetClipboardText()) {
							for (const char *p = t; *p; p++) {
								uint8_t c = uint8_t(*p);
								if (c == '\r') continue;
								if (c == '\n') c = '\r';
								if (c < 0x80) paste.push_back(c);
							}
							SDL_free(t);
						}
					}
					else if (k == SDLK_C) SDL_SetClipboardText(screen_text(ct).c_str());
					else if (k == SDLK_T) { turbo = !turbo; resync(); update_title(); }
					else if (k == SDLK_F) { fullscreen = !fullscreen; SDL_SetWindowFullscreen(win, fullscreen); }
					else if (k == SDLK_S) { view.crt = !view.crt; SDL_SetTextureScaleMode(tex, view.crt ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST); redraw = true; }
					else if (k == SDLK_R) sys.input("restart");
					else if (k == SDLK_P) for (panel &p : panels) { p.shown = true; SDL_ShowWindow(p.win); }
					else if (k == SDLK_L) { if (fullscreen) { fullscreen = false; SDL_SetWindowFullscreen(win, false); } fit_layout(); }
					break;
				}
				// keys the CT8500 sends as control codes or escape sequences
				switch (k) {
				case SDLK_RETURN: case SDLK_KP_ENTER: ct.type('\r'); break;
				case SDLK_BACKSPACE: ct.type(0x08); break;
				case SDLK_TAB: if (m & SDL_KMOD_SHIFT) ct.key(ct8500::K_BACK_TAB); else ct.type('\t'); break;
				case SDLK_ESCAPE: ct.type(0x1b); break;
				case SDLK_UP: ct.key(ct8500::K_UP); break;
				case SDLK_DOWN: ct.key(ct8500::K_DOWN); break;
				case SDLK_LEFT: ct.key(ct8500::K_LEFT); break;
				case SDLK_RIGHT: ct.key(ct8500::K_RIGHT); break;
				case SDLK_HOME: ct.key(ct8500::K_HOME); break;
				case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4:
				case SDLK_F5: case SDLK_F6: case SDLK_F7: case SDLK_F8:
					ct.key(ct8500::key_t(ct8500::K_F1 + (k - SDLK_F1))); break;
				case SDLK_PAGEDOWN: ct.key(ct8500::K_F5); break;
				case SDLK_PAGEUP: ct.key(ct8500::K_F6); break;
				case SDLK_INSERT: ct.key(ct8500::K_INS_CHAR); break;
				case SDLK_DELETE: ct.key(ct8500::K_DEL_CHAR); break;
				case SDLK_F9: ct.key(ct8500::K_INS_LINE); break;
				case SDLK_F10: ct.key(ct8500::K_DEL_LINE); break;
				case SDLK_F11: ct.key(ct8500::K_ERASE_EOL); break;
				case SDLK_F12: ct.key(ct8500::K_ERASE_EOP); break;
				default:
					// Ctrl+letter and Ctrl+[\]^_ (no text event comes with these)
					if ((m & SDL_KMOD_CTRL) && !(m & SDL_KMOD_ALT)) {
						if (k >= SDLK_A && k <= SDLK_Z) ct.type(uint8_t(k - SDLK_A + 1));
						else if (k == SDLK_LEFTBRACKET) ct.type(0x1b);
						else if (k == SDLK_BACKSLASH) ct.type(0x1c);
						else if (k == SDLK_RIGHTBRACKET) ct.type(0x1d);
					}
					break;
				}
				break;
			}
			case SDL_EVENT_WINDOW_EXPOSED: redraw = true; break;
			default: break;
			}
		}

		// run the machine: to real time, or flat out for ~14 ms per frame in turbo
		Uint64 frame0 = SDL_GetTicksNS();
		double target = emu0 + double(frame0 - wall0) / 1e9;
		while (!sys.finished()) {
			Uint64 now = SDL_GetTicksNS();
			if (turbo ? now - frame0 > 14'000'000 : (sys.now() >= target || now - frame0 > 30'000'000)) break;
			if (!paste.empty() && sys.now() >= next_paste) {
				uint8_t c = paste.front(); paste.pop_front();
				ct.type(c);
				next_paste = sys.now() + char_time + (c == '\r' ? 0.25 : 0);   // let the line be taken
			}
			sys.tick();
			for (size_t i = 0; i < lamp_names.size(); i++) lamp_acc[i] += sys.out.get(lamp_names[i]);
			frame_ticks++;
		}
		if (frame_ticks) {
			for (size_t i = 0; i < lamp_names.size(); i++) {
				double duty = lamp_acc[i] / double(frame_ticks);
				lamp_level[i] = std::max(duty, lamp_level[i] * 0.55);
				lamp_acc[i] = 0;
			}
			frame_ticks = 0;
		}
		if (turbo || sys.now() < target - 0.25) resync();      // never try to catch up a backlog
		if (ct.take_bell()) bell.ring();

		bool b = (SDL_GetTicksNS() / 500'000'000) % 2 == 0;    // blinking attribute, 1 Hz
		if (b != blink_on) { blink_on = b; redraw = true; }
		if (sys.terminal_dirty) { sys.terminal_dirty = false; redraw = true; }
		if (redraw) {
			ct8500_raster::draw(ct, blink_on, view.dots.data());
			view.compose();
			SDL_UpdateTexture(tex, nullptr, view.px.data(), TW * 4);
			redraw = false;
		}
		SDL_SetRenderDrawColor(ren, Uint8(BG.r), Uint8(BG.g), Uint8(BG.b), 255);
		SDL_RenderClear(ren);
		SDL_FRect dst = { float(OVER_X), float(OVER_Y), float(TW), float(TH) };
		SDL_RenderTexture(ren, tex, nullptr, &dst);
		auto level = [&](const lay::item &it) -> float {
			if (!it.output.empty()) return float(lamp_level[lamp_index[it.output]]);
			for (panel &p : panels) if (p.pressed == &it) return 1.f;
			return 0.f;
		};
		auto caption = [&](const lay::item &it) -> std::string { return it.drive >= 0 ? (disc[it.drive].empty() ? "(empty)" : disc[it.drive]) : ""; };
		for (panel &p : panels) {
			if (!p.shown || !p.ren) continue;
			SDL_SetRenderDrawColor(p.ren, 30, 30, 32, 255);
			SDL_RenderClear(p.ren);
			lay::draw(p.ren, p.l, *p.v, level, caption);
		}
		for (auto &sh : shots)
			if (!sh.file.empty() && sys.now() >= sh.at) {
				if (SDL_Surface *sf = SDL_RenderReadPixels(ren, nullptr)) { SDL_SaveBMP(sf, sh.file.c_str()); SDL_DestroySurface(sf); }
				for (panel &p : panels) {                      // FILE.bmp -> FILE_8301.bmp, FILE_8501.bmp
					std::string f = sh.file; size_t dot = f.rfind('.');
					f = (dot == std::string::npos ? f : f.substr(0, dot)) + "_" + p.tag + ".bmp";
					if (SDL_Surface *sf = SDL_RenderReadPixels(p.ren, nullptr)) { SDL_SaveBMP(sf, f.c_str()); SDL_DestroySurface(sf); }
				}
				sh.file.clear();
			}
		for (panel &p : panels) if (p.shown && p.ren) SDL_RenderPresent(p.ren);
		SDL_RenderPresent(ren);                                // paced by vsync...
		if (!turbo && SDL_GetTicksNS() - frame0 < 4'000'000) SDL_Delay(4);   // ...unless the window is hidden
	}

	for (panel &p : panels) { if (p.ren) SDL_DestroyRenderer(p.ren); if (p.win) SDL_DestroyWindow(p.win); }
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	sys.shutdown(false);
	return 0;
}
