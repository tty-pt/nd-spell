#include "./include/uapi/spell.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <nd/nd.h>
#include <nd/equip.h>
#include <nd/mortal.h>
#include <nd/fight.h>
#include <nd/seat.h>

#define HEAL_SKEL_REF 1
#define SPELL_COST(dmg, y, no_bdmg) (no_bdmg ? 0 : dmg) + dmg / (1 << y)

#define MSRA(ms, ra, G) G(ms) * (ra + 1) / RARE_MAX
#define HS(sp) MSRA(sp->ms, sp->ra, SPELL_G)
#define SPELL_G(v) G(v)
#define SPELL_DMG(mortal, sp) SPELL_G((mortal)->attr[ATTR_INT]) + HS(sp)

#define DEBUF_DURATION(ra) 20 * (RARE_MAX - ra) / RARE_MAX
#define DEBUF_DMG(sp_dmg, duration) ((long) 2 * sp_dmg) / duration
#define DEBUF_TYPE_MASK 0xf
#define DEBUF_TYPE(sp) (sp->flags & DEBUF_TYPE_MASK)

#define STAT_ELEMENT(fighter, caster, type) \
	mask_element(caster, EFFECT(fighter, type).mask)


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

unsigned bcp_mp, type_spell, caster_hd;
unsigned mortal_hd, fighter_hd, sitter_hd;
unsigned omp;

unsigned spell_new(char *name, unsigned element,
		unsigned char ms, unsigned char ra,
		unsigned char y, unsigned char flags)
{
	SKEL skel = {
		.type = type_spell,
	};

	spell_skeleton_t sspe = {
		.element = element,
		.ms = ms, .ra = ra, .y = y,
		.flags = flags,
	};

	memcpy((void *) &skel.name, name, sizeof(skel.name));
	memcpy(&skel.data, &sspe, sizeof(sspe));
	return nd_put(HD_SKEL, NULL, &skel);
}

static inline char*
debuf_wts(spell_skeleton_t *_sp)
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
sp_color(spell_skeleton_t *_sp)
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
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	char *wts = debuf_wts(sspe);

	if (val)
		snprintf(buf, sizeof(buf), " (%s%d%s)", ansi_fg[sp_color(sspe)], val, ANSI_RESET);
	else
		*buf = '\0';

	notify_wts(player_ref, wts, wts_plural(wts), "%s", buf);
}

