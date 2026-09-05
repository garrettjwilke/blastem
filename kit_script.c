// See kit_script.h. Frame-scripted input + screenshots for a host-side test harness.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kit_script.h"
#include "kit_prof.h"
#include "blastem.h"
#include "system.h"
#include "io.h"

#define KIT_SCRIPT_TEXT_LEN 160

enum {
	KS_DOWN,
	KS_UP,
	KS_SHOT,
	KS_BURST,
	KS_LOG
};

typedef struct {
	uint32_t frame;
	uint32_t count;
	uint16_t buttons;
	uint8_t  kind;
	uint8_t  port;
	char     text[KIT_SCRIPT_TEXT_LEN];
} kit_action;

static kit_action *actions;
static uint32_t num_actions;
static uint32_t next_action;
static uint32_t anchor_split;   // first action whose frame is relative to the anchor
static uint32_t anchor_base;
static uint8_t  anchor_wait;
static uint8_t  script_ended;
static char     anchor_text[KIT_SCRIPT_TEXT_LEN];

static uint32_t script_frame_now;
static uint32_t script_last_boundary = 0xFFFFFFFFu;
static char    *script_path;

static void (*capture_shot)(char *path);
static void (*capture_burst)(char *prefix, uint32_t count);

uint8_t kit_logframes;

static const char *button_names[NUM_GAMEPAD_BUTTONS] = {
	[DPAD_UP] = "up", [DPAD_DOWN] = "down", [DPAD_LEFT] = "left", [DPAD_RIGHT] = "right",
	[BUTTON_A] = "a", [BUTTON_B] = "b", [BUTTON_C] = "c", [BUTTON_START] = "start",
	[BUTTON_X] = "x", [BUTTON_Y] = "y", [BUTTON_Z] = "z", [BUTTON_MODE] = "mode"
};

uint8_t kit_parse_button(const char *name)
{
	for (uint8_t button = DPAD_UP; button < NUM_GAMEPAD_BUTTONS; button++)
	{
		if (button_names[button] && !strcmp(button_names[button], name)) {
			return button;
		}
	}
	return BUTTON_INVALID;
}

void kit_script_set_capture(void (*shot)(char *path), void (*burst)(char *prefix, uint32_t count))
{
	capture_shot = shot;
	capture_burst = burst;
}

void kit_script_set_logframes(uint8_t on)
{
	kit_logframes = on ? 1 : 0;
}

void kit_script_report_frame(void)
{
	char line[64];
	snprintf(line, sizeof(line), "KIT FRAME frame=%u", script_frame_now);
	kit_emit_line(line);
}

static void script_free(void)
{
	free(actions);
	actions = NULL;
	free(script_path);
	script_path = NULL;
	num_actions = 0;
	next_action = 0;
	anchor_split = 0;
	anchor_base = 0;
	anchor_wait = 0;
	script_ended = 0;
	anchor_text[0] = 0;
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t' || *s == '\r') {
		s++;
	}
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
		end--;
	}
	*end = 0;
	return s;
}

static void script_error(const char *path, uint32_t lineno, const char *msg)
{
	char line[512];
	snprintf(line, sizeof(line), "KIT SCRIPT error file=%s line=%u %s", path, lineno, msg);
	kit_emit_line(line);
	script_free();
}

// Parse "a" / "up,a" into a mask over the gamepad button enum. Returns 0 on an unknown name.
static uint8_t parse_buttons(const char *spec, uint16_t *mask)
{
	char buf[64];
	if (strlen(spec) >= sizeof(buf)) {
		return 0;
	}
	strcpy(buf, spec);
	*mask = 0;
	for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
		uint8_t button = kit_parse_button(tok);
		if (button == BUTTON_INVALID) {
			return 0;
		}
		*mask |= 1u << button;
	}
	return *mask != 0;
}

static kit_action *push_action(uint32_t *cap)
{
	if (num_actions == *cap) {
		*cap = *cap ? *cap * 2 : 64;
		actions = realloc(actions, *cap * sizeof(kit_action));
	}
	kit_action *a = &actions[num_actions++];
	memset(a, 0, sizeof(*a));
	return a;
}

