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

The resource reader recognizes the verified Steam `Creatures2.exe` layout
through PE headers, code bytes, and table-address operands. It rejects unknown
or truncated layouts and the engine reports that speech assets are unavailable.
The executable and its text tables are user-provided assets and are not bundled
with the engine.

## Validation and remaining work

Unit tests cover resource validation, vocabulary round trips, gradual learning,
babble categories, range boundaries, floor boundaries, and door permeability.
With an original C2 installation, `c2playcheck` also checks newborn speech,
queued visual and audible lessons, changing the lesson before dispatch, and
sleeping recipients. Native-code observations were used to check the English
pronunciation examples and all 86 initial entries for three genera and three
random seeds.

This is partial language support. New-word learning currently accepts ASCII
words of at most 24 bytes containing a vowel/consonant transition. Locale-specific
case folding and other pronunciation edge cases need further comparison.
Full phrase construction, native question/response commands (entries 8, 75, and
76), and creature-to-creature language delivery remain incomplete. Hand input
currently recognizes ASCII case only. The original parser's destructive mutation
of a shared phrase buffer across recipients has not been replicated. Hearing at
corner crossings, fractional boundaries,
and unusual overlapping room layouts also needs further native comparison.
These changes do not establish complete C2 save/load support or full playability.
