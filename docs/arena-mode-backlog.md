# Arena Mode Backlog

> See also: items 8-11 in `docs/backlog.md` for high-level arena roadmap items.

## 1. Squad Leader Net Analysis

1. Make test bench view for a squad leader net
   1. Show a squad net, and the fighters of the squad.
   2. Fighters are draggable
   3. visualize squad center
   4. can place enemy fighters and starbases
   5. can place home star base

## 2. Squad Leader Training Modes

1. Take an existing fighter net variant, and train for squad commands
   1. Score points for expanding/contracting correctly
   2. Score for moving towards target
   3. Score for firing on target

*Note: Fighter drill mode (implemented) partially addresses this — it trains fighters to follow scripted squad inputs. A complementary mode to train squad leaders against fixed fighters is still backlog.*

## 3. Arena Mode Info Panel

1. When in follow mode, display neural net viewer in right panel. Need new tab container to select whether fighter net is shown, or squad leader net. *(Partially implemented — ArenaGameScreen supports Fighter/SquadLeader toggle in follow mode.)*
