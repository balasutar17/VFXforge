# VFX Forge: brings the effects in Effects/ into this Unreal project.
#
# Run it from Unreal: Tools > Execute Python Script... For each effect it
# makes, under /Game/VFXForge/<name>/:
#   T_<name>_Flipbook   the frames, as VFX Forge drew them, in one picture
#   S_<name>_<n>        one Paper2D sprite per frame
#   FB_<name>           the Paper2D flipbook that plays them
#   MI_<name>           the material they are drawn with
#   BP_<name>           the effect, ready to drag into a level
# and once, /Game/VFXForge/M_VFXForge_Flipbook, the material they share.
# Running it again replaces them.

import json
import os
import re

import unreal

ROOT = "/Game/VFXForge"
HERE = os.path.dirname(os.path.abspath(__file__))
UNITS_PER_VFX_UNIT = 100.0  # one VFX Forge unit is a metre

tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
materials = unreal.MaterialEditingLibrary


def log(text):
    unreal.log("VFX Forge: " + text)


def found(path, asset_class, factory):
    """The asset at path, made if it is not there yet. An asset that is
    already there is updated in place, so levels that use it keep it."""
    if assets.does_asset_exist(path):
        existing = unreal.load_asset(path)
        if isinstance(existing, asset_class):
            return existing
        assets.delete_asset(path)
    folder, name = path.rsplit("/", 1)
    return tools.create_asset(name, folder, asset_class, factory)


def try_set(thing, name, value, call=None):
    """Sets a property, or calls the setter Unreal offers instead."""
    try:
        thing.set_editor_property(name, value)
        return True
    except Exception:
        pass
    if call is not None:
        try:
            getattr(thing, call)(value)
            return True
        except Exception as error:
            unreal.log_warning("VFX Forge: could not set %s: %s" % (name, error))
    return False


