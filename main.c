#include <nd/nd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "./include/uapi/spell.h"

#define HEAL_SKEL_REF 1
#define SPELL_COST(dmg, y, no_bdmg) (no_bdmg ? 0 : dmg) + dmg / (1 << y)
#define SPELL_DMG(p, sp) SPELL_G((p)->attr[ATTR_INT]) + HS(sp)

#define DEBUF_DURATION(ra) 20 * (RARE_MAX - ra) / RARE_MAX
#define DEBUF_DMG(sp_dmg, duration) ((long) 2 * sp_dmg) / duration
#define DEBUF_TYPE_MASK 0xf
#define DEBUF_TYPE(sp) (sp->flags & DEBUF_TYPE_MASK)

#define STAT_ELEMENT(ent, es, type) \
	mask_element(es, EFFECT(ent, type).mask)

struct es {
	struct debuf debufs[8];
	struct spell spells[8];
	unsigned short mp;
	unsigned char debuf_mask, combo;
};

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

unsigned bcp_mp, type_spell, es_hd;
unsigned omp;

unsigned spell_new(char *name, unsigned element,
		unsigned char ms, unsigned char ra,
		unsigned char y, unsigned char flags)
{
	SKEL skel = {
		.type = type_spell,
	};

	SSPE sspe = {
		.element = element,
		.ms = ms, .ra = ra, .y = y,
		.flags = flags,
	};

	memcpy((void *) &skel.name, name, sizeof(skel.name));
	memcpy(&skel.sp.raw, &sspe, sizeof(sspe));
	return nd_put(HD_SKEL, NULL, &skel);
}

static inline char*
debuf_wts(struct spell_skeleton *_sp)
{
	static char ret[BUFSIZ];
	register unsigned char mask = _sp->flags;
	register unsigned idx = (DEBUF_TYPE(_sp) << 1) + ((mask >> 4) & 1);
	unsigned wts_ref;
	extern unsigned awts_hd, wts_hd;
	unsigned ref = (_sp->element << 4) | idx;
	nd_get(HD_RWTS, &wts_ref, &ref);
	nd_get(HD_WTS, ret, &wts_ref);
	return ret;
}

static inline enum color
sp_color(SSPE *_sp)
{
	if (DEBUF_TYPE(_sp) != AF_HP || (_sp->flags & AF_NEG)) {
		element_t element;
		nd_get(HD_ELEMENT, &element, &_sp->element);
		return element.color;
	}

	return GREEN;
}

void
debuf_notify(unsigned player_ref, struct debuf *d, short val)
{
	char buf[BUFSIZ];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	SSPE *sspe = (SSPE *) &skel.sp.raw;
	char *wts = debuf_wts(sspe);

	if (val)
		snprintf(buf, sizeof(buf), " (%s%d%s)", ansi_fg[sp_color(sspe)], val, ANSI_RESET);
	else
		*buf = '\0';

	notify_wts(player_ref, wts, wts_plural(wts), "%s", buf);
}

static inline int
debuf_start(unsigned ent_ref, struct spell *sp, short val)
{
	SKEL skel;
	nd_get(HD_SKEL, &skel, &sp->skel);
	struct es es;
	SSPE *sspe = (SSPE *) &skel.sp.raw;
	nd_get(es_hd, &es, &ent_ref);
	ENT eplayer = ent_get(ent_ref);
	struct debuf *d;
	int i;

	if (es.debuf_mask) {
		i = __builtin_ffs(~es.debuf_mask);
		if (!i)
			return -1;
		i--;
	} else
		i = 0;

	d = &es.debufs[i];
	d->skel = sp->skel;
	d->duration = DEBUF_DURATION(sspe->ra);
	d->val = DEBUF_DMG(val, d->duration);

	i = 1 << i;
	es.debuf_mask |= i;

	struct effect *e = &eplayer.e[DEBUF_TYPE(sspe)];
	e->mask |= i;
	e->value += d->val;

	debuf_notify(ent_ref, d, 0);
	ent_set(ent_ref, &eplayer);
	nd_put(es_hd, &ent_ref, &es);

	return 0;
}

