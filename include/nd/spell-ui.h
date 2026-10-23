/*
 * spell-ui.h — Extensible UI component contribution for nd-spell.
 * Provides the Grimoire / Spellbook modal and spell action handlers.
 */

#ifndef ND_SPELL_UI_H
#define ND_SPELL_UI_H

#include <string.h>
#include <bud/bud.h>
#include <bud/bud_app.h>
#include <nd/ui-slot-types.h>
#include <nd/bcp-types.h>
#include <nd/spell-types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int
on_spell_modal_close(bud_event *ev)
{
	if (ev && ev->target) {
		const char *id = bud_get_attr(ev->target, "id");
		if (id && strcmp(id, "spell-modal") != 0) {
			const char *cls = bud_get_attr(ev->target, "class");
			if (!cls || !strstr(cls, "hover:text-neutral-100"))
				return 0;
		}
	}
	nd_ui_set_active_modal("");
#ifdef __wasm__
	bud_app_set_state();
#endif
	return 0;
}

static inline int
on_spell_card_click(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	return 1;
}

static inline bud_node *
nd_spell_render_modal(const void *state)
{
	const nd_client_state_t *st = (const nd_client_state_t *)state;
	const char *active = nd_ui_get_active_modal();
	if (!st || !active || strcmp(active, "spell") != 0) {
		bud_node *empty = bud_element("div");
		bud_set_attr(empty, "id", "spell-modal");
		bud_set_attr(empty, "class", "hidden");
		return empty;
	}

	bud_node *overlay = bud_element("div");
	bud_set_attr(overlay, "id", "spell-modal");
	bud_set_attr(overlay, "class", "fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4");
	bud_bind(overlay, "click", 0, on_spell_modal_close);

	bud_node *card = bud_element("div");
	bud_set_attr(card, "class", "bg-neutral-900 border border-neutral-700 rounded-lg p-6 max-w-md w-full flex flex-col gap-4 shadow-xl");
	bud_bind(card, "click", 1, on_spell_card_click);

	bud_node *header = bud_element("div");
	bud_set_attr(header, "class", "flex justify-between items-center border-b border-neutral-800 pb-2");
	bud_node *title = bud_element("h2");
	bud_set_attr(title, "class", "text-lg font-bold text-neutral-200 flex items-center gap-2");
	bud_append(title, bud_text("📜 Grimoire / Spellbook"));
	bud_append(header, title);

	bud_node *close_btn = bud_element("button");
	bud_set_attr(close_btn, "type", "button");
	bud_set_attr(close_btn, "class", "text-neutral-400 hover:text-neutral-100 text-sm");
	bud_append(close_btn, bud_text("✕"));
	bud_bind(close_btn, "click", 0, on_spell_modal_close);
	bud_append(header, close_btn);

	bud_append(card, header);

	bud_node *list = bud_tpl(
		"<div class='flex flex-col gap-2 max-h-72 overflow-y-auto text-xs text-neutral-300 pr-1'>"
		"  <div class='flex justify-between items-center bg-neutral-800/50 p-2 rounded border border-neutral-700/50'>"
		"    <div class='flex flex-col'><span class='font-bold text-neutral-100'>Heal</span><span class='text-neutral-400'>Restores vitality</span></div>"
		"    <span class='font-mono text-blue-400 font-bold'>3 MP</span>"
		"  </div>"
		"  <div class='flex justify-between items-center bg-neutral-800/50 p-2 rounded border border-neutral-700/50'>"
		"    <div class='flex flex-col'><span class='font-bold text-neutral-100'>Focus</span><span class='text-neutral-400'>Channels inner magic</span></div>"
		"    <span class='font-mono text-blue-400 font-bold'>15 MP</span>"
		"  </div>"
		"  <div class='flex justify-between items-center bg-neutral-800/50 p-2 rounded border border-neutral-700/50'>"
		"    <div class='flex flex-col'><span class='font-bold text-neutral-100'>Fireball</span><span class='text-neutral-400'>Blasts target with fire</span></div>"
		"    <span class='font-mono text-blue-400 font-bold'>3 MP</span>"
		"  </div>"
		"  <div class='flex justify-between items-center bg-neutral-800/50 p-2 rounded border border-neutral-700/50'>"
		"    <div class='flex flex-col'><span class='font-bold text-neutral-100'>Freeze</span><span class='text-neutral-400'>Cripples target movement</span></div>"
		"    <span class='font-mono text-blue-400 font-bold'>10 MP</span>"
		"  </div>"
		"  <div class='flex justify-between items-center bg-neutral-800/50 p-2 rounded border border-neutral-700/50'>"
		"    <div class='flex flex-col'><span class='font-bold text-neutral-100'>Lava Shield</span><span class='text-neutral-400'>Surrounds in molten armor</span></div>"
		"    <span class='font-mono text-blue-400 font-bold'>15 MP</span>"
		"  </div>"
		"  <div class='flex justify-between items-center bg-neutral-800/50 p-2 rounded border border-neutral-700/50'>"
		"    <div class='flex flex-col'><span class='font-bold text-neutral-100'>Stone Skin</span><span class='text-neutral-400'>Hardens flesh to absorb hits</span></div>"
		"    <span class='font-mono text-blue-400 font-bold'>20 MP</span>"
		"  </div>"
		"</div>"
	);
	bud_append(card, list);
	bud_append(overlay, card);
	return overlay;
}

#ifdef __cplusplus
}
#endif

#endif /* !ND_SPELL_UI_H */
