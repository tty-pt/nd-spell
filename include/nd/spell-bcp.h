#ifndef ND_SPELL_BCP_H
#define ND_SPELL_BCP_H

/*
 * spell-bcp.h — Wire protocol definitions and unpacker for nd-spell.
 * Modular channel ND_BCP_SPELL_LIST (opcode 16) and ND_BCP_SPELL_CAST (opcode 17).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/bcp-meta.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channel 13 is a (val,max) bar frame for mana, decoded into this gauge. */
#define ND_BCP_MP         13
#define ND_SPELL_MP_GAUGE "mp"
#define ND_BCP_SPELL_LIST 16
#define ND_BCP_SPELL_CAST 17
#define ND_MAX_SPELLS     8
#define ND_SPELL_EXT_KEY  "spell"

typedef struct {
	uint32_t id;
	uint32_t spell_skel;
	char     name[24];
	uint16_t cost;
	uint8_t  element;
	uint8_t  cooldown;
} nd_spell_slot_desc_t;

/* Most recent server cast acknowledgement (ND_BCP_SPELL_CAST). `serial`
 * increments per ack so a viewport can detect a new cast and fire an effect
 * exactly once. */
typedef struct {
	uint32_t serial;
	uint8_t  slot;
	uint8_t  element;
	uint32_t target_ref;
} nd_spell_cast_ack_t;

typedef struct {
	nd_spell_slot_desc_t slots[ND_MAX_SPELLS];
	size_t count;
	nd_spell_cast_ack_t last_cast;
} nd_spellbook_state_t;

#pragma pack(push, 1)

typedef struct {
	uint8_t  slot_id;
	uint32_t spell_skel;
	uint16_t cost;
	uint8_t  element;
	uint8_t  cooldown;
	char     name[24];
} nd_bcp_spell_slot_wire_t;

typedef struct {
	uint8_t                  count; /* Number of spell slots (0..8) */
	nd_bcp_spell_slot_wire_t slots[ND_MAX_SPELLS];
} nd_bcp_spell_list_payload_t;

typedef struct {
	nd_bcp_hdr_t                hdr;   /* magic '#b', opcode 16 */
	nd_bcp_spell_list_payload_t data;
} nd_bcp_spell_list_wire_t;

typedef struct {
	nd_bcp_hdr_t hdr;       /* magic '#b', opcode 17 */
	uint8_t      slot_id;   /* Spell slot to cast */
	uint32_t     target_ref;/* Entity ref to target */
} nd_bcp_spell_cast_wire_t;

#pragma pack(pop)

static inline int
nd_spell_bcp_unpack(uint8_t opcode, const uint8_t *payload, size_t len, void *state)
{
	if (!payload || !state ||
	    (opcode != ND_BCP_SPELL_LIST && opcode != ND_BCP_SPELL_CAST))
		return -1;

	nd_spellbook_state_t *book = nd_client_ext((nd_client_state_t *)state,
			ND_SPELL_EXT_KEY, sizeof(nd_spellbook_state_t));
	if (!book)
		return -1;

	const uint8_t *p = payload;
	size_t rem = len;

	/* If full frame with '#b<iden>' prefix was passed, advance past header */
	if (rem >= 3 && p[0] == ND_BCP_MAGIC_0 && p[1] == ND_BCP_MAGIC_1) {
		p += 3;
		rem -= 3;
	}

	if (opcode == ND_BCP_SPELL_CAST) {
		/* u8 slot, u8 element, u32 target_ref */
		if (rem < 6)
			return -1;
		book->last_cast.slot = p[0];
		book->last_cast.element = p[1];
		memcpy(&book->last_cast.target_ref, p + 2, sizeof(uint32_t));
		book->last_cast.serial++;
		return 1;
	}

	if (rem < 1)
		return -1;

	uint8_t count = *p++;
	rem--;
	if (count > ND_MAX_SPELLS)
		count = ND_MAX_SPELLS;

	size_t expected = count * sizeof(nd_bcp_spell_slot_wire_t);
	if (rem < expected)
		return -1;

	book->count = count;
	const nd_bcp_spell_slot_wire_t *wire_slots = (const nd_bcp_spell_slot_wire_t *)p;
	for (size_t i = 0; i < count; i++) {
		const nd_bcp_spell_slot_wire_t *w = &wire_slots[i];
		uint8_t s_id = (w->slot_id < ND_MAX_SPELLS) ? w->slot_id : (uint8_t)i;
		book->slots[s_id].id = s_id;
		book->slots[s_id].spell_skel = w->spell_skel;
		book->slots[s_id].cost = w->cost;
		book->slots[s_id].element = w->element;
		book->slots[s_id].cooldown = w->cooldown;

		/* Bounded copy ensuring null-termination */
		size_t nlen = sizeof(book->slots[s_id].name);
		strncpy(book->slots[s_id].name, w->name, nlen - 1);
		book->slots[s_id].name[nlen - 1] = '\0';
	}
	return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* ND_SPELL_BCP_H */
