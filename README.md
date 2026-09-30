# axil-nd-spell

`nd-spell` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Owns spells: the `ent_spell` table (one `caster_t` per entity), casting,
spell strikes, healing, and the active-debuff path. It co-implements
nd-attr's `effect` chain (debuffs on top of the base value, through its own
`effect_chain` helper) and nd-fight's `on_will_attack` chain (spell strikes
on top of the martial hit), both read through `nd_last()`.

## Install

```sh
make install
```

Installs one file:

```
lib/libnd-spell.so
```

There is deliberately no `lib/nd-spell.so` symlink (see `axil-nd-wts` for
why: `mods.load` names the installed filename, and the OpenBSD packing list
never lists a symlink).

Also packaged for deb, apk, rpm, brew and openbsd from a `v*` tag.

It installs no header because it exports no API — every symbol it defines is
discovered by the engine, not called by another module.

## Build from source

```sh
make
```

Needs [libxylem](https://github.com/tty-pt/libxylem) and the engine's game
API, `<nd/xy.h>`, plus the sibling headers it builds against (`<nd/attr.h>`,
`<nd/fight.h>`, `<nd/mortal.h>`, `<nd/seat.h>`) — from checkouts beside this
repo or from installed packages:

```sh
git clone https://github.com/tty-pt/nd-spell && cd nd-spell
git clone https://github.com/tty-pt/axil-nd ../axil-nd
git clone https://github.com/tty-pt/nd-attr ../axil-nd-attr
# ... and likewise nd-fight, nd-mortal, nd-seat
make
```

Both the checkout `-I` flags and the installed-package paths are on the
command line at once (see `Makefile`), and a missing `-I` is ignored, so the
same command works either way. CI names the deps explicitly
(`axil-nd,libxylem,nd-attr,nd-fight,nd-mortal,nd-seat,nd-equip`).

## What it does

* `xy_install()` registers the `ent_spell` table (value kind `caster`) and
  the spell skeletons.
* `effect` layers active debuffs over the chain value; `on_will_attack`
  layers spell strikes over the martial hit (passing it through unchanged
  with no target); `heal` and the `on_mortal_life` / `on_mortal_survival` /
  `on_death` bodies cover recovery and killing; `on_add`, `on_status` and
  `on_vim` cover creation, display and targeting.

## Testing

There is no `test.sh` here. Behaviour is asserted by the engine's own suite:

```sh
cd ../axil-nd
make && ./test.sh
```

## Notes from the port

* `SIC_DEF` → `XY_IMPL`, `mod_install` → `xy_install`, `call_f(...)` →
  `f(...)`. `call_verb`/`call_verb_to` never existed; cast messages go out
  as `nd_printf` + `nd_rwrite` via `OBJ.location`. `mod_open` took an arg
  that `xy_install` drops.
* Because this TU `XY_IMPL`s names from three providers' headers, it defines
  `ATTR_IMPL`, `FIGHT_IMPL` and `MORTAL_IMPL` before including them. `effect`
  is both implemented and called here, so it goes through this module's own
  registered adapter via the `effect_chain` helper.
* A local `long mp_max = mp_max(ent_ref)` shadowed the hook in its own
  initializer ("called object is not a function"); the local is now `mmax`.
* `G()` → `sqrt()`, so this module links `-lm` alongside libxylem.
  `NEEDED` is `libxylem.so`, `libm.so.6` and `libc.so.6`.

## License

BSD 2-Clause, carried over from `tty-pt/nd-spell`. See `LICENSE`.
