set_xmakever("3.0.0")
set_project("AetherialRadianceSneak")
set_version("1.0.0")
set_arch("x64")
set_languages("c++23")
-- static Visual C++ runtime: the DLL carries its own, so a player with older Visual C++ files cannot crash at load
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

set_config("skyrim_se", true)
set_config("skyrim_ae", true)
set_config("skyrim_vr", false)

includes("lib/commonlibsse-ng")

target("AetherialRadianceSneak", function()
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "AetherialRadianceSneak",
        author = "izzydoingit",
        description = "Aetherial Radiance - spell lights go out while you sneak",
    })
    add_files("src/main.cpp")
end)
