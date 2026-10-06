## 1.0.7

- **`do_heal`'s dead `EF_WIZARD` gate is gone, with the behaviour it already
  had.** Nothing ever set `EF_WIZARD` — the flag was deleted with the wizard
  concept (ST.md §27.6(1), NO_WIZ.md) — so the condition was already
  unconditionally taken and every `heal` was already refused; the refusal is
  now stated explicitly instead of being read off a flag nobody sets.
  `on_will_attack` also zero-initializes `hit_t last`, `hit` and
  `element_t element`, which were declared without an initializer.
- **The sibling `-I` lines are gone.** `CFLAGS += -I$(shell cd .. && pwd)/…`
  pointed at the axil-nd/nd sibling checkouts and only existed for a dev
  build: in CI those directories do not exist and every header comes from the
  installed packages named in `.github/workflows/ci.yml`. The build now
  resolves `<nd/…>` the way a packager sees it.
- **macOS: link with `-undefined dynamic_lookup`.** macOS `ld` rejects
  undefined symbols in a shared library, but `WARN` needs `qsyslog` — an
  engine-provided function pointer resolved at `dlopen` time (Linux allows
  this by default). `-undefined dynamic_lookup` is the Darwin equivalent, set
  as `LDFLAGS-libnd-spell-Darwin` so no other platform is affected.

## [1.0.0]

- **nd-spell is now an installable library rather than a build artifact of
  the engine.** It builds and installs exactly one file,
  `lib/libnd-spell.so`, following the same layout as `axil-tty` and
  `axil-auth`, and the same layout `nd-core` was converted to first.
  Previously `make` produced a `spell.so` named by the engine's `mods.load`
  and installed nothing. There is no `lib/nd-spell.so` symlink: `mods.load`
  names this module `libnd-spell`, the installed filename, and
  `module_load_path()` only appends `.so`. It installs no header because it
  exports no API — every symbol it defines is discovered by the engine, not
  called by another module.

- **The link line is libxylem plus libm.** `LDLIBS := -lxylem -lm`: `G()`
  resolves to `sqrt()`. `NEEDED` is `libxylem.so`, `libm.so.6` and
  `libc.so.6`.

- **Two chains are both implemented and called here.** `effect` (active
  debuffs over nd-attr's chain value) goes through this module's own
  registered adapter via the `effect_chain` helper; `on_will_attack` layers
  strikes over nd-fight's martial hit, passing it through unchanged with no
  target. This TU defines `ATTR_IMPL`, `FIGHT_IMPL` and `MORTAL_IMPL`
  because it `XY_IMPL`s names from all three providers' headers.

- **`call_verb`/`call_verb_to` never existed** and are gone from the port;
  cast messages go out as `nd_printf` + `nd_rwrite` via `OBJ.location`.
  A local shadowing the `mp_max` hook in its own initializer is now `mmax`.

- **The `ent_spell` table stores full structs.** Value kind `caster` is
  registered with `nd_len_reg`.

- **Dropped the `nd-mod.mk` dependency.** `nd-mod.mk` has now been deleted.
