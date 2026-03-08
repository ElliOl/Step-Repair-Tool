{
  "targets": [
    {
      "target_name": "step_fixer_native",
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "cflags_cc": [
        "-std=c++17",
        "-O3",
        "-Wall",
        "-Wextra",
        "-frtti"
      ],
      "sources": [
        "src/addon.cpp",
        "src/step_viewer.cpp",
        "src/name_repair.cpp",
        "src/shell_split.cpp",
        "src/hoops_compat.cpp",
        "src/step_text_patch.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<!(echo $HOME)/Libraries/opencascade/7.8.1/include/opencascade"
      ],
      "libraries": [
        "-L<!(echo $HOME)/Libraries/opencascade/7.8.1/lib",
        "-lTKDESTEP",
        "-lTKXSBase",
        "-lTKCAF",
        "-lTKLCAF",
        "-lTKXCAF",
        "-lTKVCAF",
        "-lTKBRep",
        "-lTKMesh",
        "-lTKTopAlgo",
        "-lTKGeomBase",
        "-lTKGeomAlgo",
        "-lTKG3d",
        "-lTKG2d",
        "-lTKMath",
        "-lTKernel",
        "-lTKBO"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ],
      "conditions": [
        [
          "OS=='mac'",
          {
            "xcode_settings": {
              "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
              "GCC_ENABLE_CPP_RTTI": "YES",
              "CLANG_CXX_LIBRARY": "libc++",
              "MACOSX_DEPLOYMENT_TARGET": "10.15",
              "OTHER_CFLAGS": [
                "-std=c++17",
                "-stdlib=libc++",
                "-frtti"
              ],
              "OTHER_LDFLAGS": [
                "-Wl,-rpath,<!(echo $HOME)/Libraries/opencascade/7.8.1/lib"
              ]
            }
          }
        ],
        [
          "OS=='linux'",
          {
            "cflags_cc": [
              "-std=c++17",
              "-fexceptions"
            ],
            "ldflags": [
              "-Wl,-rpath,<!(echo $HOME)/Libraries/opencascade/7.8.1/lib"
            ]
          }
        ]
      ]
    }
  ]
}