static inline unsigned
mask_element(struct es es, register unsigned char a)
{
	unsigned skel_id = es.debufs[__builtin_ffs(a) - 1].skel;
	SKEL skel;
	nd_get(HD_SKEL, &skel, &skel_id);

	if (!a)
		return ELM_PHYSICAL;

	return ((SSPE *) &skel.sp.raw)->element;
}

static inline int
spell_cast(unsigned ent_ref, ENT *eplayer, unsigned target_ref, unsigned slot)
{
	struct es es;
	struct spell sp;
	ENT etarget = ent_get(target_ref);
	SKEL skel;

	nd_get(es_hd, &es, &ent_ref);
	sp = es.spells[slot];

	nd_get(HD_SKEL, &skel, &sp.skel);
	SSPE *sspe = (SSPE *) &skel.sp.raw;

	unsigned mana = es.mp;
	char a[BUFSIZ]; // FIXME way too big?
	char c[BUFSIZ + 32];

	enum color color = sp_color(sspe);

	if (mana < sp.cost)
		return -1;

	snprintf(a, sizeof(a), "%s%s"ANSI_RESET, ansi_fg[color], skel.name);

	mana -= sp.cost;
	es.mp = mana > 0 ? mana : 0;
	nd_put(es_hd, &ent_ref, &es);

	struct es target_es;
	nd_get(es_hd, &es, &target_ref);

	short val = ent_dmg(sspe->element, sp.val,
			EFFECT(&etarget, MDEF).value,
			STAT_ELEMENT(&etarget, es, MDEF));

	if (sspe->flags & AF_NEG) {
		val = -val;
		if (dodge(ent_ref, a))
			return 0;
	} else
		target_ref = ent_ref;

	snprintf(c, sizeof(c), "cast %s on", a);

	notify_attack(ent_ref, target_ref, c, 0, color, val);

	if (random() < (RAND_MAX >> sspe->y))
		debuf_start(target_ref, &sp, val);

	return entity_damage(ent_ref, eplayer, target_ref, &etarget, val);
}

ENT sic_attack(unsigned ent_ref, ENT ent) {
	if (!ent.target)
		return ent;

	struct es es;
	register unsigned char mask = EFFECT(&ent, MOV).mask;
	nd_get(es_hd, &es, &ent_ref);

	if (mask) {
		register unsigned i = __builtin_ffs(mask) - 1;
		debuf_notify(ent_ref, &es.debufs[i], 0);
		return ent;
	}

	// second part
	register unsigned i, d, combo = es.combo;
	unsigned enough_mp = 1;

	for (i = 0; enough_mp && (d = __builtin_ffs(combo)); combo >>= d) {
		switch (spell_cast(ent_ref, &ent, ent.target, i) - 1) {
			case -1: enough_mp = 0;
				 break;
			case 1:  return ent;
		}
	}

	if (!enough_mp)
		nd_writef(ent_ref, "Not enough mana.\n");

	return ent;
}

void
debuf_end(ENT *ent, struct es *es, unsigned i)
{
	struct debuf *d = &es->debufs[i];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	SSPE *sspe = (SSPE *) &skel.sp.raw;
	struct effect *e = &ent->e[DEBUF_TYPE(sspe)];
	i = 1 << i;

	es->debuf_mask ^= i;
	e->mask ^= i;
	e->value -= d->val;
}

int
debufs_process(unsigned ent_ref, ENT *ent)
{
	struct es es;
	register unsigned mask, i, aux;
	short hpi = 0, dmg;
	struct debuf *d, *hd;

	nd_get(es_hd, &es, &ent_ref);

	for (mask = es.debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)
	{
		i += aux - 1;
		d = &es.debufs[i];
		d->duration--;
		if (d->duration <= 0) {
			debuf_end(ent, &es, i);
			continue;
		}
		SKEL skel;
		nd_get(HD_SKEL, &skel, &d->skel);
		SSPE *sspe = (SSPE *) &skel.sp.raw;
		// wtf is this special code?
		if (DEBUF_TYPE(sspe) == AF_HP) {
			dmg = ent_dmg(sspe->element, d->val,
				      EFFECT(ent, MDEF).value,
				      STAT_ELEMENT(ent, es, MDEF));
			hd = d;

			hpi += dmg;
		}
	}

	nd_get(es_hd, &ent_ref, &es);

	if (hpi) {
		debuf_notify(ent_ref, hd, hpi);
		return hpi;
	}

	return 0;
}

