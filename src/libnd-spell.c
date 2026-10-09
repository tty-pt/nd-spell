/* src/libnd-spell.c — nd-spell, ported to libxylem.
 *
 * Owns spells and debufs: the eleven spells, mana, combos, damage-over-time,
 * and healing. Co-implements nd-attr's `effect` chain (debuf modifiers),
 * nd-fight's `on_will_attack` chain (spell strikes), nd-mortal's `heal`,
 * `on_death`, `on_mortal_life` and `on_mortal_survival`, and reads
 * nd-seat's `sitting` to gate mana regen. A pure consumer otherwise -- it
 * ships no header.
 *
 * Original: tty-pt/nd-spell @ 556 lines main.c, from the nd-basics
 * superproject.
 *
 * This TU XY_IMPLs effect, on_will_attack, on_mortal_survival, on_death,
 * on_mortal_life, heal, on_add, on_status and on_vim. It co-implements from
 * three guarded headers, so all three guards are defined:
 *
 *   - ATTR_IMPL (nd/attr.h): suppresses effect/mp_max/hp_max DECLs. effect is
 *     re-dispatched manually (see below); mp_max and hp_max are re-declared
 *     manually because they are called but not implemented.
 *   - FIGHT_IMPL (nd/fight.h): suppresses on_will_attack/fighter_* DECLs.
 *     fighter_target, fight_damage and fighter_attack are re-declared manually
 *     because they are called but not implemented.
 *   - MORTAL_IMPL (nd/mortal.h): suppresses heal/on_* DECLs. mortal_damage is
 *     re-declared manually because it is called but not implemented.
 *
 * effect is both implemented AND called here (the debuf total feeds damage
 * formulae). The XY_DECL cannot coexist with the XY_IMPL, so those call sites
 * dispatch manually through the registered adapter (effect_chain).
 *
 * heal is also implemented here AND in nd-mortal, but do_heal calls spell's
 * own heal directly -- exactly as the original did (it used `heal(...)`, not
 * `call_heal(...)`), so mortal's heal is bypassed by design, not by accident.
 *
 * The old call_verb()/call_verb_to() named a service that never existed
 * (MODS.md: "no such symbols exist anywhere in nd-basics"), so debuf and heal
 * announcements are written directly. The dead `extern unsigned awts_hd,
 * wts_hd;` in debuf_wts is dropped (declared, never used).
 *
 * No mod_open existed with a distinct body; xy_install is the old mod_install
 * (whose void *arg was ignored) with the open sequence inline.
 */

#include <ttypt/xy-mod.h>

#include <nd/xy.h>

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <nd/attr-types.h>
#include <nd/fight-types.h>
#include <nd/equip.h>
#include <nd/seat.h>

/* Services called but not implemented here. The three guards above suppress
 * their headers' XY_DECLs (to protect the names this TU XY_IMPLs), so the
 * called-but-not-implemented ones are re-declared here verbatim. */
XY_DECL(long, mp_max, unsigned, ref);
XY_DECL(long, hp_max, unsigned, ref);
XY_DECL(int, mortal_damage, unsigned, killer_ref, unsigned, victim_ref, long, amt);
XY_DECL(unsigned, fighter_target, unsigned, ref);
XY_DECL(long, fight_damage, unsigned, dmg_type, long, dmg, long, def, unsigned, def_type);
XY_DECL(int, fighter_attack, unsigned, player_ref, hit_t, hit);

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

static unsigned bcp_mp, type_spell, caster_hd;
static unsigned omp;
static unsigned wt_heal;

/* API. XY_IMPL both defines the function and emits the dispatch adapter, so
 * each name gets exactly one, with its body -- no forward declarations.
 *
 * Order matters below: XY_IMPL emits a definition, so a caller has to come
 * after its callee. spell_new leads because xy_install calls it; effect leads
 * the chain users because spell_cast and on_will_attack call it. */

static unsigned spell_new(char *name, unsigned element,
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
	return (unsigned)nd_put(HD_SKEL, NULL, &skel);
}