static inline int
debuf_start(unsigned ent_ref, fighter_t *fighter, struct spell *sp, short val)
{
	SKEL skel;
	caster_t caster;

	nd_get(HD_SKEL, &skel, &sp->skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	nd_get(caster_hd, &caster, &ent_ref);
	ENT eplayer = ent_get(ent_ref);
	struct debuf *d;
	int i;

	if (caster.debuf_mask) {
		i = __builtin_ffs(~caster.debuf_mask);
		if (!i)
			return -1;
		i--;
	} else
		i = 0;

	d = &caster.debufs[i];
	d->skel = sp->skel;
	d->duration = DEBUF_DURATION(sspe->ra);
	d->val = DEBUF_DMG(val, d->duration);

	i = 1 << i;
	caster.debuf_mask |= i;

	struct effect *e = &fighter->e[DEBUF_TYPE(sspe)];
	e->mask |= i;
	e->value += d->val;

	debuf_notify(ent_ref, d, 0);
	ent_set(ent_ref, &eplayer);
	nd_put(caster_hd, &ent_ref, &caster);

	return 0;
}

static inline unsigned
mask_element(caster_t caster, register unsigned char a)
{
	unsigned skel_id = caster.debufs[__builtin_ffs(a) - 1].skel;
	SKEL skel;
	nd_get(HD_SKEL, &skel, &skel_id);

	if (!a)
		return ELM_PHYSICAL;

	return ((spell_skeleton_t *) &skel.data)->element;
}

static inline int
spell_cast(unsigned ent_ref, unsigned target_ref, unsigned slot)
{
	mortal_t mortal;
	fighter_t fighter, target;
	caster_t caster;
	struct spell sp;
	hit_t hit = { .ndmg = 0, };
	SKEL skel;
	ENT etarget = ent_get(target_ref);

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(fighter_hd, &fighter, &ent_ref);
	sp = caster.spells[slot];

	nd_get(HD_SKEL, &skel, &sp.skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;

	unsigned mana = caster.mp;
	sic_str_t ss_a, ss_c;
	char c[BUFSIZ + 32];

	enum color color = sp_color(sspe);

	if (mana < sp.cost)
		return -1;

	snprintf(ss_a.str, sizeof(ss_a.str), "%s%s"ANSI_RESET, ansi_fg[color], skel.name);

	mana -= sp.cost;
	caster.mp = mana > 0 ? mana : 0;
	nd_put(caster_hd, &ent_ref, &caster);

	nd_get(fighter_hd, &target, &target_ref);

	SIC_CALL(&hit.cdmg, fight_damage, sspe->element, sp.val,
			EFFECT(&target, MDEF).value,
			STAT_ELEMENT(&fighter, caster, MDEF));

	element_t element;
	nd_get(HD_ELEMENT, &element, &sspe->element);
	hit.color = element.color;

	if (sspe->flags & AF_NEG)
		hit.ndmg = -hit.ndmg;
	else
		target_ref = ent_ref;

	snprintf(ss_c.str, sizeof(ss_c.str), "cast %s on", ss_a.str);

	int ret;
	SIC_CALL(&ret, fighter_attack, ent_ref, ss_c, hit);

	if (ret && random() < (RAND_MAX >> sspe->y))
		debuf_start(target_ref, &target, &sp, hit.cdmg);

	return 0;
}

int on_will_attack(unsigned ent_ref) {
	fighter_t fighter, target;
	caster_t caster, target_caster;
	element_t element;

	nd_get(fighter_hd, &fighter, &ent_ref);

	if (!fighter.target)
		return 0;

	// add spell features to the normal attack
	// (like enchantments)

	nd_get(fighter_hd, &target, &fighter.target);

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(caster_hd, &target_caster, &fighter.target);

	unsigned at = STAT_ELEMENT(&fighter, caster, MDMG);
	unsigned dt = STAT_ELEMENT(&target, target_caster, MDEF);

	SIC_CALL(&fighter.attack.ndmg, fight_damage, ELM_PHYSICAL, EFFECT(&fighter, DMG).value, EFFECT(&target, DEF).value + EFFECT(&target, MDEF).value, dt);
	SIC_CALL(&fighter.attack.cdmg, fight_damage, at, EFFECT(&fighter, MDMG).value, EFFECT(&target, MDEF).value, dt);
	fighter.attack.ndmg = -fighter.attack.ndmg;
	fighter.attack.cdmg = -fighter.attack.cdmg;

	nd_get(HD_ELEMENT, &element, &at);
	fighter.attack.color = element.color;

	// now cast spells!

	register unsigned char mask = EFFECT(&fighter, MOV).mask;

	if (mask) {
		register unsigned i = __builtin_ffs(mask) - 1;
		debuf_notify(ent_ref, &caster.debufs[i], 0);
		return 0;
	}

	// second part
	register unsigned i, d, combo = caster.combo;
	unsigned enough_mp = 1, cast = 0;

	for (i = 0; enough_mp && (d = __builtin_ffs(combo)); combo >>= d) {
		switch (spell_cast(ent_ref, fighter.target, i) - 1) {
			case -1: enough_mp = 0;
				 break;
			case 1:
				 return 0;
			default:
				 cast++;
		}
	}

	if (!enough_mp)
		nd_writef(ent_ref, "Not enough mana.\n");
	else if (cast) {
		nd_get(fighter_hd, &fighter, &ent_ref);
		fighter.flags &= ~FF_ATTACK;
		nd_put(fighter_hd, &ent_ref, &fighter);
	}

	return 0;
}

void
debuf_end(fighter_t *fighter, caster_t *caster, unsigned i)
{
	struct debuf *d = &caster->debufs[i];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	struct effect *e = &fighter->e[DEBUF_TYPE(sspe)];
	i = 1 << i;

	caster->debuf_mask ^= i;
	e->mask ^= i;
	e->value -= d->val;
}

int
debufs_process(unsigned ent_ref)
{
	caster_t caster;
	fighter_t fighter;
	register unsigned mask, i, aux;
	short hpi = 0, dmg;
	struct debuf *d, *hd;

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(fighter_hd, &fighter, &ent_ref);

	for (mask = caster.debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)
	{
		i += aux - 1;
		d = &caster.debufs[i];
		d->duration--;
		if (d->duration <= 0) {
			debuf_end(&fighter, &caster, i);
			continue;
		}
		SKEL skel;
		nd_get(HD_SKEL, &skel, &d->skel);
		spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
		// wtf is this special code?
		if (DEBUF_TYPE(sspe) == AF_HP) {
			SIC_CALL(&dmg, fight_damage, sspe->element, d->val,
				      EFFECT(&fighter, MDEF).value,
				      STAT_ELEMENT(&fighter, caster, MDEF));

			hd = d;

			hpi += dmg;
		}
	}

	nd_get(caster_hd, &ent_ref, &caster);

	if (hpi) {
		debuf_notify(ent_ref, hd, hpi);
		return hpi;
	}

	return 0;
}

void mcp_mp(unsigned ent_ref) {
	fighter_t fighter;
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(fighter_hd, &fighter, &ent_ref);

	mcp_bar(bcp_mp, ent_ref, caster.mp, MP_MAX(&fighter));
}

int on_mortal_survival(unsigned ent_ref, double dt) {
	fighter_t fighter;
	caster_t caster;
	mortal_t mortal;
	sitter_t sitter;
	ENT ent = ent_get(ent_ref);

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(fighter_hd, &fighter, &ent_ref);
	nd_get(mortal_hd, &mortal, &ent_ref);
	nd_get(sitter_hd, &sitter, &ent_ref);

	int damage;

	omp = caster.mp;
	if (sitter != STANDING) {
		int div = 100;
		int max, cur;
		damage += dt * HP_MAX(&fighter) / div;
		max = MP_MAX(&mortal);
		cur = caster.mp + (max / div);
		caster.mp = cur > max ? max : cur;
	}

	nd_put(caster_hd, &ent_ref, &caster);
	damage += debufs_process(ent_ref);

	SIC_CALL(NULL, mortal_damage, NOTHING, ent_ref, damage);

	if (caster.mp != omp)
		mcp_mp(ent_ref);

	return 0;
}

int on_birth(unsigned ref, uint64_t v) {
	register int j;
	caster_t caster;
	fighter_t fighter;
	mortal_t mortal;
	memset(&caster, 0, sizeof(caster));

	nd_get(fighter_hd, &fighter, &ref);
	nd_get(mortal_hd, &mortal, &ref);
	caster.mp = MP_MAX(&fighter);

	for (j = 0; j < 8; j++) {
		struct spell *sp = &caster.spells[j];
		SKEL skel;
		unsigned ref = HEAL_SKEL_REF;
		nd_get(HD_SKEL, &skel, &ref);
		spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
		sp->val = SPELL_DMG(&mortal, sspe);
		sp->cost = SPELL_COST(sp->val, sspe->y, sspe->flags & AF_BUF);
	}

	nd_put(caster_hd, &ref, &caster);
	return 0;
}


void
debufs_end(fighter_t *fighter, caster_t *caster)
{
	register unsigned mask, i, aux;

	for (mask = caster->debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)

		 debuf_end(fighter, caster, i += aux - 1);
}


int on_death(unsigned ent_ref) {
	caster_t caster;
	fighter_t fighter;

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(fighter_hd, &fighter, &ent_ref);

	caster.mp = 1;
	debufs_end(&fighter, &caster);

	nd_put(caster_hd, &ent_ref, &caster);
	nd_put(fighter_hd, &ent_ref, &fighter);
	return 0;
}

int on_mortal_life(unsigned ent_ref, double dt) {
	caster_t caster;
	nd_get(caster_hd, &caster, &ent_ref);
	omp = caster.mp;
	return 0;
}

int on_status(unsigned ent_ref) {
	caster_t caster;
	fighter_t fighter;

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(fighter_hd, &fighter, &ent_ref);

	nd_writef(ent_ref, "mp %u/%u\t"
		"combo 0x%x \tdebuf_mask 0x%x\n",
		caster.mp, MP_MAX(&fighter),
		caster.combo, caster.debuf_mask);

	return 0;
}

void
do_heal(int fd, int argc __attribute__((unused)), char *argv[])
{
	char *name = argv[1];
	unsigned player_ref = fd_player(fd), target_ref;
	caster_t caster_target;
	mortal_t mortal_target;
	fighter_t fighter_target;

	if (strcmp(name, "me")) {
		target_ref = ematch_near(player_ref, name);
	} else
		target_ref = player_ref;

	if (target_ref == NOTHING || !(ent_get(player_ref).flags & EF_WIZARD)) {
                nd_writef(player_ref, CANTDO_MESSAGE);
		return;
	}

	nd_get(caster_hd, &caster_target, &player_ref);
	nd_get(mortal_hd, &mortal_target, &target_ref);
	nd_get(fighter_hd, &fighter_target, &target_ref);

	debufs_end(&fighter_target, &caster_target);
	mortal_target.hp = HP_MAX(&mortal_target);
	caster_target.mp = MP_MAX(&fighter_target);
	mortal_target.huth[HUTH_THIRST]
		= mortal_target.huth[HUTH_HUNGER] = 0;

	nd_put(caster_hd, &target_ref, &caster_target);
	nd_put(mortal_hd, &target_ref, &mortal_target);

	SIC_CALL(NULL, mcp_hp, target_ref);
	mcp_mp(target_ref);

	notify_wts_to(player_ref, target_ref, "heal", "heals", "");
}

int on_vim(unsigned ent_ref, sic_str_t ss, int ofs) {
	char *opcs = ss.str + ofs;
	char *end;
	if (isdigit(*opcs)) {
		caster_t caster;
		unsigned combo = strtol(opcs, &end, 0);
		nd_get(caster_hd, &caster, &ent_ref);
		caster.combo = combo;
		nd_put(caster_hd, &ent_ref, &caster);
		nd_writef(ent_ref, "Set combo to 0x%x.\n", combo);
		return (end - opcs);
	} else if (*opcs == 'c' && isdigit(opcs[1])) {
		unsigned slot = strtol(opcs + 1, &end, 0);
		OBJ player;
		nd_get(HD_OBJ, &player, &ent_ref);
		if (player.location == 0)
			nd_writef(ent_ref, "You may not cast spells in room 0.\n");
		else {
			fighter_t fighter;
			nd_get(fighter_hd, &fighter, &ent_ref);
			spell_cast(ent_ref, fighter.target, slot);
		}
		return (end - opcs);
	} else
		return 0;
}

void
mod_open(void *arg __attribute__((unused))) {
	nd_len_reg("caster", sizeof(caster_t));
	caster_hd = nd_open("ent_spell", "u", "caster", 0);

	type_spell = nd_put(HD_TYPE, NULL, "spell");

	bcp_mp = nd_put(HD_BCP, NULL, "mp");
	nd_get(HD_HD, "fighter", &fighter_hd);
	nd_get(HD_HD, "mortal", &mortal_hd);
	nd_get(HD_HD, "sitter", &sitter_hd);

	nd_register("heal", do_heal, 0);
}

void
mod_install(void *arg __attribute__((unused))) {
	mod_open(arg);

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
}
