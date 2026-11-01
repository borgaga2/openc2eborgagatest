# Creatures 2 language support

New C2 creatures initialize their own 86-entry vocabulary from the installed
game's babble tables. `SAY#` and the current single-drive-word implementation
of `SAYN` use that creature's output string. An empty output stays silent.
Vocabulary restoration and capture preserve the input, output, and raw
strength of every entry without teaching additional words.

The C2 teaching computer queues message 7 for visible or audible recipients.
Delivery reads the computer's current vocabulary ID and text and ignores
sleeping or dead creatures. Repetition weakens a competing association, then
establishes and improves the new pronunciation using the installed game's
ordered substitution rules. Visual teaching uses C2 size permeability;
audible teaching checks axis ranges, horizontal wrap, room doors, and floor
separation.

Typing through the hand and pressing Return queues native C2 speech message 6
for creatures in earshot, including creatures already running an action script.
At delivery they read the hand's current words. Recognition uses each creature's
input and output vocabulary in entry order with ASCII case folding. Ordinary
commands stimulate the noun and verb lobes according to association strength;
they do not force a decision script. A verb paired with a different attention
category waits up to four following brain frames for attention to reach the noun.
Unknown words can teach the current attention category, and hand teaching echoes
the creature's current pronunciation. Learned encouragement and discouragement
words route to the corresponding genetic stimuli.

C2 `SAY#` now queues native message 6 for nearby creatures. At dispatch a
listener reads the sender's current **input** association for the numeric word
ID, while the visible bubble and voice use the sender's output pronunciation.
This distinction matches the original handler and lets creatures understand
different pronunciations. Message 7 from a creature teaches that association
directly, including the usual sleep/death checks. Changed associations echo the
learner's pronunciation; unchanged non-hand lessons echo on one in six draws.
These echoes do not recursively rebroadcast.

The learned action question (entry 76) produces a response describing the
creature's current decision. Action-to-word and optional-noun tables come from
the verified installed executable. The reply can include its self word and the
object of its attention, shares the corresponding vocabulary IDs with listeners,
and keeps the original 24-byte CP-1252 phrase limit.

The resource reader recognizes the verified Steam `Creatures2.exe` layout
through PE headers, code bytes, and table-address operands. It rejects unknown
or truncated layouts and the engine reports that speech assets are unavailable.
The executable and its text tables are user-provided assets and are not bundled
with the engine.

## Validation and remaining work

Unit tests cover resource validation, vocabulary round trips, gradual learning,
babble categories, range boundaries, floor boundaries, and door permeability.
An additional 128 synthetic-word observations from the original action-response
routine check text, shared vocabulary IDs, random draws, optional nouns, and
silent decisions. No original game strings are embedded in these observations.
With an original C2 installation, `c2playcheck` also checks newborn speech,
queued visual and audible lessons, changing the lesson before dispatch, and
sleeping recipients. The check also submits peer speech and teaching to a busy
listener, changes the sender's association before delivery, and requests an
action reply. Native-code observations were used to check the English
pronunciation examples and all 86 initial entries for three genera and three
random seeds.

This is partial language support. New-word learning currently accepts ASCII
words of at most 24 bytes containing a vowel/consonant transition. Locale-specific
case folding and other pronunciation edge cases need further comparison.
Need-expression phrases and native question/response commands 8 and 75 remain
incomplete. Audio-dependent message timing and the original world's complete
random stream are not restored; the action planner uses native draw rules with
seeds supplied by the engine's existing random generator. Hand input
currently recognizes ASCII case only. The original parser's destructive mutation
of a shared phrase buffer across recipients has not been replicated. Hearing at
corner crossings, fractional boundaries,
and unusual overlapping room layouts also needs further native comparison.
These changes do not establish complete C2 save/load support or full playability.