static inline unsigned
debuf_wts(spell_skeleton_t *_sp)
{
	register unsigned char mask = _sp->flags;
	register unsigned idx = (DEBUF_TYPE(_sp) << 1) + ((mask >> 4) & 1);
	unsigned wts_ref;
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

/* A debuf announcement. The old code called call_verb(player, wt, buf), but
 * verb never existed. "<Name><buf>" goes to the room, "You<buf>" to the
 * player. */
static void
say_debuf(unsigned player_ref, char *msg)
{
	OBJ player;
	char buf[BUFSIZ];
	int len;

	nd_get(HD_OBJ, &player, &player_ref);
	nd_printf(player_ref, "You%s\n", msg);
	len = snprintf(buf, sizeof(buf), "%s%s\n", player.name, msg);
	nd_rwrite(player.location, player_ref, buf, (size_t)len);
}

static void
debuf_notify(unsigned player_ref, struct debuf *d, unsigned val)
{
	char buf[BUFSIZ];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	unsigned wt_debuf = debuf_wts(sspe);

	(void) wt_debuf;
	if (val)
		snprintf(buf, sizeof(buf), " (%s%d%s)", ansi_fg[sp_color(sspe)], val, ANSI_RESET);
	else
		*buf = '\0';

	say_debuf(player_ref, buf);
}

/* Co-implementor of nd-attr's effect chain: active debufs on top of the base
 * value. nd_last() gives us whatever ran before us in this dispatch. */
XY_IMPL(long, effect, unsigned, ref, enum affect, slot)
{
	caster_t caster;
	SKEL skel;
	spell_skeleton_t *sspe = (spell_skeleton_t *) &skel.data;
	long last;

	nd_last(&last);
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

/* effect is XY_IMPL'd above AND called below (damage formulae need the full
 * chain total, including our own debufs). Dispatch manually; the struct and
 * adapter are emitted by our own XY_IMPL. */
static long
effect_chain(unsigned ref, enum affect slot)
{
	struct effect_args args = { ref, slot };
	long ret = 0;
	xy_call(&ret, &effect_adapter, &args);
	return ret;
}

static inline int
debuf_start(unsigned ent_ref, struct spell *sp, unsigned val)
{
	SKEL skel;
	caster_t caster_target;
	unsigned target_ref = fighter_target(ent_ref);

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

	hit.cdmg = fight_damage(
			sspe->element, sp.val,
			effect_chain(target_ref, AF_MDEF),
			stat_element(&caster, caster.mdef_mask));

	element_t element;
	nd_get(HD_ELEMENT, &element, &sspe->element);
	hit.color = element.color;

	if (sspe->flags & AF_NEG)
		hit.ndmg = -hit.ndmg;
	else
		target_ref = ent_ref;

	snprintf(ss_c.str, sizeof(ss_c.str), "cast %.200s on", ss_a.str);

	// FIXME
	/* int ret = fighter_attack(ent_ref, ss_c, hit); */
	int ret = fighter_attack(ent_ref, hit);

	if (ret && random() < (RAND_MAX >> sspe->y))
		debuf_start(target_ref, &sp, hit.cdmg);

	return 0;
}

/* Co-implementor of nd-fight's on_will_attack chain: spell strikes on top of
 * the martial hit. nd_last() gives us fight's (or seat's) hit; without a
 * target we pass it through unchanged. */
XY_IMPL(hit_t, on_will_attack, unsigned, ent_ref, double, dt)
{
	hit_t last = { 0 }, hit = { 0 };
	unsigned target_ref = fighter_target(ent_ref);
	caster_t caster, target_caster;
	element_t element = { 0 };

	(void) dt;
	nd_last(&last);

	if (!target_ref)
		return last;

	// add spell features to the normal attack
	// (like enchantments)

	nd_get(caster_hd, &caster, &ent_ref);
	nd_get(caster_hd, &target_caster, &target_ref);

	unsigned dmg_el = stat_element(&caster, caster.mdmg_mask);
	unsigned def_el = stat_element(&target_caster, target_caster.mdef_mask);

	long dmg = effect_chain(ent_ref, AF_DMG),
		 mdmg = effect_chain(ent_ref, AF_MDMG),
		 def = effect_chain(target_ref, AF_DEF),
		 mdef = effect_chain(target_ref, AF_MDEF);

	nd_get(HD_ELEMENT, &element, &dmg_el);

	hit.ndmg = -fight_damage(ELM_PHYSICAL, dmg, def + mdef, def_el);;
	hit.cdmg = -fight_damage(dmg_el, mdmg, mdef, def_el);
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
			nd_printf(ent_ref, "Not enough mana.\n");
			break;
		}
	}

	return hit;
}

static void
debuf_end(caster_t *caster, unsigned i)
{
	struct debuf *d = &caster->debufs[i];
	SKEL skel;
	nd_get(HD_SKEL, &skel, &d->skel);
	caster->debuf_mask ^= 1 << i;
	// TODO make debug mask the true source of debuf activation
}

static int
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

			hpi += fight_damage(sspe->element, d->val,
					effect_chain(ent_ref, AF_MDEF),
					stat_element(&caster, caster.mdef_mask));
		}
	}

	nd_get(caster_hd, &ent_ref, &caster);

	if (!hpi)
		return 0;

	debuf_notify(ent_ref, hd, hpi);
	return hpi;
}

static void mcp_mp(unsigned ent_ref) {
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	mcp_bar(bcp_mp, ent_ref, caster.mp, mp_max(ent_ref));
}

