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

Binaries are compiled for `4.27`, and will work on C++ projects.

If you have a Blueprint project, you must upgrade to a C++ project, or else the game will fail to package.

If you are using a different version of Unreal Engine, you will need to recompile the plugin.

## Redistribution note

Our ladder movement code and sprinting speed logic is game specific and is not publicly redistributed at this time.

# Instructions

1. Paste the [PBCharacterMovement folder](https://github.com/ProjectBorealis/PBCharacterMovement/archive/master.zip) into your project's `Plugins/` folder.
2. Open your Unreal Engine project.
3. Add input action bindings for jump and crouch (Settings > Project Settings > Engine > Input). Add axis bindings for forward, right, look up and turn.
4. Create a new player controller in Blueprint or C++. Here's a [simple Blueprint example](https://blueprintue.com/blueprint/l7vxktwk/).
5. Create a Blueprint child class of PBPlayerCharacter.
6. Create a gamemode with Default Pawn set to your Blueprint character class, and Player Controller set to your player controller.
7. Enjoy the movement!

You may also want to use HL2 gravity settings. Go to Settings > Project Settings > Engine > Physics > Constants > Default Gravity Z and set it to `-1143`.

Additionally, your default physics material should have a friction of `0.8` and restitution of `0.25` if you want Source defaults.
