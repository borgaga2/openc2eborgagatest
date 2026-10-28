# C2 saved creature bodies

`SkeletalCreature::restoreC2Body` restores the saved composite sprite slices,
attachment points, views, expression, eyes, body positions, planted foot,
pose table and gait table. It does not initialize a newborn or replay its genes.
The image resolver supplies the user's saved composite S16 gallery. Original
game assets and saved-world files are not included in the repository.

The runtime body order differs from the archive's six linked limb chains.
The chains are head; left thigh/shin/foot; right thigh/shin/foot; left upper
arm/forearm; right upper arm/forearm; tail root/tip. A missing entire tail is
valid: one tested original creature has a 230-frame composite and twelve
body parts, while the other five have 250 frames and fourteen parts. Missing
required limbs and incomplete tail chains are rejected before replacement.

The archive foot flag is **0 for left, 1 for right**. This was checked against
both feet's world attachment coordinates in an original populated save and
its backup, covering twelve creature snapshots. The selected foot matches
the separately saved foot coordinate in all twelve; interpreting the flag
as a left-foot boolean does not. The C2 field is now named `downfoot_right`;
its serialized representation is unchanged. C1's separate format is unchanged.

Head angle `5` selects C2 view 9. Actual saved head frames include the eye
and expression offsets; restoring the pose must not drop this angle. The
visible eye state comes from the saved head frame: a tested backup has a
closed-eye frame despite `eyes_open` being 1, consistent with a blink.
Rebuilding the frame from that flag alone changes the saved appearance.
The composite body contains only its ten current views, so pregnancy offsets
must not index beyond that slice. If a later shape change rebuilds the body,
appearance genes are selected through the current stage without replaying
biochemistry or replacing the saved animation tables. Full C2 pregnancy,
growth and pigmentation fidelity still require separate verification.

## Checks and remaining integration

`C2SkeletonStateTest` checks slice mapping, both feet, all directions, signed
body attachments, saved animation tables, optional tails, and rejection of
invalid replacements and overflowing coordinates. `c2playcheck` loads the
composites next to the selected save (or in its Images directory), then checks
every rendered part's pixel-buffer identity, frame and world coordinates
against the archive. It repeats this check after recalculating the saved pose
and verifies the planted-foot coordinates. Both original saves pass for all
six creatures, including the creature without a tail.

This remains a component restoration API. The normal saved-world loader still
needs to construct and register saved creatures, bind references and resume
running scripts. Restoring a body is not evidence that the saved world is
playable. In particular, this API resets the runtime gait cursor; continuation
of an in-progress animation is not yet restored. Do not call the ordinary
`SkeletalCreature::finishInit` after restoration: it replaces saved visuals
and animation tables with the newborn setup.
