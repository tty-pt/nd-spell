#include <nd/nd.h>

unsigned type_spell;

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

SKEL legacy_spell_map[] = {
	[SPELL_HEAL] = {
		.name = "Heal",
		.sp = {
			.spell = {
				.element = ELM_PHYSICAL,
				.ms = 3, .ra = 1, .y = 2,
				.flags = AF_HP,
			}
		},
	},
	[SPELL_FOCUS] = {
		.name = "Focus",
		.sp = {
			.spell = {
				.element = ELM_PHYSICAL,
				.ms = 15, .ra = 3, .y = 1,
				.flags = AF_MDMG | AF_BUF,
			}
		},
	},

	[SPELL_FIRE_FOCUS] = {
		.name = "Fire Focus",
		.sp = {
			.spell = {
				.element = ELM_FIRE,
				.ms = 15, .ra = 3, .y = 1,
				.flags = AF_MDMG | AF_BUF,
			}
		},
	},

	[SPELL_CUT] = {
		.name = "Cut",
		.sp = {
			.spell = {
				.element = ELM_PHYSICAL,
				.ms = 15, .ra = 1, .y = 2,
				.flags = AF_NEG,
			}
		},
	},

	[SPELL_FIREBALL] = {
		.name = "Fireball",
		.sp = {
			.spell = {
				.element = ELM_FIRE,
				.ms = 3, .ra = 1, .y = 2,
				.flags = AF_NEG,
			}
		},
	}, // 1/4 chance of burning

	[SPELL_WEAKEN] = {
		.name = "Weaken",
		.sp = { .spell = {
			.element = ELM_PHYSICAL,
			.ms = 15, .ra = 3, .y = 1,
			.flags = AF_MDMG | AF_BUF | AF_NEG,
		} },
	},

	[SPELL_DISTRACT] = {
		.name = "Distract",
		.sp = { .spell = {
			.element = ELM_PHYSICAL,
			.ms = 15, .ra = 3, .y = 1,
			.flags = AF_MDEF | AF_BUF | AF_NEG,
		} },
	},

	[SPELL_FREEZE] = {
		.name = "Freeze",
		.sp = { .spell = {
			.element = ELM_WATER,
			.ms = 10, .ra = 2, .y = 4,
			.flags = AF_MOV | AF_NEG,
		} },
	},

	[SPELL_LAVA_SHIELD] = {
		.name = "Lava Shield",
		.sp = { .spell = {
			.element = ELM_FIRE,
			.ms = 15, .ra = 3, .y = 1,
			.flags = AF_MDEF | AF_BUF,
		} },
	},

	[SPELL_WIND_VEIL] = {
		.name = "Wind Veil",
		.sp = { .spell = {
			.flags = AF_DODGE,
		} },
	},

	[SPELL_STONE_SKIN] = {
		.name = "Stone Skil",
		.sp = { .spell = {
			.flags = AF_DEF,
		} },
	},
};

void
mod_install(void *arg __attribute__((unused))) {
	type_spell = nd_put(HD_TYPE, NULL, "spell");
	for (int i = 0; i < SPELL_MAX; i++) {
		legacy_spell_map[i].type = type_spell;
		nd_put(HD_SKEL, NULL, &legacy_spell_map[i]);
	}
}
