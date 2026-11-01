# Experimental C2 world reconstruction

`c2worldcheck` reconstructs an original C2 `.sfc` archive in an empty engine,
checks the paused result, and optionally runs a bounded simulation. This is a
development tool for implementing ordinary saved-game loading. The normal
player startup path still uses the existing loader; no File/Open or File/Save
workflow is provided by this work.

Build the `c2worldcheck` target, then run:

```text
c2worldcheck C2_INSTALL SAVED_WORLD GENETICS_DIRECTORY ISOLATED_USER_DIRECTORY [RESUME_TICKS]
```

Use copies or read-only inputs for the original world and genetics. The tool
uses the null renderer, silent audio, and a separate user-data directory. It
does not write the input archive. The optional tick count is limited to 10,000.

## Restored state

- Rooms, floors, doors, environmental values, camera position and selection.
- Saved world tick and the map's independent calendar fields (time of day,
  day within the year and year), preserving the next calendar transition.
- Ordinary objects, call buttons and lifts, variables, object pointers, carried
  objects, saved timer countdowns and sprite animation positions.
- Existing creature identities, composite body sprites, poses, vocabulary,
  brain state, biochemistry, organ state, instincts, age and physiological loci.
- Speech bubble text and lifetime.
- Supported suspended macros at their saved command boundary, including WAIT,
  GSUB, LOOP and REPS state. All macros are prepared before any are attached.

Reconstruction stays paused and suppresses new-creature initialization and
room-entry events. Resuming a saved script does not run it again from its start.

## Validation and limitations

Before creating live rooms or agents, the loader checks object-reference
membership, identity collisions, selection, carrier cycles, overlapping held
objects, macro references and duplicate macro owners. Call-button-owned lift
records are included even when absent from the top-level object list.

Creature genomes and composite images are resolved and cached before live-world
construction. Missing dependencies and cyclic or shared limb records therefore
fail before changing the world. This does not provide complete rollback: errors
during later construction or macro compilation can still leave partial state.

Complete native history, pregnancy and genetic lifecycle references, remaining
opaque world state, all macro execution modes, editable speech bubbles and
runtime snapshot capture still require work. Passing this checker is not proof
that a world is fully playable or that a normal save/load cycle works.

The local current and backup Creaturtopolis archives have each run for 3,600
ticks with all six saved creatures alive and no reported agent errors. Original
archives, sprites, genomes and executable data are not included in this repository.
