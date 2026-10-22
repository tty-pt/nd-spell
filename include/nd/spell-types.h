/* spell-types.h — nd-spell's cross-module types and enums */

#ifndef ND_SPELL_TYPES_H
#define ND_SPELL_TYPES_H

#include <stdint.h>

enum legacy_spell_type {
	SPELL_HEAL,
	SPELL_FOCUS,
	SPELL_FIRE_FOCUS,
	SPELL_CUT,
	SPELL_FIREBALL,
	SPELL_WEAKEN,
	SPELL_DISTRACT,
	SPELL_FREEZE,
	SPELL_LAVA_SHIELD,
	SPELL_WIND_VEIL,
	SPELL_STONE_SKIN,
	SPELL_MAX,
};

struct debuf {
	unsigned skel;
	unsigned duration;
	long val;
};

struct spell {
	unsigned skel;
	unsigned cost;
	unsigned val;
};

typedef struct {
	struct debuf debufs[8];
	struct spell spells[8];
	long mp;
	unsigned char debuf_mask, combo;
	unsigned char mov_mask, mdmg_mask, mdef_mask;
} caster_t;

typedef struct {
	const char *id;
	const char *name;
	const char *icon;
	unsigned element;
	unsigned cost;
	const char *desc;
} spell_info_t;

#endif /* !ND_SPELL_TYPES_H */
