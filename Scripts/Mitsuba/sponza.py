import mitsuba as mi

mi.set_variant('scalar_rgb')

mi.set_log_level(mi.LogLevel.Info)

img = mi.render(mi.load_dict(
  {
    "type": "scene",
    "integrator": {"type": "path"},

    # Simple camera setup
    "sensor": {
        "type": "perspective",
        "to_world": mi.ScalarTransform4f.look_at(
            origin=[8.28, 4.866, -0.685],
            target=[7.302, 4.664, -0.645],
            up=[0, 1, 0]
        ),
        "fov": 65,
        "film": {"type": "hdrfilm", "width": 1920, "height": 1080},
        "sampler": {"type": "independent", "sample_count": 128},
    },

    # Directional light with specified illuminance
    "emitter": {
        "type": "directional",
        "irradiance": {"type": "rgb", "value": [20.0, 20.0, 20.0]},
        # Orientation of the light — from (0, 1, 1) toward scene center
        "to_world": mi.ScalarTransform4f.look_at(
            origin=[0, 0, 0],
            target=[-0.380, -1.0,  0.180],
            up=[0, 1, 0]
        )
    },

    # Load Sponza geometry (replace with your converted OBJ)
    "shape": {
        "type": "obj",
        "filename": "Assets/Source/sponza/sponza_mitsuba.obj",
    },
  }
))


mi.Bitmap(img).write('References/sponza.exr')
