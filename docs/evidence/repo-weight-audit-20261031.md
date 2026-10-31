# Tracked spike-asset weight audit (2026-10-31)

Companion to [`repo-weight-audit-20261031.tsv`](repo-weight-audit-20261031.tsv).
The TSV is generated with `tools/audit_tracked_artifacts.py` from the
superproject; its `sha256` column is the stable identity of each file.

## Observed

| Metric | Value |
|---|---:|
| Tracked artifacts at or above 1 MiB | 32 |
| Total size of those artifacts | 75.6 MiB |
| Media | 32 files, 75.6 MiB |
| No document reference found | 12 files, 22.3 MiB |
| Referenced by a tracked document | 20 files, 53.3 MiB |
| Tracked `render/spike` total | 110.4 MiB |

The largest assets are the 9.3 MiB Apollo still, the 7.7 MiB
`Avocado.glb`, the 7.3 MiB `UAL1_Standard.glb`, and the 5.3 MiB
`mrbeast_reference.mp4`. These are **render inputs**, not generated evidence:
most of the referenced files are loaded by the spike programs.

## Policy

Spike media belongs in an external asset pack with a fetch script; git keeps
this manifest, the READMEs and the small fixtures needed by the core build.
`render/spike/assets` is optional (`-DCHRONONMOTION_BUILD_SPIKE_PROGRAMS=ON`),
so the pack can be fetched only by the spike lane.

`render/spike/**/*.mp4` is already ignored, with an explicit exception for
`mrbeast_reference.mp4` because it is source footage. The tracked 110.4 MiB
therefore comes from images, GLB and one source video that predate or override
that rule.

## Staged move (not executed)

1. Upload the assets to the external pack under their `sha256`.
2. Add a fetch script that materialises `render/spike/assets` from the pack.
3. Update the spike READMEs and any JSON that names the files.
4. `git rm --cached` the moved assets, keeping the local copy until the fetch
   path and the spike programs are verified.
5. Commit the removal; the history rewrite is a separate, coordinated
   force-push and is not part of this pass.

Removing the files from `HEAD` does not reduce clone size while the blobs stay
in history. The pack plus fetch script is the prerequisite; this audit does
not break the spike lane.
