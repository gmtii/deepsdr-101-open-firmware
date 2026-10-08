Thanks for the report — and for picking the one example that shows it cleanly.

**The configuration is not being ignored. It is read correctly, applied
correctly, and then overwritten a moment later by the theme.**

Here is the exact boot sequence that produced what you saw:

```
1. settings_load() reads CONFIG.CSV
     spectrum_palette,TURBO  ->  spectrum_set_palette(TURBO)   <- applied right here
2. main()'s apply block calls tema_aplicar(OLIVE)
     k_temas[OLIVE].wf = SPECTRUM_PALETTE_SMOKE
                         ->  spectrum_set_palette(SMOKE)       <- clobbered
```

Every theme *proposes* a waterfall palette. "Olive" carries SMOKE because it is
one of the two light themes and SMOKE is the only light scale there is (white
at the bottom, black at the top). With a dark theme and a dark palette the
clobber is invisible; with Olive + Turbo it is obvious. Your example is the one
that makes it visible, which is why this had gone unnoticed.

### What actually failed

In `settings.c`, directly above the offending line, there was this:

> *"spectrum_palette (08/09/2026): applied DIRECTLY here ... see this file's
> build_csv() comment for why **there is no ordering hazard**."*

**That comment was true on 08/09/2026.** Nothing in the boot sequence touched
the waterfall palette after the file was read, so applying it during the read
was legitimate and reasoned.

It stopped being true on **23/09/2026**, when themes gained their own waterfall
palette and `tema_aplicar()` started calling `spectrum_set_palette()`. From
that day the comment was asserting there was no hazard exactly where one had
just appeared — and nobody re-read it, because a comment that says something is
safe is precisely what stops people looking again.

### The fix

The narrow fix would be to make the theme skip its palette when the file
carries one. That fixes your case and leaves the whole class of bug standing.

So instead: **`spectrum_palette` is no longer applied during the file read.**
It now gets its own `have_`/value pair like every other order-dependent
setting, and `main.c` applies it where you can see in one glance that it comes
after the theme:

```c
    if (s_loaded_settings.have_tema_idx) {
        tema_aplicar(s_loaded_settings.tema_idx);
    }
    if (s_loaded_settings.have_spectrum_palette) {
        spectrum_set_palette((spectrum_palette_t)s_loaded_settings.spectrum_palette);
    }
```

The ordering is no longer spread across two files and a comment; it is two
consecutive lines that are read together.

**The theme still proposes.** Picking a theme by hand still sets its waterfall
palette — that is the documented behaviour and it stays. What changed is that
**at boot, what you saved wins.**

### So it cannot come back

`tools/orden_arranque.py`, inside `make comprueba`, checks both halves:

1. `settings.c` does **not** call `spectrum_set_palette()` (and does store
   `have_spectrum_palette`).
2. In `main.c`, the line applying the palette appears **after**
   `tema_aplicar()`.

It has been verified to fail with the bug put back, in **both** of its forms —
palette re-applied during the read, and the two `main.c` lines in the opposite
order. Both come out red.

This bug did not arrive with a bad change; it arrived with time, when a true
comment quietly became false. A comment cannot check anything. That script can,
and it will keep looking when the next change turns something else that is true
today into a lie.

Fixed in **V3.91**.
