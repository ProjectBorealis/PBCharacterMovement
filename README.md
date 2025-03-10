# PBCharacterMovement

Project Borealis character movement component.

Includes all your standard classic FPS movement from HL2:

* Strafe bunnyhopping
* Accelerated back hopping (and forward and back hopping)
* Strafe boosting
* Circle strafing
* Surfing
* Ramp sliding/trimping/collision boosting
* Wall strafing
* Smooth crouching and uncrouching
* Crouch jumping
* Optional pogo jumping (automatic bunnyhopping): `move.Pogo` cvar
* Optional forward bunnyhopping: `move.Bunnyhopping` cvar

More info in this blog post: https://www.projectborealis.com/movement.

## Binaries

Binaries are compiled for `5.5`, and will work on C++ projects.

If you have a Blueprint project, you must upgrade to a C++ project, or else the game will fail to package.

If you are using a different version of Unreal Engine, you will need to recompile the plugin. Versions prior to `5.5` may require additional code changes. We are happy to accept PRs to improve the compatibility of the plugin.

# Instructions

1. [Download the PBCharacterMovement plugin](https://github.com/ProjectBorealis/PBCharacterMovement/archive/main.zip) and paste it into your project's `Plugins/` folder.
2. Open your Unreal Engine project.
3. Add Enhanced Input actions and mappings for forward, right, look up, turn, jump, and crouch. Setting these assets up properly is not covered here, but many tutorials for this exist online.
4. Create a new player controller in Blueprint or C++. Here's a [simple Blueprint example](https://blueprintue.com/blueprint/mhk2sgn9/).
5. Create a Blueprint child class of PBPlayerCharacter.
6. Create a gamemode with Default Pawn set to your Blueprint character class, and Player Controller set to your player controller.
7. Enjoy the movement!

## Gravity

You may also want to use HL2 gravity settings. Go to Settings > Project Settings > Engine > Physics > Constants > Default Gravity Z and set it to `-1143`.

The player movement will automatically adjust its own gravity scale to account for any differences between the gravity Z in your project and HL2, so
it's fine if you want a different gravity for your physics objects, and retain HL2 player gravity for fall speeds and jump heights. However, if you want
to fully use your own gravity instead of HL2 gravity, you can define `USE_HL2_GRAVITY=0`

## Physics materials

Additionally, your default physics material should have a friction of `0.8` and restitution of `0.25` if you want Source defaults.

You can put this in your `Config/DefaultEngine.ini` file to do so:

```ini
[SystemSettings]
p.DefaultCollisionFriction=0.8
p.DefaultCollisionRestitution=0.25
```

## Move Speeds and Ladder

Our ladder movement code and sprinting speed logic is game specific and is not publicly redistributed at this time. However, we do have stubs for you to insert your own logic here.

You can call `SetSprinting` and `SetWantsToWalk` for sprint and walk speed respectively. You can set `SprintSpeed`, `WalkSpeed` to set speeds for those modes, and set `RunSpeed` for the default speed.

Ladder movement code is a bit less complete due to reliance on ladder entity data to determine various aspects of the movement like direction and state. Integrating your own ladder code is outside
the scope of this document, and is left as an exercise for the reader.

## Jump Boosting

"Jump boosting", a feature of HL2 that causes the player to get a velocity boost when jumping. This also
causes the accelerated back hopping (ABH) bug which allows players to rapidly move around the map. You can control this feature using the `move.JumpBoost` cvar. `0` will entirely remove this feature, resulting in no velocity boost. `1` is HL2 functionality, with the ABH bug. `2` is HL2 functionality, patched to prevent such exploits. You may want to use `0` or `2` for a multiplayer game.

## Forward bunnyhopping 

An older version of HL2 had a bug where the jump boosting feature had no speed limit, and players could repeatedly jump to get successively speed boosts with no special movement like ABH requires. You can enable
the feature from the older engine of HL2 by setting `move.Bunnyhopping 1`.

## First person mesh support

Unreal Engine's character movement component assumes the character's mesh is a third person mesh that is grounded within the world. This affects how it is positioned when crouch hitboxes change, and some other effects.

Therefore, to assist in implementing first-person games and reduce confusion, we have provided a Mesh1P component you can use for FPS meshes for the player's perspective. You will need to implement your own
game-specific positioning system though, and respond to crouch eye height changes. We recommend to do this on camera update. If you need an example, please refer to the old `ShooterGame` example project from Epic. If you do not want or need this functionality, define `USE_FIRST_PERSON=0`.

## Always apply friction

HL2 movement uses friction to bring the player down to the speed cap. In air, this is not
applied by default. If you would like to limit the player's air speed, set `move.AlwaysApplyFriction 1`.

## Crouch sliding

Experimental support for crouch sliding is provided by setting `bShouldCrouchSlide` to `true`. However, some gameplay effects, like camera shakes, sounds, etc have been stripped due to dependencies on some internal gameplay systems. You can implement this for your own game if wanted.

## Directional braking

HL2 movement only applies braking friction in oppposition to the player's full movement. This may be too slippery when strafing or tapping keys for some games, these games can use directional braking which brakes each direction (forward/back and left/right) independently, allowing for each directional to be opposed by friction with full force. Enable this by defining `DIRECTIONAL_BRAKING=1`.