ENT sic_ent_update(unsigned ent_ref, ENT ent, double dt) {
	int damage = ent.aux;
	struct es es;
	nd_get(es_hd, &es, &ent_ref);

	omp = es.mp;
	if (ent.flags & EF_SITTING) {
		int div = 100;
		int max, cur;
		damage += dt * HP_MAX(&ent) / div;
		max = MP_MAX(&ent);
		cur = es.mp + (max / div);
		es.mp = cur > max ? max : cur;
	}

	nd_put(es_hd, &ent_ref, &es);
	damage += debufs_process(ent_ref, &ent);

	return ent;
}

ENT sic_ent_after_update(unsigned ent_ref, ENT ent) {
	struct es es;
	nd_get(es_hd, &es, &ent_ref);
	ent.aux = es.mp != omp; // mcp_mp_bar
	return ent;
}

ENT sic_birth(unsigned ent_ref, ENT ent) {
	register int j;
	struct es es;
	memset(&es, 0, sizeof(es));

	EFFECT(&ent, DMG).value = DMG_BASE(&ent);
	EFFECT(&ent, DODGE).value = DODGE_BASE(&ent);
	es.mp = MP_MAX(&ent);

	for (j = 0; j < 8; j++) {
		struct spell *sp = &es.spells[j];
		SKEL skel;
		unsigned ref = HEAL_SKEL_REF;
		nd_get(HD_SKEL, &skel, &ref);
		SSPE *sspe = (SSPE *) &skel.sp.raw;
		sp->val = SPELL_DMG(&ent, sspe);
		sp->cost = SPELL_COST(sp->val, sspe->y, sspe->flags & AF_BUF);
	}

	nd_put(es_hd, &ent_ref, &es);

	return ent;
}


void
debufs_end(ENT *eplayer, struct es *es)
{
	register unsigned mask, i, aux;

	for (mask = es->debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)

		 debuf_end(eplayer, es, i += aux - 1);
}


ENT sic_death(unsigned ent_ref, ENT ent) {
	struct es es;
	nd_get(es_hd, &es, &ent_ref);
	es.mp = 1;
	debufs_end(&ent, &es);
	nd_put(es_hd, &ent_ref, &es);
	return ent;
}

ENT sic_dodge(unsigned ent_ref, ENT ent) {
	if (ent.aux) {
		ENT target = ent_get(ent.target);
		if (EFFECT(&target, MOV).value)
			ent.aux = 0;
	}

	return ent;
}

struct hit sic_hit(unsigned ent_ref, ENT ent, ENT target, struct hit hit) {
	struct es es;

	nd_get(es_hd, &es, &ent_ref);

	unsigned at = STAT_ELEMENT(&ent, es, MDMG);
	unsigned dt = STAT_ELEMENT(&target, es, MDEF);
	short aval = -ent_dmg(ELM_PHYSICAL, EFFECT(&ent, DMG).value, EFFECT(&target, DEF).value + EFFECT(&target, MDEF).value, dt);
	short bval = -ent_dmg(at, EFFECT(&ent, MDMG).value, EFFECT(&target, MDEF).value, dt);
	element_t element;
	nd_get(HD_ELEMENT, &element, &at);
	hit.ndmg = aval;
	hit.cdmg = bval;
	hit.color = element.color;
	return hit;
}

int sic_before_attack(unsigned ent_ref, ENT ent) {
	struct es es;
	nd_get(es_hd, &es, &ent_ref);
	omp = es.mp;
	return 0;
}

ENT sic_after_attack(unsigned ent_ref, ENT ent) {
	struct es es;
	nd_get(es_hd, &es, &ent_ref);
	ent.aux = es.mp != omp;
	return ent;
}

ENT sic_reroll(unsigned ent_ref, ENT ent) {
	EFFECT(&ent, DMG).value = DMG_BASE(&ent);
	EFFECT(&ent, DODGE).value = DODGE_BASE(&ent);
	return ent;
}

int sic_status(unsigned ent_ref, ENT ent) {
	struct es es;
	nd_get(es_hd, &es, &ent_ref);
	nd_writef(ent_ref, "mp %u/%u\t"
		"combo 0x%x \tdebuf_mask 0x%x\n",
		es.mp, MP_MAX(&ent),
		es.combo, es.debuf_mask);
	return 0;
}

