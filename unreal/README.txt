VFX Forge for Unreal Engine
===========================

This folder was made by VFX Forge's "Export to Unreal". It brings effects
made in VFX Forge into an Unreal Engine 5 project, looking exactly as they
do in VFX Forge's window.

  Effects/<name>/            one folder per exported effect: its flipbook
                             (every frame of the effect, as VFX Forge draws
                             it, in one picture) and a description.
  VFXForge_Import.py         the script that turns them into Unreal assets.

To bring them in
----------------
1. In Unreal, make sure two plugins are on (Edit > Plugins): "Python Editor
   Script Plugin" and "Paper2D". Both are on in a new project; turning one
   on asks Unreal to restart.
2. Tools > Execute Python Script... (in older versions File > Execute
   Python Script...) and choose VFXForge_Import.py in this folder.
3. Every effect in Effects/ appears in the Content Browser under
   Content/VFXForge/<name>:

     BP_<name>            the effect, ready to use: drag it into a level.
                          It plays when the level starts; a one-shot effect
                          plays once, a looping one loops.
     FB_<name>            the Paper2D flipbook it plays.
     MI_<name>            its material (Glow brightens or dims it).
     T_<name>_Flipbook    the frames, and S_<name>_<n> one sprite per frame.

Run the script again after exporting again: the assets are replaced, and
levels that use them pick up the change.

To play an effect from a Blueprint, spawn BP_<name> (Spawn Actor from
Class), or add a Paper Flipbook component with FB_<name> and call Play
from Start on it.

What it is, and what it is not
------------------------------
Unreal gets the effect as VFX Forge drew it, frame by frame: the shapes,
colours, glow and timing are the same, and nothing has to be set up by
hand. It is not rebuilt from separate Niagara emitters, so its layers can't
be edited one by one in Unreal: change the effect in VFX Forge and export
again. It is a flat picture standing in the X-Z plane (Paper2D's), seen
from the front or the back, as VFX Forge's camera saw it when exported.

Colours: the material shows the frames' colours at full strength whatever
the scene's exposure (it undoes Unreal's eye adaptation). Unreal's
tonemapper still shapes the brightest colours, as it does everything else
in your game; to compare with VFX Forge exactly, look at it with the
viewport's Lit view and Game Settings' tonemapper as your game uses it.
