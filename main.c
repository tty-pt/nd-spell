#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <nd/nd.h>
#include <nd/verb.h>
#include <nd/attr.h>
#include <nd/equip.h>
#include <nd/mortal.h>
#include <nd/fight.h>
#include <nd/seat.h>

#define HEAL_SKEL_REF 1
#define SPELL_COST(dmg, y, no_bdmg) (no_bdmg ? 0 : dmg) + dmg / (1 << y)

#define DEBUF_DURATION(ra) 20 * (RARE_MAX - ra) / RARE_MAX
#define DEBUF_DMG(sp_dmg, duration) ((long) 2 * sp_dmg) / duration
#define DEBUF_TYPE_MASK 0xf
#define DEBUF_TYPE(sp) (sp->flags & DEBUF_TYPE_MASK)

typedef struct {
	unsigned element;
	unsigned char ms, ra, y, flags;
} spell_skeleton_t;

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
unsigned omp;
unsigned wt_heal;

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

static inline unsigned
debuf_wts(spell_skeleton_t *_sp)
{
	register unsigned char mask = _sp->flags;
	register unsigned idx = (DEBUF_TYPE(_sp) << 1) + ((mask >> 4) & 1);
	unsigned wts_ref;
	extern unsigned awts_hd, wts_hd;
	unsigned ref = (_sp->element << 4) | idx;
	nd_get(HD_RWTS, &wts_ref, &ref);
	return wts_ref;
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
debuf_notify(unsigned player_ref, struct debuf *d, unsigned val)
{
	char buf[BUFSIZ];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	unsigned wt_debuf = debuf_wts(sspe);

	if (val)
		snprintf(buf, sizeof(buf), " (%s%d%s)", ansi_fg[sp_color(sspe)], val, ANSI_RESET);
	else
		*buf = '\0';

	call_verb(player_ref, wt_debuf, buf);
}

long effect(unsigned ref, enum affect slot) {
	caster_t caster;
	SKEL skel;
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	long last;

	sic_last(&last);
	if (nd_get(caster_hd, &caster, &ref))
		return last;

	for (int i = 0; i < 8; i++) {
		struct debuf *d = &caster.debufs[i];
		if (d->skel == NOTHING)
			continue;
		nd_get(HD_SKEL, &skel, &d->skel);
		if (DEBUF_TYPE(sspe) != slot)
			continue;
		last += d->val;
	}

	return last;
}

static inline int
debuf_start(unsigned ent_ref, struct spell *sp, unsigned val)
{
	SKEL skel;
	caster_t caster_target;
	unsigned target_ref = call_fighter_target(ent_ref);

	nd_get(HD_SKEL, &skel, &sp->skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	nd_get(caster_hd, &caster_target, &target_ref);
	struct debuf *d;
	int i;

	if (caster_target.debuf_mask) {
		i = __builtin_ffs(~caster_target.debuf_mask);
		if (!i)
			return -1;
		i--;
	} else
		i = 0;

	d = &caster_target.debufs[i];
	d->skel = sp->skel;
	d->duration = DEBUF_DURATION(sspe->ra);
	d->val = DEBUF_DMG(val, d->duration);

	i = 1 << i;
	caster_target.debuf_mask |= i;

	debuf_notify(ent_ref, d, 0);
	nd_put(caster_hd, &ent_ref, &caster_target);

	return 0;
}

static inline unsigned
stat_element(caster_t *caster, register unsigned char mask)
{
	unsigned skel_id = caster->debufs[__builtin_ffs(mask) - 1].skel;
	SKEL skel;
	nd_get(HD_SKEL, &skel, &skel_id);

	if (!mask)
		return ELM_PHYSICAL;

	return ((spell_skeleton_t *) &skel.data)->element;
}

static inline int
spell_cast(unsigned ent_ref, unsigned target_ref, unsigned slot)
{
	caster_t caster;
	struct spell sp;
	hit_t hit = { .ndmg = 0, };
	SKEL skel;

	nd_get(caster_hd, &caster, &ent_ref);
	sp = caster.spells[slot];

	nd_get(HD_SKEL, &skel, &sp.skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;

	unsigned mana = caster.mp;
	sic_str_t ss_a, ss_c;

	enum color color = sp_color(sspe);

	if (mana < sp.cost)
		return -1;

	snprintf(ss_a.str, sizeof(ss_a.str), "%s%s"ANSI_RESET, ansi_fg[color], skel.name);

	mana -= sp.cost;
	caster.mp = mana > 0 ? mana : 0;
	nd_put(caster_hd, &ent_ref, &caster);

	hit.cdmg = call_fight_damage(
			sspe->element, sp.val,
			call_effect(target_ref, AF_MDEF),
			stat_element(&caster, caster.mdef_mask));

	element_t element;
	nd_get(HD_ELEMENT, &element, &sspe->element);
	hit.color = element.color;

	if (sspe->flags & AF_NEG)
		hit.ndmg = -hit.ndmg;
	else
		target_ref = ent_ref;

	snprintf(ss_c.str, sizeof(ss_c.str), "cast %s on", ss_a.str);

	int ret = call_fighter_attack(ent_ref, ss_c, hit);

	if (ret && random() < (RAND_MAX >> sspe->y))
		debuf_start(target_ref, &sp, hit.cdmg);

	return 0;
}

hit_t on_will_attack(unsigned ent_ref, double dt __attribute__((unused))) {
	hit_t last, hit;
	unsigned target_ref = call_fighter_target(ent_ref);
	caster_t caster, target_caster;
	element_t element;

	sic_last(&last);

	if (!target_ref)
		return last;

	// add spell features to the normal attack
	// (like enchantments)

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(caster_hd, &target_caster, &target_ref);

	unsigned dmg_el = stat_element(&caster, caster.mdmg_mask);
	unsigned def_el = stat_element(&target_caster, target_caster.mdef_mask);

	long dmg = call_effect(ent_ref, AF_DMG),
		 mdmg = call_effect(ent_ref, AF_MDMG),
		 def = call_effect(target_ref, AF_DEF),
		 mdef = call_effect(target_ref, AF_MDEF);

	nd_get(HD_ELEMENT, &element, &dmg_el);

	hit.ndmg = -call_fight_damage(ELM_PHYSICAL, dmg, def + mdef, def_el);;
	hit.cdmg = -call_fight_damage(dmg_el, mdmg, mdef, def_el);
	hit.color = element.color;

	// now cast spells!

	register unsigned char mask = caster.mov_mask;

	if (mask) {
		register unsigned i = __builtin_ffs(mask) - 1;
		debuf_notify(ent_ref, &caster.debufs[i], 0);
		return hit;
	}

	// second part
	register unsigned i, d, combo = caster.combo;

	for (i = 0; (d = __builtin_ffs(combo)); combo >>= d) {
		if (spell_cast(ent_ref, target_ref, i)) {
			nd_writef(ent_ref, "Not enough mana.\n");
			break;
		}
	}

	return hit;
}

void
debuf_end(caster_t *caster, unsigned i)
{
	struct debuf *d = &caster->debufs[i];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	caster->debuf_mask ^= 1 << i;
	// TODO make debug mask the true source of debuf activation
}

int
debufs_process(unsigned ent_ref)
{
	caster_t caster;
	register unsigned mask, i, aux;
	long hpi = 0;
	struct debuf *d, *hd;

	nd_get(caster_hd, &caster, &ent_ref);

	for (mask = caster.debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)
	{
		i += aux - 1;
		d = &caster.debufs[i];
		if (d->skel == NOTHING)
			continue;
		d->duration--;
		if (d->duration <= 0) {
			debuf_end(&caster, i);
			continue;
		}
		SKEL skel;
		nd_get(HD_SKEL, &skel, &d->skel);
		spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
		// wtf is this special code?
		if (DEBUF_TYPE(sspe) == AF_HP) {
			hd = d;

			hpi += call_fight_damage(sspe->element, d->val,
					call_effect(ent_ref, AF_MDEF),
					stat_element(&caster, caster.mdef_mask));
		}
	}

	nd_get(caster_hd, &ent_ref, &caster);

	if (!hpi)
		return 0;

	debuf_notify(ent_ref, hd, hpi);
	return hpi;
}

void mcp_mp(unsigned ent_ref) {
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	mcp_bar(bcp_mp, ent_ref, caster.mp, call_mp_max(ent_ref));
}

int on_mortal_survival(unsigned ent_ref, double dt __attribute__((unused))) {
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	long damage = 0;

	omp = caster.mp;
	if (call_sitting(ent_ref)) {
		int div = 100;
		long cur;
		long mp_max = call_mp_max(ent_ref);
		damage += dt * call_hp_max(ent_ref) / div;
		cur = caster.mp + (mp_max / div);
		caster.mp = cur > mp_max ? mp_max : cur;
	}

	nd_put(caster_hd, &ent_ref, &caster);
	damage += debufs_process(ent_ref);

	if (damage)
		call_mortal_damage(NOTHING, ent_ref, damage);

	if (caster.mp != omp)
		mcp_mp(ent_ref);

	return 0;
}

int on_add(unsigned ref, unsigned type, uint64_t v __attribute__((unused))) {
	register int j;
	caster_t caster;
	memset(&caster, 0, sizeof(caster));

	if (type != TYPE_ENTITY)
		return 1;

	caster.mp = call_mp_max(ref);

	long mdmg = call_effect(ref, AF_MDMG);

	for (j = 0; j < 8; j++) {
		struct spell *sp = &caster.spells[j];
		SKEL skel;
		unsigned ref = HEAL_SKEL_REF;
		nd_get(HD_SKEL, &skel, &ref);
		spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
		sp->val = mdmg + G(sspe->ms) * (sspe->ra + 1) / RARE_MAX;
		sp->cost = SPELL_COST(sp->val, sspe->y, sspe->flags & AF_BUF);
		caster.debufs[j].skel = NOTHING;
	}

	nd_put(caster_hd, &ref, &caster);
	return 0;
}


void
debufs_end(caster_t *caster)
{
	register unsigned mask, i, aux;

	for (mask = caster->debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)

		 debuf_end(caster, i += aux - 1);
}

int on_death(unsigned ent_ref) {
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	caster.mp = 1;
	debufs_end(&caster);

	nd_put(caster_hd, &ent_ref, &caster);
	return 0;
}

int on_mortal_life(unsigned ent_ref, double dt __attribute__((unused))) {
	caster_t caster;
	nd_get(caster_hd, &caster, &ent_ref);
	omp = caster.mp;
	return 0;
}

int on_status(unsigned ent_ref) {
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	nd_writef(ent_ref, "Spell\tmp %5u, mmp %4u, com %4x, dem %4x\n",
		caster.mp, call_mp_max(ent_ref),
		caster.combo, caster.debuf_mask);

	return 0;
}

int heal(unsigned ref) {
	caster_t caster;
	nd_get(caster_hd, &caster, &ref);

	debufs_end(&caster);
	caster.mp = call_mp_max(ref);

	nd_put(caster_hd, &ref, &caster);
	mcp_mp(ref);
	return 0;
}

void
do_heal(int fd, int argc __attribute__((unused)), char *argv[])
{
	char *name = argv[1];
	unsigned player_ref = fd_player(fd), target_ref;
	caster_t caster_target;

	if (strcmp(name, "me")) {
		target_ref = ematch_near(player_ref, name);
	} else
		target_ref = player_ref;

	if (target_ref == NOTHING || !(ent_get(player_ref).flags & EF_WIZARD)) {
                nd_writef(player_ref, CANTDO_MESSAGE);
		return;
	}

	nd_get(caster_hd, &caster_target, &player_ref);
	heal(target_ref);

	call_verb_to(player_ref, target_ref, wt_heal, "");
}

sic_str_t on_vim(unsigned ent_ref, sic_str_t ss) {
	char *opcs = ss.str + ss.pos;
	char *end;
	if (isdigit(*opcs)) {
		caster_t caster;
		unsigned combo = strtol(opcs, &end, 0);
		nd_get(caster_hd, &caster, &ent_ref);
		caster.combo = combo;
		nd_put(caster_hd, &ent_ref, &caster);
		nd_writef(ent_ref, "Set combo to 0x%x.\n", combo);
		ss.pos += end - opcs;
		return ss;
	} else if (*opcs == 'c' && isdigit(opcs[1])) {
		unsigned slot = strtol(opcs + 1, &end, 0);
		OBJ player;
		nd_get(HD_OBJ, &player, &ent_ref);
		if (player.location == 0)
			nd_writef(ent_ref, "You may not cast spells in room 0.\n");
		else
			spell_cast(ent_ref, call_fighter_target(ent_ref), slot);
		ss.pos += end - opcs;
		return ss;
	} else
		return ss;
}

void
mod_open(void *arg __attribute__((unused))) {
	nd_len_reg("caster", sizeof(caster_t));
	caster_hd = nd_open("ent_spell", "u", "caster", 0);

	type_spell = nd_put(HD_TYPE, NULL, "spell");

	bcp_mp = nd_put(HD_BCP, NULL, "mp");

	nd_register("heal", do_heal, 0);
	nd_get(HD_RWTS, &wt_heal, "hit");
}

void
mod_install(void *arg __attribute__((unused))) {
	nd_put(HD_WTS, NULL, "heal");

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