void mcp_mp_bar(unsigned ent_ref) {
	ENT ent = ent_get(ent_ref);
	struct es es;
	nd_get(es_hd, &es, &ent_ref);
	mcp_bar(bcp_mp, ent_ref, es.mp, MP_MAX(&ent));
}

void
do_heal(int fd, int argc __attribute__((unused)), char *argv[])
{
	char *name = argv[1];
	unsigned player_ref = fd_player(fd), target_ref;
	struct es es;

	if (strcmp(name, "me")) {
		target_ref = ematch_near(player_ref, name);
	} else
		target_ref = player_ref;

	if (target_ref == NOTHING || !(ent_get(player_ref).flags & EF_WIZARD)) {
                nd_writef(player_ref, CANTDO_MESSAGE);
		return;
	}

	nd_get(es_hd, &es, &player_ref);
	ENT etarget = ent_get(target_ref);

	etarget.hp = HP_MAX(&etarget);
	es.mp = MP_MAX(&etarget);
	etarget.huth[HUTH_THIRST] = etarget.huth[HUTH_HUNGER] = 0;
	debufs_end(&etarget, &es);
	ent_set(target_ref, &etarget);
	nd_put(es_hd, &player_ref, &es);
	notify_wts_to(player_ref, target_ref, "heal", "heals", "");
	mcp_hp_bar(target_ref);
	mcp_mp_bar(target_ref);
}

int sic_vim(unsigned ent_ref, sic_str_t ss, int ofs) {
	char *opcs = ss.str + ofs;
	char *end;
	if (isdigit(*opcs)) {
		struct es es;
		unsigned combo = strtol(opcs, &end, 0);
		nd_get(es_hd, &es, &ent_ref);
		es.combo = combo;
		nd_put(es_hd, &ent_ref, &es);
		nd_writef(ent_ref, "Set combo to 0x%x.\n", combo);
		return (end - opcs);
	} else if (*opcs == 'c' && isdigit(opcs[1])) {
		unsigned slot = strtol(opcs + 1, &end, 0);
		OBJ player;
		nd_get(HD_OBJ, &player, &ent_ref);
		if (player.location == 0)
			nd_writef(ent_ref, "You may not cast spells in room 0.\n");
		else {
			ENT ent = ent_get(ent_ref);
			spell_cast(ent_ref, &ent, ent.target, slot);
			ent_set(ent_ref, &ent);
		}
		return (end - opcs);
	} else
		return 0;
}

void
mod_open(void *arg __attribute__((unused))) {
	nd_len_reg("es", sizeof(struct es));
	es_hd = nd_open("ent_spell", "u", "es", ND_AINDEX);
}

void
mod_install(void *arg __attribute__((unused))) {
	type_spell = nd_put(HD_TYPE, NULL, "spell");

	spell_new("Heal", ELM_PHYSICAL, 3, 1, 2, AF_HP);
	spell_new("Focus", ELM_PHYSICAL, 15,3, 1, AF_MDMG | AF_BUF);
	spell_new("Fire Focus", ELM_FIRE, 15,3, 1, AF_MDMG | AF_BUF);
	spell_new("Cut", ELM_PHYSICAL, 15,1, 2, AF_NEG);
	spell_new("Fireball", ELM_FIRE, 3, 1, 2, AF_NEG);
	spell_new("Weaken", ELM_PHYSICAL, 15,3, 1, AF_MDMG | AF_BUF | AF_NEG);
	spell_new("Distract", ELM_PHYSICAL, 15, 3, 1, AF_MDEF | AF_BUF | AF_NEG);
	spell_new("Freeze", ELM_WATER, 10, 2, 4, AF_MOV | AF_NEG);
	spell_new("Lava Shield", ELM_FIRE, 15, 3, 1, AF_MDEF | AF_BUF);
	spell_new("Wind Veil", ELM_AIR, 0, 0, 0, AF_DODGE);
	spell_new("Stone Skin", ELM_EARTH, 0, 0, 0, AF_DEF);

	nd_register("heal", do_heal, 0);
	bcp_mp = nd_put(HD_BCP, NULL, "mp");
	mod_open(arg);
}
