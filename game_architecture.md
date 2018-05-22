# Main game principles

**no difficulty level**

Game is meant to be challenging, but rewarding. It'll be possible to survive missions with no hits whatsoever if
player is skilled enough.

**One-hit killers will be fair**

Player will be warned in special way about 1-hit danger few seconds before it happens to get chance to react.
It'll be possible to reduce the noise from the notifications in game when desired.

**Save only at the end of level**

Since levels will be short / mid-length it'll be pointless to offer any save option. Failure state will also be more plainful this
way as well.

# Mech movement mechanics

**SHIFT** - use "booster" movement scheme.

Similar to how other games implement sprint this will force the boosters to be used.
This will drain the propeller supply which at the end will disable any booster usage.

**WASD** - first person mode movement scheme.

"a" and "d" strafe while "w" moves forward and "s" moves backward.

**SPACEBAR** - boost up.

Allows to spread aerodynamic breaks and traverse through the air for the short period of time

**1/2/3** - switch between weapons

1 - primary weapon (sniper rifle, heavy machine gun, granade launcher, etc)
2 - secondary (smg, sword, etc)
3 - knife

Pressing the button once will trigger de-equiping of the weapon first (put the weapon into holster)
and then equiping the new one.
De-equiping will be time consuming operation (2-5 seconds) depending on the weapon.

**t** - throw currently equiped weapon away

Player will have an option of throwing the currently equiped weapon away to skip this timer and immediately equip next weapon.

**e** - interaction button

**p** - power

Turning off power will be a valid strategy to avoid detection from enemies. It'll work great against weak-sensing waves
(to circle around a wave for example)

# Mech resource mechanics

**booster jet fuel**

depending on robots weight it'll drain at different rate. Walking does not affect it.

**battery life**

when it drops to 0 whole robot stops moving and pilot has to eject and run away

**ammo**

depending on mech size might be very limited

**overheating**

shotting continously will increase gun temperature up critical value is reached

**maintenence**

each shot / usage will drain this resource. After it is drained, the gun breaks and has to be thrown away.

# Mech types

- old type, not very maneuverable, single weapon not tossable, knife, keeps partially momentum when changing direction
- training type, maneuverable, 2 weapons, knife, low ammo/energy/propeller storages
- modern type, same as training one but with good storages

additionally modern types can specialize in some functions:

- light: extremly agile for escapes, air fights, good with hand-to-hand combat.
- standard: balanced
- heavy: extra armor, shield, limited jumping

# Enemy types

**small laser class**

slow movement, hitscan with fast tracking, avoids friendly fire, only encounterable on the surface / big spaces,
can be ambushed from behind if not spotted, depending on armor mech can withstand few seconds of fire on certain armor parts

**big laser class**

very slow movement, hitscan with slow tracking, once starts shotting it keeps the beam orientation static for shot duration,
anything that gets into line of fire is destroyed by default, very bad at sensing and easily outmaneuvered

**brute**

normal speed, 1-hit kills with momentum, very durable armor, killable only from behind with explosive rounds,
killing through armor requires penetration ammo

**grappler**

fast, graps onto the armor, once pinned to player only killable with a knife, easily killed with explosive rounds

**walking fortress**

Same thing as in gif. Very slow movement, one hit kills with the needle which is a projectile,
though armor which negates explosive rounds, main body can be hit by penetrating rounds, swords, knifes,
red armor parts can't be damaged, can spawn other enemies

# Level objectives

**Defeat wave attack**

Enemy waves can emarge from ground if the fight takes place near the hive.

**Defend the objective**

Defend bigger and slower ally target. Similar to escort missions.

**Escape**

Move to the end of level. Good coordination and mapping is required

**Secure area**

Kill enemies in fixed radius from point

**Evacuate area**

Find trapped alive pilots and take them to safe zone

**Resupply area**

Toss everything you have, take all the ammo you can carry and take it to some location

**hive infiltration**

Detonate the core

# Level architecture principles

**10-20 minutes long at max**

It should be digestable by an adult.

**Story should be skipable**

If someone just wants to play for the nice gameplay, he should be able to as well.

# Level locations / environments

- virtual simulation
- surface, beach invasion
- surface, shore, trenches and fortifications defence
- surface, midland escapes
- underground base defence from point to point
- hive infiltration down to the core (both simulated and real) from orbital dive

# Level goals

Each level will have a "main goal" like:

- set detonation charges in specific places
- mark artillery hit spots
- scout behind enemy and come back

Player won't be able to resupply during the mission and provided resources won't allow for level full clears.
Pacing and economy will be most important.

# Story

TBD

# Implementation plan

1. Virtual simulation environment scene, basic mech mechanics, tutorial for movement, first attempts at random maze generation
2. Next mech (fast one?) and beach invision level. Single enemy type
3. More advanced mechanics, rafininng level arenas, work on layouts etc (prettying the game)
4. Next mech (strong one?), shore defence
5. next two levels (midland escape), underground base defence
6. Hive infiltration
