-- RELight - Spell Addon - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("RELightSpellAddonSneak")
set_version("1.0.0")
set_license("GPL-3.0-or-later")
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

target("RELightSpellAddonSneak", function()
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "RELightSpellAddonSneak",
        author = "izzydoingit",
        description = "RELight - Spell Addon - spell lights go out while you sneak",
    })
    -- the source is split by job (see the file map at the top of src/main.cpp); every .cpp in src is built
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
end)
