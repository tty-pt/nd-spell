#ifndef SPELL_H
#define SPELL_H

/* REQUIRES: mortal, fight, equip */
/* SOFT REQUIRES: seat */

#define MP_G(v) HP_G(v)
#define MP_MAX(fighter) ((unsigned short) MP_G((fighter)->attr[ATTR_WIZ]))

/* DATA */

typedef struct {
	unsigned element;
	unsigned char ms, ra, y, flags;
} spell_skeleton_t;

struct debuf {
	int skel;
	unsigned duration;
	short val;
};

struct spell {
	unsigned skel;
	unsigned cost; 
	unsigned short val;
};

typedef struct {
	struct debuf debufs[8];
	struct spell spells[8];
	unsigned short mp;
	unsigned char debuf_mask, combo;
} caster_t;

#endif