uint8_t kit_script_load(const char *path)
{
	script_free();
	if (!path || !*path) {
		return 0;
	}
	FILE *f = fopen(path, "r");
	if (!f) {
		char line[512];
		snprintf(line, sizeof(line), "KIT SCRIPT error file=%s cannot open", path);
		kit_emit_line(line);
		return 1;
	}
	script_path = strdup(path);

	char raw[512];
	uint32_t lineno = 0, cap = 0, prev_frame = 0, section_start = 0;
	uint8_t have_anchor = 0;
	while (fgets(raw, sizeof(raw), f))
	{
		lineno++;
		char *line = trim(raw);
		if (!*line || *line == '#') {
			continue;
		}
		if (!strncmp(line, "anchor", 6) && (line[6] == ' ' || line[6] == '\t')) {
			char *text = trim(line + 6);
			if (have_anchor || !*text || strlen(text) >= KIT_SCRIPT_TEXT_LEN) {
				fclose(f);
				script_error(path, lineno, "expected one 'anchor <text>' line");
				return 1;
			}
			have_anchor = 1;
			anchor_split = num_actions;
			section_start = num_actions;
			prev_frame = 0;
			snprintf(anchor_text, sizeof(anchor_text), "%s", text);
			continue;
		}
		char *frame_tok = strtok(line, " \t");
		char *verb = strtok(NULL, " \t");
		if (!frame_tok || !verb) {
			fclose(f);
			script_error(path, lineno, "expected '<frame> <action> ...'");
			return 1;
		}
		char *endp;
		unsigned long frame = strtoul(frame_tok, &endp, 10);
		if (*endp || endp == frame_tok) {
			fclose(f);
			script_error(path, lineno, "frame is not a decimal number");
			return 1;
		}
		if (num_actions > section_start && frame < prev_frame) {
			fclose(f);
			script_error(path, lineno, "frames must be non-decreasing");
			return 1;
		}
		prev_frame = (uint32_t)frame;

		if (!strcmp(verb, "down") || !strcmp(verb, "up")) {
			char *port = strtok(NULL, " \t");
			char *buttons = strtok(NULL, " \t");
			uint16_t mask;
			if (!port || !buttons) {
				fclose(f);
				script_error(path, lineno, "expected '<frame> down|up <port> <buttons>'");
				return 1;
			}
			if (!parse_buttons(buttons, &mask)) {
				fclose(f);
				script_error(path, lineno, "unknown button name");
				return 1;
			}
			kit_action *a = push_action(&cap);
			a->frame = (uint32_t)frame;
			a->kind = verb[0] == 'd' ? KS_DOWN : KS_UP;
			a->port = (uint8_t)atoi(port);
			a->buttons = mask;
			snprintf(a->text, sizeof(a->text), "%s", buttons);
		} else if (!strcmp(verb, "shot") || !strcmp(verb, "log")) {
			char *rest = strtok(NULL, "");
			rest = rest ? trim(rest) : NULL;
			if (!rest || !*rest || strlen(rest) >= KIT_SCRIPT_TEXT_LEN) {
				fclose(f);
				script_error(path, lineno, "expected '<frame> shot|log <text>'");
				return 1;
			}
			kit_action *a = push_action(&cap);
			a->frame = (uint32_t)frame;
			a->kind = verb[0] == 's' ? KS_SHOT : KS_LOG;
			snprintf(a->text, sizeof(a->text), "%s", rest);
		} else if (!strcmp(verb, "burst")) {
			char *count = strtok(NULL, " \t");
			char *prefix = strtok(NULL, "");
			prefix = prefix ? trim(prefix) : NULL;
			if (!count || !prefix || !*prefix || atoi(count) <= 0 || strlen(prefix) >= KIT_SCRIPT_TEXT_LEN) {
				fclose(f);
				script_error(path, lineno, "expected '<frame> burst <count> <prefix>'");
				return 1;
			}
			kit_action *a = push_action(&cap);
			a->frame = (uint32_t)frame;
			a->kind = KS_BURST;
			a->count = (uint32_t)atoi(count);
			snprintf(a->text, sizeof(a->text), "%s", prefix);
		} else {
			fclose(f);
			script_error(path, lineno, "unknown action");
			return 1;
		}
	}
	fclose(f);

	if (!num_actions) {
		script_error(path, lineno, "no actions");
		return 1;
	}
	anchor_wait = have_anchor;
	if (!have_anchor) {
		anchor_split = num_actions;
	}

	char line[512];
	snprintf(line, sizeof(line), "KIT SCRIPT loaded file=%s actions=%u absolute=%u anchor=%s now=%u",
		path, num_actions, anchor_split, have_anchor ? anchor_text : "-", script_frame_now);
	kit_emit_line(line);
	if (anchor_split && actions[0].frame <= script_frame_now) {
		snprintf(line, sizeof(line), "KIT SCRIPT warn first=%u already passed (now=%u) -- not reproducible",
			actions[0].frame, script_frame_now);
		kit_emit_line(line);
	}
	return 0;
}

