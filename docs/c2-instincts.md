# Creatures 2 instinct behavior

These notes record the original executable evidence used for the C2 instinct
changes. The inspected Steam Albian Years `creatures2.exe` has SHA-256
`b1b5b77579193f93241dcd930d10c90dd4c715f4e620735ad7a87e98990576a4`.
Addresses below are virtual addresses in that executable, not portable symbols.

* `41e6a0` and the inlined path at `47a085` call `41ab20`, the direct wiring
  routine. They remove the queued instinct only when that routine returns true.
  After removal, an older creature decrements its requested dreaming count;
  a baby keeps the count until its queue is empty.
* `41a8a0` contains a separate phased training routine. The inspected dreaming
  callers do not use it. Its existence does not justify inserting a delay into
  the direct wiring path. The archive still preserves each instinct's phase.
* `41a4f0` expresses lobe IDs in the range 0–7 and actions in 0–15 using masks,
  and folds cell indices modulo the source lobe's cell count.
* `41ab20` redirects a non-perceptible source to the first same-sized lobe with
  a matching input whose maximum connection count is nonzero. That relay search
  does not independently require its perceptibility flag to be set.
* `414090` selects the weakest eligible concept neuron with exactly the needed
  connection count. `414260` selects the weakest decision connection, using its
  inhibitory type only for chemical 53. Both reset the connection's home
  coordinates and susceptibility. Missing decision connections return false;
  an unusable concept request is consumed rather than retried indefinitely.
* `41e5d0` skips normal biochemistry while the dreaming count is nonzero.

`C2InstinctTest.cpp` checks relay selection, connection reset, expressed values,
retry behavior, baby/adult limits and chemistry suspension. `c2playcheck` also
exercises the newborn queue using installed C2 assets. This does not implement
complete saved-world creature loading or prove all brain/lifecycle behavior.
