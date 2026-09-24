# Prepared-stem reader dependencies

The reader implements the data format observed on the user's Overcue USB. It
does not contain Overcue firmware or private Overcue source.

- miniz 3.0.2, commit `293d4db1b7d0ffee9756d035b9ac6f7431ef8492`,
  https://github.com/richgel999/miniz, MIT. Only the inflate implementation is
  compiled. `miniz_export.h` defines an empty export annotation for static use.
- jsmn, commit `25647e692c7906b96ffd2b05ca54c097948e879c`,
  https://github.com/zserge/jsmn, MIT. Used to parse bounded USB metadata.
- Brad Conte's SHA-256, commit `cfbde48414baacf51fc7c74f275190881f037d32`,
  https://github.com/B-Con/crypto-algorithms, public domain. Used for integrity
  checks. The input-byte shifts explicitly cast to unsigned WORD to avoid
  undefined signed overflow detected by UBSan.

Each dependency's original notice is retained beside its source and copied into
the packaged mod bundle. The format and timing evidence is recorded in the
Library's `docs/research/2026-09-21-overcue-stems.md`.