void kit_script_kdebug(const char *msg, uint32_t frame)
{
	if (!anchor_wait || !strstr(msg, anchor_text)) {
		return;
	}
	anchor_wait = 0;
	anchor_base = frame;
	char line[512];
	snprintf(line, sizeof(line), "KIT SCRIPT anchor frame=%u text=%s dropped=%u",
		frame, anchor_text, anchor_split - next_action);
	kit_emit_line(line);
	if (next_action < anchor_split) {
		next_action = anchor_split;
	}
}

static void apply_pad(kit_action *a)
{
	if (!current_system) {
		return;
	}
	for (uint8_t button = DPAD_UP; button < NUM_GAMEPAD_BUTTONS; button++)
	{
		if (!(a->buttons & (1u << button))) {
			continue;
		}
		if (a->kind == KS_DOWN) {
			if (current_system->gamepad_down) {
				current_system->gamepad_down(current_system, a->port, button);
			}
		} else if (current_system->gamepad_up) {
			current_system->gamepad_up(current_system, a->port, button);
		}
	}
}

static void apply_action(kit_action *a, uint32_t target)
{
	char line[512];
	switch (a->kind)
	{
	case KS_DOWN:
	case KS_UP:
		apply_pad(a);
		snprintf(line, sizeof(line), "KIT SCRIPT frame=%u at=%u %s %u %s",
			script_frame_now, target, a->kind == KS_DOWN ? "down" : "up", a->port, a->text);
		break;
	case KS_SHOT:
		if (capture_shot) {
			capture_shot(strdup(a->text));
		}
		snprintf(line, sizeof(line), "KIT SCRIPT frame=%u at=%u shot %s",
			script_frame_now, target, a->text);
		break;
	case KS_BURST:
		if (capture_burst) {
			capture_burst(strdup(a->text), a->count);
		}
		snprintf(line, sizeof(line), "KIT SCRIPT frame=%u at=%u burst %u %s",
			script_frame_now, target, a->count, a->text);
		break;
	default:
		snprintf(line, sizeof(line), "KIT SCRIPT frame=%u at=%u log %s",
			script_frame_now, target, a->text);
		break;
	}
	kit_emit_line(line);
}

void kit_script_frame(uint32_t finished_frame)
{
	if (finished_frame == script_last_boundary) {
		return;
	}
	script_last_boundary = finished_frame;
	script_frame_now = finished_frame + 1;
	if (script_ended || !num_actions) {
		return;
	}
	while (next_action < num_actions)
	{
		kit_action *a = &actions[next_action];
		uint32_t target;
		if (next_action < anchor_split) {
			target = a->frame;
		} else if (anchor_wait) {
			break;
		} else {
			target = anchor_base + a->frame;
		}
		if (target > script_frame_now) {
			break;
		}
		apply_action(a, target);
		next_action++;
	}
	if (next_action >= num_actions) {
		script_ended = 1;
		char line[512];
		snprintf(line, sizeof(line), "KIT SCRIPT end frame=%u actions=%u file=%s",
			script_frame_now, num_actions, script_path ? script_path : "");
		kit_emit_line(line);
	}
}
