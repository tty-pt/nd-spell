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
#include <nd/spell-bcp.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int
on_spell_modal_close(bud_event *ev)
{
	(void)ev;
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

static inline int
on_spell_row_click(bud_event *ev)
{
	if (ev)
		bud_event_stop_propagation(ev);
	if (!ev || !ev->target)
		return 0;
	const char *cmd = bud_get_attr(ev->target, "data-cmd");
	if (cmd && cmd[0]) {
		nd_ui_send_cmd(cmd);
		return 1;
	}
	return 0;
}

static inline bud_node *
nd_spell_render_modal(const void *state)
{
	const char *active = nd_ui_get_active_modal();
	int is_open = (active && strcmp(active, "spell") == 0);

	bud_node *overlay = bud_element("div");
	bud_set_attr(overlay, "id", "spell-modal");
	bud_set_attr(overlay, "class", is_open
		? "modal fixed inset-0 z-50 flex items-center justify-center bg-black/60"
		: "hidden");
	bud_bind(overlay, "click", 1, on_spell_modal_close);

	bud_node *card = bud_element("div");
	bud_set_attr(card, "id", "spell");
	bud_set_attr(card, "class", "flex flex-col gap-2 p-3 border border-[#9589c5] bg-[#2c2c2c]/90 shadow-2xl min-w-[280px] max-w-sm select-none");
	bud_bind(card, "click", 1, on_spell_card_click);

	bud_node *title = bud_element("div");
	bud_set_attr(title, "class", "text-xs font-bold text-neutral-200 border-b border-[#9589c5]/60 pb-1 flex items-center gap-1.5");
	bud_append(title, bud_text("📜 Grimoire"));
	bud_append(card, title);

	bud_node *list = bud_element("div");
	bud_set_attr(list, "class", "flex flex-col gap-1.5 max-h-72 overflow-y-auto text-xs text-neutral-300 pr-1");

	const nd_spellbook_state_t *book = (const nd_spellbook_state_t *)
		nd_client_ext_get((const nd_client_state_t *)state, ND_SPELL_EXT_KEY);
	size_t count = (book && book->count > 0) ? book->count : 0;
	const nd_bar_state_t *mpg = nd_client_gauge_get(
			(const nd_client_state_t *)state, ND_SPELL_MP_GAUGE);
	unsigned mp = mpg ? mpg->val : 0;
	static const struct { unsigned bit; const char *icon, *name; } k_elem[] = {
		{ 1, "✨", "spirit" }, { 2, "🔥", "fire" }, { 4, "💧", "water" },
		{ 8, "🌬", "air" }, { 16, "⛰", "earth" }, { 32, "⚔", "physical" },
	};

	/* The DOM shape is fixed (one empty-state line + ND_MAX_SPELLS rows) and
	 * only attributes/text change with the spellbook. The bud patch stream
	 * cannot append children to an existing element, so a list that grew or
	 * shrank would not hydrate correctly. Unused rows are `hidden`. */
	size_t shown = 0;
	for (size_t i = 0; i < count && i < ND_MAX_SPELLS; i++)
		if (book->slots[i].name[0] || book->slots[i].spell_skel)
			shown++;

	bud_node *empty = bud_element("div");
	bud_set_attr(empty, "id", "spell-empty");
	bud_set_attr(empty, "class", shown ? "hidden" : "text-neutral-400 italic py-2");
	bud_append(empty, bud_text("Your grimoire is empty."));
	bud_append(list, empty);

	for (size_t i = 0; i < ND_MAX_SPELLS; i++) {
		const nd_spell_slot_desc_t *sp = book ? &book->slots[i] : NULL;
		int used = book && i < count && (sp->name[0] || sp->spell_skel);
		int can_cast = used && mp >= sp->cost;

		bud_node *row = bud_element("div");
		bud_set_attr(row, "class", !used ? "hidden"
			: can_cast
			? "flex justify-between items-center py-1.5 px-2 border-b border-neutral-700/40 hover:bg-neutral-800/80 rounded cursor-pointer transition-colors group"
			: "flex justify-between items-center py-1.5 px-2 border-b border-neutral-700/40 rounded opacity-50 cursor-not-allowed");

		char cmd_buf[32] = "", slot_buf[16] = "";
		if (used) {
			snprintf(slot_buf, sizeof(slot_buf), "%u", (unsigned)sp->id);
			if (can_cast)
				snprintf(cmd_buf, sizeof(cmd_buf), "c%u", (unsigned)sp->id);
		}
		bud_set_attr(row, "data-spell-slot", slot_buf);
		bud_set_attr(row, "data-cmd", cmd_buf);               /* "" = not castable */
		bud_set_attr(row, "data-disabled", used && !can_cast ? "1" : "");
		bud_bind(row, "click", 1, on_spell_row_click);

		const char *eicon = "", *ename = "";
		if (used)
			for (size_t e = 0; e < sizeof(k_elem) / sizeof(k_elem[0]); e++)
				if (k_elem[e].bit == sp->element) { eicon = k_elem[e].icon; ename = k_elem[e].name; }

		bud_node *col = bud_element("div");
		bud_set_attr(col, "class", "flex flex-col pointer-events-none");
		bud_node *sname = bud_element("span");
		bud_set_attr(sname, "class", "font-bold text-neutral-100 group-hover:text-purple-300 transition-colors");
		bud_set_attr(sname, "title", ename);
		char label[64] = " ";
		if (used)
			snprintf(label, sizeof(label), "%s %s", eicon, sp->name[0] ? sp->name : "Spell");
		bud_append(sname, bud_text(label));
		bud_append(col, sname);

		bud_node *scost = bud_element("span");
		bud_set_attr(scost, "class", can_cast || !used
			? "font-mono cf4 font-bold pointer-events-none"
			: "font-mono cf1 font-bold pointer-events-none");
		char cost_buf[16] = " ";
		if (used)
			snprintf(cost_buf, sizeof(cost_buf), "%u MP", (unsigned)sp->cost);
		bud_append(scost, bud_text(cost_buf));

		bud_append(row, col);
		bud_append(row, scost);
		bud_append(list, row);
	}

	bud_append(card, list);
	bud_append(overlay, card);
	return overlay;
}

#ifdef __cplusplus
}
#endif

#endif /* !ND_SPELL_UI_H */