def import_texture(png, folder, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(folder + "/" + name)
    # The frames as they are: no compression, no mipmaps (they would
    # blur one frame into the next), never streamed out.
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    assets.save_loaded_asset(texture)
    return texture


def master_material():
    """Unlit, see-through, two-sided: the frame's colour times its cover,
    laid over the scene by that cover, at the same strength whatever the
    scene's exposure. Paper2D hands the frame in as SpriteTexture."""
    path = ROOT + "/M_VFXForge_Flipbook"
    material = found(path, unreal.Material, unreal.MaterialFactoryNew())
    materials.delete_all_material_expressions(material)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    material.set_editor_property("two_sided", True)

    frame = materials.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    frame.set_editor_property("parameter_name", "SpriteTexture")
    tint = materials.create_material_expression(material, unreal.MaterialExpressionVertexColor, -900, 300)
    glow = materials.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, 500)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 1.0)
    exposure = None
    if hasattr(unreal, "MaterialExpressionEyeAdaptationInverse"):
        exposure = materials.create_material_expression(material, unreal.MaterialExpressionEyeAdaptationInverse,
                                                        -900, 650)

    # Cover: the frame's alpha, faded by the sprite's own alpha.
    cover = materials.create_material_expression(material, unreal.MaterialExpressionMultiply, -600, 250)
    materials.connect_material_expressions(frame, "A", cover, "A")
    materials.connect_material_expressions(tint, "A", cover, "B")

    # Colour: the frame's, tinted, times its cover (premultiplied), times Glow,
    # with the scene's exposure undone.
    tinted = materials.create_material_expression(material, unreal.MaterialExpressionMultiply, -600, 0)
    materials.connect_material_expressions(frame, "RGB", tinted, "A")
    materials.connect_material_expressions(tint, "RGB", tinted, "B")
    covered = materials.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 0)
    materials.connect_material_expressions(tinted, "", covered, "A")
    materials.connect_material_expressions(cover, "", covered, "B")
    glowing = materials.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 0)
    materials.connect_material_expressions(covered, "", glowing, "A")
    materials.connect_material_expressions(glow, "", glowing, "B")
    shown = glowing
    if exposure is not None:
        shown = materials.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 0)
        materials.connect_material_expressions(glowing, "", shown, "A")
        materials.connect_material_expressions(exposure, "", shown, "B")

    materials.connect_material_property(shown, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    materials.connect_material_property(cover, "", unreal.MaterialProperty.MP_OPACITY)
    materials.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def make_effect(folder_on_disk, base):
    with open(os.path.join(folder_on_disk, base + ".json"), encoding="utf-8") as f:
        d = json.load(f)
    # Unreal names allow letters, digits and underscores only.
    stem = re.sub(r"[^A-Za-z0-9_]+", "_", d["stem"]).strip("_") or "Effect"
    folder = ROOT + "/" + stem
    frames = int(d["frames"])
    columns = int(d["columns"])
    width = int(d["frameWidth"])
    height = int(d["frameHeight"])
    fps = float(d["framesPerSecond"])
    loops = bool(d["loop"])

    texture = import_texture(os.path.join(folder_on_disk, d["flipbook"]), folder, "T_" + stem + "_Flipbook")

    instance = found(folder + "/MI_" + stem, unreal.MaterialInstanceConstant,
                     unreal.MaterialInstanceConstantFactoryNew())
    materials.set_material_instance_parent(instance, unreal.load_asset(ROOT + "/M_VFXForge_Flipbook"))
    materials.set_material_instance_scalar_parameter_value(instance, "Glow", 1.0)
    assets.save_loaded_asset(instance)

    # Where VFX Forge's centre falls in a frame, in pixels from its corner.
    pixels_per_unit = height / float(d["unitsHigh"])
    pivot_x = width * 0.5 - float(d["centerX"]) * pixels_per_unit
    pivot_y = height * 0.5 + float(d["centerY"]) * pixels_per_unit

    sprites = []
    for k in range(frames):
        sprite = found("%s/S_%s_%03d" % (folder, stem, k), unreal.PaperSprite, unreal.PaperSpriteFactory())
        left, top = (k % columns) * width, (k // columns) * height
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(left, top))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(width, height))
        sprite.set_editor_property("pixels_per_unreal_unit", pixels_per_unit / UNITS_PER_VFX_UNIT)
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CUSTOM)
        sprite.set_editor_property("custom_pivot_point", unreal.Vector2D(left + pivot_x, top + pivot_y))
        sprite.set_editor_property("sprite_collision_domain", unreal.SpriteCollisionMode.NONE)
        sprite.set_editor_property("default_material", instance)
        assets.save_loaded_asset(sprite)
        sprites.append(sprite)

    flipbook = found(folder + "/FB_" + stem, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())
    keys = []
    for sprite in sprites:
        key = unreal.PaperFlipbookKeyFrame()
        key.set_editor_property("sprite", sprite)
        key.set_editor_property("frame_run", 1)
        keys.append(key)
    flipbook.set_editor_property("key_frames", keys)
    flipbook.set_editor_property("frames_per_second", fps)
    flipbook.set_editor_property("default_material", instance)
    assets.save_loaded_asset(flipbook)

    # Sprites left from a longer version of the effect.
    k = frames
    while assets.does_asset_exist("%s/S_%s_%03d" % (folder, stem, k)):
        assets.delete_asset("%s/S_%s_%03d" % (folder, stem, k))
        k += 1

    # The effect as an actor: a flipbook that plays when it appears.
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    blueprint = found(folder + "/BP_" + stem, unreal.Blueprint, factory)
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    handles = subobjects.k2_gather_subobject_data_for_blueprint(blueprint)
    component = None
    for handle in handles:
        thing = library.get_object(library.get_data(handle))
        if isinstance(thing, unreal.PaperFlipbookComponent):
            component = thing
    if component is None:
        params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.PaperFlipbookComponent,
                                              blueprint_context=blueprint)
        handle, why = subobjects.add_new_subobject(params)
        if library.is_handle_valid(handle):
            subobjects.rename_subobject(handle, unreal.Text("Effect"))
            component = library.get_object(library.get_data(handle))
        else:
            unreal.log_warning("VFX Forge: could not add the flipbook to BP_%s: %s" % (stem, why))
    if component is not None:
        try_set(component, "source_flipbook", flipbook, "set_flipbook")
        try_set(component, "looping", loops, "set_looping")
        try_set(component, "override_materials", [instance])
        try_set(component, "cast_shadow", False, "set_cast_shadow")
        try:
            component.set_collision_profile_name("NoCollision")
        except Exception:
            pass
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assets.save_loaded_asset(blueprint)
    log("%s: %d frames at %g a second, %s" % (stem, frames, fps, "looping" if loops else "once"))


def main():
    effects = os.path.join(HERE, "Effects")
    found_any = False
    with unreal.ScopedSlowTask(1, "VFX Forge: importing effects") as task:
        task.make_dialog(True)
        master_material()
        for name in sorted(os.listdir(effects)) if os.path.isdir(effects) else []:
            folder = os.path.join(effects, name)
            if not os.path.isfile(os.path.join(folder, name + ".json")):
                continue
            found_any = True
            make_effect(folder, name)
    if not found_any:
        unreal.log_warning("VFX Forge: no effects found in " + effects)
    else:
        log("done. The effects are in Content/VFXForge.")


main()