XY_IMPL(int, on_mortal_survival, unsigned, ent_ref, double, dt)
{
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	long damage = 0;

	omp = caster.mp;
	if (sitting(ent_ref)) {
		int div = 100;
		long cur;
		long mmax = mp_max(ent_ref);
		damage += dt * hp_max(ent_ref) / div;
		cur = caster.mp + (mmax / div);
		caster.mp = cur > mmax ? mmax : cur;
	}

	nd_put(caster_hd, &ent_ref, &caster);
	damage += debufs_process(ent_ref);

	if (damage)
		mortal_damage(NOTHING, ent_ref, damage);

	if (caster.mp != omp)
		mcp_mp(ent_ref);

	return 0;
}

XY_IMPL(int, on_add, unsigned, ref, unsigned, type, uint64_t, v)
{
	register int j;
	caster_t caster;
	memset(&caster, 0, sizeof(caster));

	(void) v;
	if (type != TYPE_ENTITY)
		return 1;

	caster.mp = mp_max(ref);

	long mdmg = effect_chain(ref, AF_MDMG);

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


static void
debufs_end(caster_t *caster)
{
	register unsigned mask, i, aux;

	for (mask = caster->debuf_mask, i = 0;
	     (aux = __builtin_ffs(mask));
	     i++, mask >>= aux)

		 debuf_end(caster, i += aux - 1);
}

XY_IMPL(int, on_death, unsigned, ent_ref)
{
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	caster.mp = 1;
	debufs_end(&caster);

	nd_put(caster_hd, &ent_ref, &caster);
	return 0;
}

XY_IMPL(int, on_mortal_life, unsigned, ent_ref, double, dt)
{
	caster_t caster;
	nd_get(caster_hd, &caster, &ent_ref);

	(void) dt;
	omp = caster.mp;
	return 0;
}

XY_IMPL(int, on_status, unsigned, ent_ref)
{
	caster_t caster;

	nd_get(caster_hd, &caster, &ent_ref);

	nd_printf(ent_ref, "Spell\tmp %5u, mmp %4u, com %4x, dem %4x\n",
		caster.mp, mp_max(ent_ref),
		caster.combo, caster.debuf_mask);

	return 0;
}

XY_IMPL(int, heal, unsigned, ref)
{
	caster_t caster;
	nd_get(caster_hd, &caster, &ref);

	debufs_end(&caster);
	caster.mp = mp_max(ref);

	nd_put(caster_hd, &ref, &caster);
	mcp_mp(ref);
	return 0;
}

static void
do_heal(int fd, int argc __attribute__((unused)), char *argv[])
{
	char *name = argv[1];
	unsigned player_ref = fd_player(fd), target_ref;
	caster_t caster_target;

	if (strcmp(name, "me")) {
		target_ref = ematch_near(player_ref, name);
	} else
		target_ref = player_ref;

	/* EF_WIZARD was deleted (ST.md §27.6(1), NO_WIZ.md). Nothing ever set
	 * it, so this gate was already unconditionally taken. */
	(void)name;
	(void)target_ref;
	(void)caster_target;
	nd_printf(player_ref, "You can't do that.\n");
	return;

	nd_get(caster_hd, &caster_target, &player_ref);
	heal(target_ref);

	{
		OBJ target;
		char buf[BUFSIZ];
		int len;

		nd_get(HD_OBJ, &target, &target_ref);
		nd_printf(player_ref, "You heal %s.\n", target.name);
		len = snprintf(buf, sizeof(buf), "is healed.\n");
		nd_rwrite(target.location, target_ref, buf, (size_t)len);
	}
}

XY_IMPL(int, on_vim, unsigned, ent_ref, sic_str_t, ss)
{
	char *opcs = ss.str + ss.pos;
	char *end;
	if (isdigit(*opcs)) {
		caster_t caster;
		unsigned combo = strtol(opcs, &end, 0);
		nd_get(caster_hd, &caster, &ent_ref);
		caster.combo = combo;
		nd_put(caster_hd, &ent_ref, &caster);
		nd_printf(ent_ref, "Set combo to 0x%x.\n", combo);
		return end - opcs;
	} else if (*opcs == 'c' && isdigit(opcs[1])) {
		unsigned slot = strtol(opcs + 1, &end, 0);
		OBJ player;
		nd_get(HD_OBJ, &player, &ent_ref);
		if (player.location == 0)
			nd_printf(ent_ref, "You may not cast spells in room 0.\n");
		else
			spell_cast(ent_ref, fighter_target(ent_ref), slot);
		return end - opcs;
	} else
		return 0;
}

XY_MODULE_API void
xy_install(void)
{
	/* The original mod_install took an arg and forwarded it to mod_open,
	 * which ignored it. xy_install takes none; the open sequence is inline,
	 * in the original order (WTS word first). */
	nd_put(HD_WTS, NULL, "heal");

	nd_len_reg("caster", sizeof(caster_t));
	caster_hd = (unsigned)nd_open("ent_spell", "u", "caster", 0);

	type_spell = (unsigned)nd_put(HD_TYPE, NULL, "spell");

	bcp_mp = (unsigned)nd_put(HD_BCP, NULL, "mp");

	nd_register("heal", do_heal, 0);
	nd_get(HD_RWTS, &wt_heal, "hit");

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